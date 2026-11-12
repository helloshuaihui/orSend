// 带读写缓冲的 TCP 连接。非阻塞 + 背压控制，所有方法必须在 loop 线程调用。
#pragma once
#ifndef ORSEND_TCPCONNECTION_H
#define ORSEND_TCPCONNECTION_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Common.h"
#include "EventLoop.h"
#include "NetCompat.h"

namespace orsend {

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using Ptr = std::shared_ptr<TcpConnection>;
    using DataCallback = std::function<void(const char*, size_t)>;
    using CloseCallback = std::function<void()>;
    using ConnectCallback = std::function<void(Ptr, const std::string& error)>;

    // 发起非阻塞连接。成功时回调中 error 为空。
    static void Connect(EventLoop& loop, const Endpoint& endpoint, int timeoutMs, ConnectCallback cb);

    // 接管一个已连接的 socket
    TcpConnection(EventLoop& loop, socket_t fd, const Endpoint& peer);
    ~TcpConnection();

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    void Start();

    void SetOnData(DataCallback cb) { onData_ = std::move(cb); }
    void SetOnClose(CloseCallback cb) { onClose_ = std::move(cb); }

    void Send(const void* data, size_t len);
    void Send(const std::string& data) { Send(data.data(), data.size()); }

    // 优雅关闭：先把发送缓冲刷完再关
    void Close();
    // 立即关闭
    void CloseNow();

    bool Connected() const { return connected_; }
    bool Closing() const { return closing_; }
    bool Closed() const { return closed_; }
    socket_t Fd() const { return fd_; }
    const Endpoint& Peer() const { return peer_; }

    uint64_t BytesIn() const { return bytesIn_; }
    uint64_t BytesOut() const { return bytesOut_; }
    size_t PendingWrite() const { return outBuf_.size() - outSent_; }

    // 背压：写缓冲超过 high 时暂停读，低于 low 时恢复
    void SetWaterMark(size_t high, size_t low);

    void SetNoDelay(bool on);

private:
    void UpdateEvents();
    void HandleEvent(uint32_t events);
    void HandleRead();
    void HandleWrite();
    void Flush();
    void DoClose();

    EventLoop& loop_;
    socket_t fd_ = kInvalidSocket;
    Endpoint peer_;

    uint32_t events_ = EventLoop::kReadable;
    bool connected_ = false;
    bool closing_ = false;
    bool closeAfterFlush_ = false;
    bool readPaused_ = false;
    bool closed_ = false;

    std::vector<char> inBuf_;
    std::vector<char> outBuf_;
    size_t outSent_ = 0;

    size_t highWaterMark_ = 1024 * 1024;
    size_t lowWaterMark_ = 256 * 1024;

    uint64_t bytesIn_ = 0;
    uint64_t bytesOut_ = 0;

    DataCallback onData_;
    CloseCallback onClose_;
};

}  // namespace orsend

#endif  // ORSEND_TCPCONNECTION_H
