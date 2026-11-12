// 本地控制接口：127.0.0.1 上的「一行一个 JSON」RPC
//
//   请求: {"id":1,"cmd":"status","args":{}}
//   响应: {"id":1,"ok":true,"data":{...}}
//         {"id":1,"ok":false,"error":"原因"}
//
// C# 端的服务端 / 客户端页面通过这个接口驱动引擎。
#pragma once
#ifndef ORSEND_CONTROLSERVER_H
#define ORSEND_CONTROLSERVER_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "Common.h"
#include "EventLoop.h"
#include "Json.h"
#include "TcpConnection.h"
#include "TcpListener.h"

namespace orsend {

class ControlServer {
public:
    // 返回 false 时 error 会作为错误信息回给调用方
    using Handler = std::function<bool(const std::string& cmd, const Json& args, Json& data, std::string& error)>;

    ControlServer(EventLoop& loop, Handler handler);
    ~ControlServer();

    bool Start(const Endpoint& endpoint, std::string* error);
    void Stop();
    bool Running() const { return static_cast<bool>(listener_); }
    uint16_t Port() const { return port_; }
    size_t ClientCount() const { return clients_.size(); }

private:
    struct Client {
        TcpConnection::Ptr conn;
        std::string buffer;
    };

    void HandleAccept(socket_t fd, const Endpoint& peer);
    void HandleData(uint64_t clientId, const char* data, size_t len);
    void HandleLine(uint64_t clientId, const std::string& line);

    EventLoop& loop_;
    Handler handler_;
    TcpListener::Ptr listener_;
    uint16_t port_ = 0;
    std::map<uint64_t, Client> clients_;
    uint64_t nextClientId_ = 1;
};

}  // namespace orsend

#endif  // ORSEND_CONTROLSERVER_H
