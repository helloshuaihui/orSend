#include "EventLoop.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>

#include "Logger.h"

namespace orsend {

namespace {
uint32_t ToPollEvents(uint32_t mask) {
    uint32_t events = 0;
    if (mask & EventLoop::kReadable) events |= POLLIN;
    if (mask & EventLoop::kWritable) events |= POLLOUT;
    return events;
}

uint32_t FromPollEvents(int16_t revents) {
    uint32_t mask = 0;
    if (revents & POLLIN) mask |= EventLoop::kReadable;
    if (revents & POLLOUT) mask |= EventLoop::kWritable;
#ifdef POLLRDHUP
    if (revents & POLLRDHUP) mask |= EventLoop::kReadable;
#endif
    if (revents & (POLLERR | POLLHUP | POLLNVAL)) mask |= EventLoop::kError;
    return mask;
}
}  // namespace

EventLoop::EventLoop() { running_.store(false); }

EventLoop::~EventLoop() {
    Stop();
    handlers_.clear();
    timers_.clear();
}

int64_t EventLoop::NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void EventLoop::Post(std::function<void()> task) {
    if (!task) return;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        tasks_.push_back(std::move(task));
    }
}

void EventLoop::Stop() {
    stopRequested_.store(true);
}

bool EventLoop::AddFd(Fd fd, uint32_t events, EventCallback cb) {
    if (fd == kInvalidSocket) return false;
    Handler h;
    h.events = events;
    h.token = nextToken_++;
    h.cb = std::move(cb);
    handlers_[fd] = std::move(h);
    return true;
}

bool EventLoop::ModifyFd(Fd fd, uint32_t events) {
    auto it = handlers_.find(fd);
    if (it == handlers_.end()) return false;
    it->second.events = events;
    return true;
}

void EventLoop::RemoveFd(Fd fd) { handlers_.erase(fd); }

bool EventLoop::HasFd(Fd fd) const { return handlers_.find(fd) != handlers_.end(); }

EventLoop::TimerId EventLoop::RunAfter(int64_t delayMs, std::function<void()> cb) {
    Timer t;
    t.id = nextTimerId_++;
    t.dueMs = NowMs() + std::max<int64_t>(0, delayMs);
    t.intervalMs = 0;
    t.cb = std::move(cb);
    timers_.push_back(std::move(t));
    return nextTimerId_ - 1;
}

EventLoop::TimerId EventLoop::RunEvery(int64_t intervalMs, std::function<void()> cb) {
    Timer t;
    t.id = nextTimerId_++;
    t.intervalMs = std::max<int64_t>(10, intervalMs);
    t.dueMs = NowMs() + t.intervalMs;
    t.cb = std::move(cb);
    timers_.push_back(std::move(t));
    return nextTimerId_ - 1;
}

void EventLoop::CancelTimer(TimerId id) {
    for (auto& t : timers_) {
        if (t.id == id) {
            t.cancelled = true;
            return;
        }
    }
}

void EventLoop::DrainPosted() {
    std::vector<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        if (tasks_.empty()) return;
        local.swap(tasks_);
    }
    for (auto& task : local) {
        try {
            task();
        } catch (const std::exception& e) {
            LOG_ERROR("loop", "投递任务异常: " << e.what());
        } catch (...) {
            LOG_ERROR("loop", "投递任务未知异常");
        }
    }
}

int EventLoop::ComputeTimeout(int64_t now) const {
    int64_t nextDue = std::numeric_limits<int64_t>::max();
    for (const auto& t : timers_) {
        if (t.cancelled) continue;
        if (t.dueMs < nextDue) nextDue = t.dueMs;
    }
    int timeout = 50;  // 上限 50ms：兼顾定时器精度与跨线程投递延迟
    if (nextDue != std::numeric_limits<int64_t>::max()) {
        int64_t diff = nextDue - now;
        if (diff < 0) diff = 0;
        if (diff < timeout) timeout = static_cast<int>(diff);
    }
    return timeout;
}

void EventLoop::FireTimers(int64_t now) {
    // 先收集到期的定时器，再执行回调：回调中增删定时器不会破坏遍历
    std::vector<size_t> due;
    for (size_t i = 0; i < timers_.size(); ++i) {
        if (!timers_[i].cancelled && timers_[i].dueMs <= now) due.push_back(i);
    }
    for (size_t idx : due) {
        if (idx >= timers_.size()) continue;
        Timer& t = timers_[idx];
        if (t.cancelled) continue;
        if (t.intervalMs > 0) {
            t.dueMs = now + t.intervalMs;
        } else {
            t.cancelled = true;
        }
        auto cb = t.cb;  // 拷贝，防止回调中容器被修改
        if (cb) {
            try {
                cb();
            } catch (const std::exception& e) {
                LOG_ERROR("loop", "定时器回调异常: " << e.what());
            } catch (...) {
                LOG_ERROR("loop", "定时器回调未知异常");
            }
        }
    }
}

void EventLoop::CompactTimers() {
    size_t cancelled = 0;
    for (const auto& t : timers_) {
        if (t.cancelled) ++cancelled;
    }
    if (cancelled < 32 || cancelled * 2 < timers_.size()) return;
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                                 [](const Timer& t) { return t.cancelled; }),
                  timers_.end());
}

void EventLoop::Run() {
    running_.store(true);
    stopRequested_.store(false);
    LOG_DEBUG("loop", "事件循环启动");

    while (!stopRequested_.load()) {
        DrainPosted();

        int64_t now = NowMs();
        FireTimers(now);
        CompactTimers();

        pollfds_.clear();
        pollOrder_.clear();
        pollfds_.reserve(handlers_.size());
        pollOrder_.reserve(handlers_.size());
        for (auto& kv : handlers_) {
            pollfd_t p{};
            p.fd = kv.first;
            p.events = static_cast<short>(ToPollEvents(kv.second.events));
            p.revents = 0;
            pollfds_.push_back(p);
            pollOrder_.push_back(kv.first);
        }

        const int timeout = ComputeTimeout(NowMs());
        int n = 0;
        if (pollfds_.empty()) {
            // 没有 fd 时用简单休眠代替 poll，避免空转
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
        } else {
            n = PollOnce(pollfds_.data(), static_cast<unsigned long>(pollfds_.size()), timeout);
            if (n < 0) {
                const int err = LastSocketError();
#ifdef _WIN32
                if (err == WSAEINTR) continue;
#else
                if (err == EINTR) continue;
#endif
                LOG_ERROR("loop", "poll 失败: " << SocketErrorString(err));
                continue;
            }
        }

        if (n <= 0) continue;

        ready_.clear();
        for (size_t i = 0; i < pollfds_.size(); ++i) {
            if (pollfds_[i].revents == 0) continue;
            ready_.emplace_back(pollOrder_[i], FromPollEvents(pollfds_[i].revents));
        }

        for (auto& item : ready_) {
            auto it = handlers_.find(item.first);
            if (it == handlers_.end()) continue;
            const uint64_t token = it->second.token;
            auto cb = it->second.cb;  // 拷贝：回调中可能移除自身
            if (!cb) continue;
            // 派发期间 fd 可能被关闭并复用，用 token 二次确认
            auto check = handlers_.find(item.first);
            if (check == handlers_.end() || check->second.token != token) continue;
            try {
                cb(item.second);
            } catch (const std::exception& e) {
                LOG_ERROR("loop", "fd 回调异常: " << e.what());
            } catch (...) {
                LOG_ERROR("loop", "fd 回调未知异常");
            }
        }
    }

    running_.store(false);
    LOG_DEBUG("loop", "事件循环退出");
}

}  // namespace orsend
