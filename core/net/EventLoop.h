// 单线程事件循环：fd 事件（poll 多路复用）+ 定时器 + 跨线程投递
//
// 设计要点：
//   * 所有 fd 的注册/注销/回调都在 loop 线程执行，避免锁竞争；
//   * 其它线程通过 Post() 投递任务，loop 每轮最多阻塞 50ms 保证及时唤醒；
//   * 回调内可以安全地增删 fd / 定时器（派发前会做 token 校验，防止 fd 复用导致误派发）。
#pragma once
#ifndef ORSEND_EVENTLOOP_H
#define ORSEND_EVENTLOOP_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "NetCompat.h"

namespace orsend {

class EventLoop {
public:
    enum EventMask : uint32_t {
        kReadable = 1u,
        kWritable = 2u,
        kError = 4u,
    };

    using Fd = socket_t;
    using EventCallback = std::function<void(uint32_t)>;
    using TimerId = uint64_t;

    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void Run();
    void Stop();
    bool IsRunning() const { return running_.load(); }

    // 线程安全：把任务投递到 loop 线程执行
    void Post(std::function<void()> task);

    // 定时器（loop 线程调用；跨线程请用 Post 包一层）
    TimerId RunAfter(int64_t delayMs, std::function<void()> cb);
    TimerId RunEvery(int64_t intervalMs, std::function<void()> cb);
    void CancelTimer(TimerId id);

    // fd 管理（仅 loop 线程）
    bool AddFd(Fd fd, uint32_t events, EventCallback cb);
    bool ModifyFd(Fd fd, uint32_t events);
    void RemoveFd(Fd fd);
    bool HasFd(Fd fd) const;
    size_t FdCount() const { return handlers_.size(); }
    size_t TimerCount() const { return timers_.size(); }

    static int64_t NowMs();

private:
    struct Handler {
        uint32_t events = 0;
        uint64_t token = 0;
        EventCallback cb;
    };

    struct Timer {
        TimerId id = 0;
        int64_t dueMs = 0;
        int64_t intervalMs = 0;
        bool cancelled = false;
        std::function<void()> cb;
    };

    void DrainPosted();
    int ComputeTimeout(int64_t now) const;
    void FireTimers(int64_t now);
    void CompactTimers();

    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};

    std::unordered_map<Fd, Handler> handlers_;
    std::vector<Timer> timers_;
    std::vector<pollfd_t> pollfds_;
    std::vector<Fd> pollOrder_;
    std::vector<std::pair<Fd, uint32_t>> ready_;

    uint64_t nextToken_ = 1;
    TimerId nextTimerId_ = 1;

    mutable std::mutex taskMutex_;
    std::vector<std::function<void()>> tasks_;
};

}  // namespace orsend

#endif  // ORSEND_EVENTLOOP_H
