// 内网穿透客户端（agent，部署在内网机器上）
//
//  1. 主动连接公网服务端（无需公网 IP / 无需路由器端口映射）
//  2. 完成 token 认证后注册本地隧道
//  3. 服务端收到公网连接时通知本端，由本端拨号内网目标并双向转发
#pragma once
#ifndef ORSEND_TUNNELCLIENT_H
#define ORSEND_TUNNELCLIENT_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Common.h"
#include "ConnectionReaper.h"
#include "EventLoop.h"
#include "Json.h"
#include "Protocol.h"
#include "TcpConnection.h"
#include "TunnelServer.h"  // 复用 TunnelDef

namespace orsend {

class TunnelClient {
public:
    enum class State { Idle = 0, Connecting = 1, Handshaking = 2, Ready = 3, Backoff = 4 };

    struct Options {
        Endpoint server;
        std::string token;
        std::string clientId;
        std::string name;
        std::vector<TunnelDef> tunnels;
        int reconnectMinMs = 1000;
        int reconnectMaxMs = 30000;
        int connectTimeoutMs = 8000;
        int heartbeatSec = 20;
    };

    explicit TunnelClient(EventLoop& loop);
    ~TunnelClient();

    TunnelClient(const TunnelClient&) = delete;
    TunnelClient& operator=(const TunnelClient&) = delete;

    bool Start(const Options& options, std::string* error);
    void Stop();
    bool Running() const { return running_; }
    State GetState() const { return state_; }
    const char* StateName() const;

    bool AddTunnel(const TunnelDef& def, std::string* error);
    bool RemoveTunnel(const std::string& name);
    Json Snapshot() const;

    std::function<void(const std::string& level, const std::string& message)> onEvent;

private:
    struct TunnelState {
        TunnelDef def;
        bool registered = false;
        uint16_t publicPort = 0;
        std::string lastError;
        uint64_t connections = 0;
        uint64_t bytesIn = 0;
        uint64_t bytesOut = 0;
    };

    struct LocalStream {
        uint32_t id = 0;
        std::string tunnelName;
        TcpConnection::Ptr local;
        bool closed = false;
        int64_t startMs = 0;
    };

    void Emit(const std::string& level, const std::string& msg);
    void SetState(State s, const std::string& reason);

    void DoConnect();
    void ScheduleReconnect();
    void OnConnected();
    void OnDisconnected(const std::string& reason);
    void SendHello();
    void RegisterAll();
    void SendRegister(const std::string& tunnelName);
    void SendFrame(proto::MsgType type, uint32_t streamId, const std::string& payload);
    void HandleData(const char* data, size_t len);
    void HandleFrame(const proto::Frame& frame);
    void HandleHelloAck(const proto::Frame& frame);
    void HandleRegisterAck(const proto::Frame& frame);
    void HandleTunnelOpen(const proto::Frame& frame);
    void HandleDataFrame(const proto::Frame& frame);
    void HandleCloseFrame(const proto::Frame& frame);
    void CloseStream(uint32_t id, const std::string& reason, bool notifyServer);
    void StartHeartbeat();
    void StopHeartbeat();

    EventLoop& loop_;
    Options options_;
    bool running_ = false;
    State state_ = State::Idle;

    TcpConnection::Ptr conn_;
    proto::Decoder decoder_;
    EventLoop::TimerId reconnectTimer_ = 0;
    EventLoop::TimerId handshakeTimer_ = 0;
    EventLoop::TimerId heartbeatTimer_ = 0;

    int currentBackoffMs_ = 0;
    uint64_t generation_ = 0;  // 连接代际，用于忽略旧连接的回调
    int64_t lastPongMs_ = 0;
    int64_t connectedAtMs_ = 0;
    int64_t lastErrorMs_ = 0;
    std::string lastError_;

    std::map<std::string, TunnelState> tunnels_;
    std::map<uint32_t, std::shared_ptr<LocalStream>> streams_;
    uint32_t nextStreamId_ = 1;
    ConnectionReaper reaper_;  // 内网连接优雅关闭
};

}  // namespace orsend

#endif  // ORSEND_TUNNELCLIENT_H
