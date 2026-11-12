#include "TcpConnection.h"

#include <algorithm>
#include <cstring>

#include "Logger.h"

namespace orsend {

namespace {

struct ConnectState {
    EventLoop* loop = nullptr;
    socket_t fd = kInvalidSocket;
    Endpoint endpoint;
    TcpConnection::ConnectCallback cb;
    EventLoop::TimerId timer = 0;
    bool done = false;
};

int GetSockError(socket_t fd) {
    int err = 0;
#ifdef _WIN32
    int len = sizeof(err);
#else
    socklen_t len = sizeof(err);
#endif
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0) {
        err = LastSocketError();
    }
    return err;
}

bool ApplySocketOptions(socket_t fd) {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&one), sizeof(one));
    return true;
}

void FinishConnect(const std::shared_ptr<ConnectState>& st, bool ok, const std::string& error) {
    if (!st || st->done) return;
    st->done = true;
    if (st->timer != 0) st->loop->CancelTimer(st->timer);
    st->loop->RemoveFd(st->fd);

    if (!ok) {
        CloseSocket(st->fd);
        auto cb = st->cb;
        st->cb = nullptr;
        st->loop = nullptr;
        st->fd = kInvalidSocket;
        if (cb) cb(nullptr, error);
        return;
    }

    ApplySocketOptions(st->fd);
    auto conn = std::make_shared<TcpConnection>(*st->loop, st->fd, st->endpoint);
    conn->Start();
    auto cb = st->cb;
    st->cb = nullptr;
    st->loop = nullptr;
    st->fd = kInvalidSocket;
    if (cb) cb(conn, std::string());
}

}  // namespace

void TcpConnection::Connect(EventLoop& loop, const Endpoint& endpoint, int timeoutMs, ConnectCallback cb) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(endpoint.port);
    if (inet_pton(AF_INET, endpoint.host.c_str(), &addr.sin_addr) != 1) {
        // 尝试按主机名解析
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(endpoint.host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) {
            if (cb) cb(nullptr, "无法解析地址: " + endpoint.host);
            return;
        }
        addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }

    socket_t fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidSocket) {
        if (cb) cb(nullptr, "创建 socket 失败: " + SocketErrorString(LastSocketError()));
        return;
    }
    SetNonBlockingImpl(fd, true);
    ApplySocketOptions(fd);

    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc == 0) {
        auto conn = std::make_shared<TcpConnection>(loop, fd, endpoint);
        conn->Start();
        if (cb) cb(conn, std::string());
        return;
    }

    const int err = LastSocketError();
    if (!IsInProgress(err)) {
        CloseSocket(fd);
        if (cb) cb(nullptr, "连接 " + endpoint.ToString() + " 失败: " + SocketErrorString(err));
        return;
    }

    auto st = std::make_shared<ConnectState>();
    st->loop = &loop;
    st->fd = fd;
    st->endpoint = endpoint;
    st->cb = std::move(cb);

    // 注意：这里必须强引用持有 st，否则 Connect 返回后状态对象立即析构，
    // 回调里的 weak_ptr 全部失效，连接永远不会有结果。
    loop.AddFd(fd, EventLoop::kWritable | EventLoop::kError, [st](uint32_t) {
        if (!st || st->done) return;
        const int err = GetSockError(st->fd);
        if (err == 0) {
            FinishConnect(st, true, std::string());
        } else {
            FinishConnect(st, false,
                          "连接 " + st->endpoint.ToString() + " 失败: " + SocketErrorString(err));
        }
    });

    if (timeoutMs > 0) {
        st->timer = loop.RunAfter(timeoutMs, [st]() {
            if (!st || st->done) return;
            FinishConnect(st, false, "连接 " + st->endpoint.ToString() + " 超时");
        });
    }
}

TcpConnection::TcpConnection(EventLoop& loop, socket_t fd, const Endpoint& peer)
    : loop_(loop), fd_(fd), peer_(peer) {
    inBuf_.resize(64 * 1024);
}

TcpConnection::~TcpConnection() {
    if (fd_ != kInvalidSocket) {
        loop_.RemoveFd(fd_);
        CloseSocket(fd_);
        fd_ = kInvalidSocket;
    }
}

void TcpConnection::Start() {
    if (fd_ == kInvalidSocket) return;
    connected_ = true;
    std::weak_ptr<TcpConnection> weak = shared_from_this();
    loop_.AddFd(fd_, EventLoop::kReadable, [weak](uint32_t events) {
        auto self = weak.lock();
        if (self) self->HandleEvent(events);
    });
    events_ = EventLoop::kReadable;
}

void TcpConnection::SetNoDelay(bool on) {
    if (fd_ == kInvalidSocket) return;
    int flag = on ? 1 : 0;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
}

void TcpConnection::SetWaterMark(size_t high, size_t low) {
    highWaterMark_ = high;
    lowWaterMark_ = low;
    if (lowWaterMark_ > highWaterMark_) lowWaterMark_ = highWaterMark_ / 2;
}

void TcpConnection::UpdateEvents() {
    uint32_t want = 0;
    if (!readPaused_ && !closing_) want |= EventLoop::kReadable;
    if (outBuf_.size() > outSent_) want |= EventLoop::kWritable;
    if (want == events_) return;
    events_ = want;
    loop_.ModifyFd(fd_, want);
}

void TcpConnection::HandleEvent(uint32_t events) {
    if (closed_) return;
    if (events & EventLoop::kError) {
        // POLLHUP / POLLERR 经常与"对端已把数据发完并关闭"同时上报。
        // 此时接收缓冲里可能还有没读走的数据，必须读干净再关，
        // 否则最后一段数据会被直接丢弃（现象：响应被截断或只剩响应头）。
        HandleRead();
        if (closed_) return;
        HandleWrite();
        if (closed_) return;
        if (outBuf_.size() > outSent_) {
            Close();  // 还有数据没写完，刷完再关
        } else {
            DoClose();
        }
        return;
    }
    if (events & EventLoop::kWritable) HandleWrite();
    if (closed_) return;
    if (events & EventLoop::kReadable) HandleRead();
}

void TcpConnection::HandleRead() {
    if (closed_ || readPaused_) return;
    for (;;) {
        if (inBuf_.empty()) inBuf_.resize(64 * 1024);
        int n = ::recv(fd_, inBuf_.data(), static_cast<int>(inBuf_.size()), 0);
        if (n > 0) {
            bytesIn_ += static_cast<uint64_t>(n);
            if (onData_) onData_(inBuf_.data(), static_cast<size_t>(n));
            if (closed_) return;
            if (readPaused_) return;
            continue;
        }
        if (n == 0) {
            DoClose();
            return;
        }
        const int err = LastSocketError();
        if (IsWouldBlock(err)) return;
        DoClose();
        return;
    }
}

void TcpConnection::HandleWrite() {
    if (closed_) return;
    Flush();
    if (closed_) return;
    if (closeAfterFlush_ && outSent_ >= outBuf_.size()) {
        DoClose();
        return;
    }
    UpdateEvents();
}

void TcpConnection::Flush() {
    while (outSent_ < outBuf_.size()) {
        const int len = static_cast<int>(outBuf_.size() - outSent_);
        int n = ::send(fd_, outBuf_.data() + outSent_, len, 0);
        if (n > 0) {
            outSent_ += static_cast<size_t>(n);
            bytesOut_ += static_cast<uint64_t>(n);
            continue;
        }
        if (n < 0) {
            const int err = LastSocketError();
            if (IsWouldBlock(err)) break;
            DoClose();
            return;
        }
        break;
    }

    if (outSent_ >= outBuf_.size()) {
        outBuf_.clear();
        outSent_ = 0;
        if (readPaused_) {
            readPaused_ = false;
            UpdateEvents();
        }
    }
}

void TcpConnection::Send(const void* data, size_t len) {
    if (closed_ || len == 0) return;
    const char* p = static_cast<const char*>(data);
    outBuf_.insert(outBuf_.end(), p, p + len);
    Flush();
    if (closed_) return;

    if (outBuf_.size() - outSent_ > highWaterMark_ && !readPaused_) {
        readPaused_ = true;
        UpdateEvents();
    }
    if (outSent_ < outBuf_.size()) UpdateEvents();
}

void TcpConnection::Close() {
    if (closed_) return;
    if (outBuf_.size() > outSent_) {
        closeAfterFlush_ = true;
        closing_ = true;
        UpdateEvents();
        // 兜底：5 秒内没刷完就强制关闭
        std::weak_ptr<TcpConnection> weak = shared_from_this();
        loop_.RunAfter(5000, [weak]() {
            auto self = weak.lock();
            if (self) self->DoClose();
        });
        return;
    }
    DoClose();
}

void TcpConnection::CloseNow() { DoClose(); }

void TcpConnection::DoClose() {
    if (closed_) return;
    closed_ = true;
    connected_ = false;
    if (fd_ != kInvalidSocket) {
        loop_.RemoveFd(fd_);
        CloseSocket(fd_);
        fd_ = kInvalidSocket;
    }
    outBuf_.clear();
    outSent_ = 0;
    auto cb = onClose_;
    onClose_ = nullptr;
    onData_ = nullptr;
    if (cb) cb();
}

}  // namespace orsend
