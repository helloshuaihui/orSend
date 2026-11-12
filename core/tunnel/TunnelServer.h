// 内网穿透服务端（公网侧）
//
//  公网用户 --> [公网端口 18080] --> TunnelServer --(复用控制连接)--> TunnelClient --> [内网 127.0.0.1:8080]
//
// 服务端职责：
//   * 接受 agent 接入、校验 token
//   * 为 agent 注册的隧道在公网侧监听端口
//   * 公网连接进来时分配 stream，通过控制连接通知 agent 回连内网目标
#pragma once
#ifndef ORSEND_TUNNELSERVER_H
#define ORSEND_TUNNELSERVER_H

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
#include "TcpListener.h"

namespace orsend {

struct TunnelDef {
    std::string name;
    Endpoint local;            // 内网目标地址（由 agent 解析）
    uint16_t remotePort = 0;   // 公网端口，0 表示由服务端自动分配
    bool enabled = true;
};

class TunnelServer {
public:
    struct Options {
        Endpoint bind;                          // agent 接入地址，如 0.0.0.0:7000
        std::string token;                      // 共享密钥，为空表示不校验
        std::string publicHost = "0.0.0.0";     // 公网端口监听地址
        int heartbeatTimeoutSec = 90;
        int maxTunnelsPerAgent = 32;
    };

    explicit TunnelServer(EventLoop& loop);
    ~TunnelServer();

    TunnelServer(const TunnelServer&) = delete;
    TunnelServer& operator=(const TunnelServer&) = delete;

    bool Start(const Options& options, std::string* error);
    void Stop();
    bool Running() const { return static_cast<bool>(controlListener_); }

    Json Snapshot() const;
    Json SnapshotAgents() const;
    Json SnapshotTunnels() const;

    // 主动断开某个 agent
    bool KickAgent(const std::string& clientId);

    std::function<void(const std::string& level, const std::string& message)> onEvent;

private:
    struct AgentSession {
        uint64_t id = 0;
        TcpConnection::Ptr conn;
        proto::Decoder decoder;
        std::string clientId;
        std::string name;
        std::string version;
        std::string remoteAddr;
        bool authed = false;
        int64_t connectedAt = 0;
        int64_t lastSeenMs = 0;
        uint64_t bytesIn = 0;
        uint64_t bytesOut = 0;
        uint64_t streamsTotal = 0;
    };

    struct TunnelRuntime {
        TunnelDef def;
        uint64_t agentId = 0;
        std::string clientId;
        std::string key;  // clientId + "/" + name
        TcpListener::Ptr listener;
        uint16_t actualPort = 0;
        uint64_t connections = 0;
        uint64_t activeStreams = 0;
        uint64_t bytesIn = 0;
        uint64_t bytesOut = 0;
        int64_t createdAt = 0;
    };

    struct Stream {
        uint32_t id = 0;
        uint64_t agentId = 0;
        std::string tunnelKey;
        TcpConnection::Ptr publicConn;
        std::string pending;
        bool acked = false;
        bool closed = false;
        int64_t startMs = 0;
        uint64_t bytesIn = 0;
        uint64_t bytesOut = 0;
    };

    void Emit(const std::string& level, const std::string& msg);
    void HandleAgentAccept(socket_t fd, const Endpoint& peer);
    void HandleAgentData(uint64_t agentId, const char* data, size_t len);
    void HandleAgentClose(uint64_t agentId);
    void HandleFrame(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame);

    void HandleHello(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame);
    void HandleRegister(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame);
    void HandleUnregister(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame);
    void HandleOpenAck(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame);
    void HandleDataFrame(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame);
    void HandleCloseFrame(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame);

    void HandlePublicAccept(const std::string& tunnelKey, socket_t fd, const Endpoint& peer);
    void CloseStream(uint32_t streamId, const std::string& reason, bool notifyAgent);
    void CloseTunnel(const std::string& key, const std::string& reason);
    void CloseAgent(uint64_t agentId, const std::string& reason);

    bool SendFrame(uint64_t agentId, proto::MsgType type, uint32_t streamId, const std::string& payload);
    uint32_t AllocStreamId();

    EventLoop& loop_;
    Options options_;
    TcpListener::Ptr controlListener_;
    EventLoop::TimerId watchdogTimer_ = 0;
    ConnectionReaper reaper_;  // 公网连接优雅关闭（刷完缓冲再断）

    std::map<uint64_t, std::shared_ptr<AgentSession>> agents_;
    std::map<std::string, std::shared_ptr<TunnelRuntime>> tunnels_;  // key: clientId/name
    std::map<uint32_t, std::shared_ptr<Stream>> streams_;

    uint64_t nextAgentId_ = 1;
    uint32_t nextStreamId_ = 1;
    int64_t startedAt_ = 0;
};

}  // namespace orsend

#endif  // ORSEND_TUNNELSERVER_H
