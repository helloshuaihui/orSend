// 通用 TCP 转发引擎：监听本地端口 -> 连接目标 -> 双向转发
//
// 端口转发（PortForward）与端口映射（PortMapping）共用这套引擎，
// 区别只在于规则里目标的数量与选择策略。
#pragma once
#ifndef ORSEND_RELAYENGINE_H
#define ORSEND_RELAYENGINE_H

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
#include "TcpConnection.h"
#include "TcpListener.h"

namespace orsend {

enum class RelayStrategy {
    RoundRobin = 0,  // 轮询
    Random = 1,      // 随机
    Failover = 2,    // 主备（按顺序尝试，失败切下一个）
    LeastConn = 3,   // 最少连接
};

const char* RelayStrategyName(RelayStrategy s);
RelayStrategy ParseRelayStrategy(const std::string& text);

struct RelayTarget {
    Endpoint endpoint;
    int weight = 1;
};

struct RelayRule {
    std::string name;
    Endpoint listen;
    std::vector<RelayTarget> targets;
    RelayStrategy strategy = RelayStrategy::RoundRobin;
    bool healthCheck = false;
    int healthIntervalSec = 10;
    int connectTimeoutMs = 5000;
    int maxRetry = 2;  // 单次连接最多尝试的目标数
};

struct RelayStats {
    uint64_t connections = 0;
    uint64_t active = 0;
    uint64_t failed = 0;
    uint64_t bytesIn = 0;
    uint64_t bytesOut = 0;
};

class RelayEngine {
public:
    explicit RelayEngine(EventLoop& loop);
    ~RelayEngine();

    RelayEngine(const RelayEngine&) = delete;
    RelayEngine& operator=(const RelayEngine&) = delete;

    bool AddRule(const RelayRule& rule, std::string* error);
    bool RemoveRule(const std::string& name);
    bool HasRule(const std::string& name) const;
    size_t RuleCount() const { return rules_.size(); }
    std::vector<std::string> RuleNames() const;

    // 汇总快照，供控制接口 / 页面展示
    Json Snapshot() const;
    Json SnapshotRule(const std::string& name) const;

    void StopAll();

    // 错误/事件回调（日志用）
    std::function<void(const std::string& level, const std::string& message)> onEvent;

private:
    struct TargetState {
        bool up = true;
        bool probing = false;
        uint32_t active = 0;
        int64_t lastCheckMs = 0;
        int64_t lastOkMs = 0;
        std::string lastError;
    };

    struct Session {
        uint32_t id = 0;
        std::shared_ptr<TcpConnection> client;
        std::shared_ptr<TcpConnection> upstream;
        std::string ruleName;
        size_t targetIndex = 0;
        bool closed = false;
        int64_t startMs = 0;
    };

    struct RuleRuntime {
        RelayRule rule;
        TcpListener::Ptr listener;
        std::vector<TargetState> targets;
        size_t cursor = 0;
        RelayStats stats;
        std::map<uint32_t, std::shared_ptr<Session>> sessions;
        EventLoop::TimerId healthTimer = 0;
    };

    void HandleAccept(const std::string& ruleName, socket_t fd, const Endpoint& peer);
    void ConnectToTarget(std::shared_ptr<Session> session, int attempt);
    size_t PickTarget(RuleRuntime& rt);
    void WireSession(const std::shared_ptr<Session>& session);
    void CloseSession(const std::shared_ptr<Session>& session);
    void StartHealthCheck(RuleRuntime& rt);
    void ProbeTarget(const std::string& ruleName, size_t index);
    void Emit(const std::string& level, const std::string& msg);

    EventLoop& loop_;
    std::map<std::string, std::unique_ptr<RuleRuntime>> rules_;
    uint32_t nextSessionId_ = 1;
    ConnectionReaper reaper_;  // 会话两端优雅关闭（刷完缓冲再断）
};

}  // namespace orsend

#endif  // ORSEND_RELAYENGINE_H
