// TCP 监听器：accept 后把新连接交给回调
#pragma once
#ifndef ORSEND_TCPLISTENER_H
#define ORSEND_TCPLISTENER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "Common.h"
#include "EventLoop.h"
#include "NetCompat.h"

namespace orsend {

class TcpListener : public std::enable_shared_from_this<TcpListener> {
public:
    using Ptr = std::shared_ptr<TcpListener>;
    using AcceptCallback = std::function<void(socket_t fd, const Endpoint& peer)>;

    // 创建并开始监听。失败返回 nullptr，error 中带原因。
    static Ptr Create(EventLoop& loop, const Endpoint& endpoint, AcceptCallback cb, std::string* error);

    ~TcpListener();

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    void Close();
    bool IsOpen() const { return fd_ != kInvalidSocket; }
    // 实际监听的端口（当请求端口为 0 时由系统分配）
    uint16_t LocalPort() const { return localPort_; }
    const Endpoint& BindEndpoint() const { return bindEndpoint_; }

private:
    TcpListener(EventLoop& loop, socket_t fd, const Endpoint& bind, uint16_t actualPort);
    void HandleAccept();

    EventLoop& loop_;
    socket_t fd_ = kInvalidSocket;
    Endpoint bindEndpoint_;
    uint16_t localPort_ = 0;
    AcceptCallback onAccept_;
};

}  // namespace orsend

#endif  // ORSEND_TCPLISTENER_H
