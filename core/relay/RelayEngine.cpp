#include "RelayEngine.h"

#include <algorithm>

#include "Logger.h"

namespace orsend {

const char* RelayStrategyName(RelayStrategy s) {
    switch (s) {
        case RelayStrategy::RoundRobin: return "roundrobin";
        case RelayStrategy::Random: return "random";
        case RelayStrategy::Failover: return "failover";
        case RelayStrategy::LeastConn: return "leastconn";
    }
    return "roundrobin";
}

RelayStrategy ParseRelayStrategy(const std::string& text) {
    const std::string s = ToLowerAscii(Trim(text));
    if (s == "random") return RelayStrategy::Random;
    if (s == "failover" || s == "backup") return RelayStrategy::Failover;
    if (s == "leastconn" || s == "least_conn") return RelayStrategy::LeastConn;
    return RelayStrategy::RoundRobin;
}

RelayEngine::RelayEngine(EventLoop& loop) : loop_(loop), reaper_(loop) {}

RelayEngine::~RelayEngine() { StopAll(); }

void RelayEngine::Emit(const std::string& level, const std::string& msg) {
    if (onEvent) onEvent(level, msg);
}

bool RelayEngine::AddRule(const RelayRule& rule, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (rule.name.empty()) return fail("规则名不能为空");
    if (rules_.find(rule.name) != rules_.end()) return fail("规则已存在: " + rule.name);
    if (rule.listen.port == 0) return fail("监听端口不能为 0: " + rule.name);
    if (rule.targets.empty()) return fail("目标列表为空: " + rule.name);

    auto rt = std::unique_ptr<RuleRuntime>(new RuleRuntime());
    rt->rule = rule;
    rt->targets.resize(rule.targets.size());

    if (rule.listen.port != 0) {
        std::string err;
        std::string ruleName = rule.name;
        rt->listener = TcpListener::Create(
            loop_, rule.listen,
            [this, ruleName](socket_t fd, const Endpoint& peer) { HandleAccept(ruleName, fd, peer); },
            &err);
        if (!rt->listener) return fail(err);
    }

    RuleRuntime* raw = rt.get();
    rules_[rule.name] = std::move(rt);

    if (rule.healthCheck) StartHealthCheck(*raw);

    LOG_INFO("relay", "规则已启动 [" << rule.name << "] " << rule.listen.ToString() << " -> "
                                     << rule.targets.size() << " 个目标, 策略="
                                     << RelayStrategyName(rule.strategy));
    return true;
}

bool RelayEngine::RemoveRule(const std::string& name) {
    auto it = rules_.find(name);
    if (it == rules_.end()) return false;
    RuleRuntime& rt = *it->second;

    if (rt.healthTimer != 0) loop_.CancelTimer(rt.healthTimer);
    if (rt.listener) rt.listener->Close();

    // 关闭该规则下所有会话
    std::vector<std::shared_ptr<Session>> sessions;
    sessions.reserve(rt.sessions.size());
    for (auto& kv : rt.sessions) sessions.push_back(kv.second);
    for (auto& s : sessions) CloseSession(s);

    rules_.erase(it);
    LOG_INFO("relay", "规则已停止 [" << name << "]");
    return true;
}

bool RelayEngine::HasRule(const std::string& name) const { return rules_.find(name) != rules_.end(); }

std::vector<std::string> RelayEngine::RuleNames() const {
    std::vector<std::string> names;
    names.reserve(rules_.size());
    for (const auto& kv : rules_) names.push_back(kv.first);
    return names;
}

void RelayEngine::StopAll() {
    std::vector<std::string> names = RuleNames();
    for (const auto& n : names) RemoveRule(n);
    rules_.clear();
    reaper_.Clear();
}

void RelayEngine::HandleAccept(const std::string& ruleName, socket_t fd, const Endpoint& peer) {
    auto it = rules_.find(ruleName);
    if (it == rules_.end()) {
        CloseSocket(fd);
        return;
    }
    RuleRuntime& rt = *it->second;

    auto session = std::make_shared<Session>();
    session->id = nextSessionId_++;
    session->ruleName = ruleName;
    session->startMs = NowMs();
    // 注意：这里先不 Start()，等目标连接建立后再开始读，天然形成背压
    session->client = std::make_shared<TcpConnection>(loop_, fd, peer);

    rt.stats.connections++;
    rt.stats.active++;
    rt.sessions[session->id] = session;

    LOG_DEBUG("relay", "[" << ruleName << "] 新连接 #" << session->id << " 来自 " << peer.ToString());

    ConnectToTarget(session, {});
}

size_t RelayEngine::PickTarget(RuleRuntime& rt) {
    const size_t count = rt.rule.targets.size();
    if (count == 0) return static_cast<size_t>(-1);

    // onlyHealthy=true 时只看健康检查通过的目标；false 表示全部不健康时的兜底
    auto select = [&](bool onlyHealthy) -> size_t {
        auto usable = [&](size_t i) { return !onlyHealthy || rt.targets[i].up; };

        switch (rt.rule.strategy) {
            case RelayStrategy::RoundRobin: {
                for (size_t k = 0; k < count; ++k) {
                    size_t idx = (rt.cursor + k) % count;
                    if (usable(idx)) {
                        rt.cursor = (idx + 1) % count;
                        return idx;
                    }
                }
                return static_cast<size_t>(-1);
            }
            case RelayStrategy::Random: {
                std::vector<size_t> candidates;
                for (size_t i = 0; i < count; ++i) {
                    if (usable(i)) candidates.push_back(i);
                }
                if (candidates.empty()) return static_cast<size_t>(-1);
                return candidates[RandomU32() % candidates.size()];
            }
            case RelayStrategy::LeastConn: {
                size_t best = static_cast<size_t>(-1);
                for (size_t i = 0; i < count; ++i) {
                    if (!usable(i)) continue;
                    if (best == static_cast<size_t>(-1) ||
                        rt.targets[i].active < rt.targets[best].active) {
                        best = i;
                    }
                }
                return best;
            }
            case RelayStrategy::Failover:
            default: {
                for (size_t i = 0; i < count; ++i) {
                    if (usable(i)) return i;
                }
                return static_cast<size_t>(-1);
            }
        }
    };

    const size_t idx = select(true);
    if (idx != static_cast<size_t>(-1)) return idx;

    // 所有目标都被健康检查判为不可用：退化成盲试一次。
    // 否则目标刚恢复时，规则要等到下一个探测周期才会重新可用。
    const size_t fallback = select(false);
    if (fallback != static_cast<size_t>(-1)) {
        LOG_DEBUG("relay", "[" << rt.rule.name << "] 无健康目标，尝试直接连接 #" << fallback);
    }
    return fallback;
}

void RelayEngine::ConnectToTarget(std::shared_ptr<Session> session, int attempt) {
    auto it = rules_.find(session->ruleName);
    if (it == rules_.end()) {
        CloseSession(session);
        return;
    }
    RuleRuntime& rt = *it->second;

    if (attempt >= std::max(1, rt.rule.maxRetry) || rt.rule.targets.empty()) {
        rt.stats.failed++;
        Emit("warn", "[" + session->ruleName + "] 无法连接到任何目标，关闭客户端连接 #" +
                         std::to_string(session->id));
        CloseSession(session);
        return;
    }

    // Failover 策略按顺序尝试，其它策略直接选一个
    size_t idx = static_cast<size_t>(-1);
    if (rt.rule.strategy == RelayStrategy::Failover) {
        for (size_t i = 0; i < rt.rule.targets.size(); ++i) {
            if (rt.targets[i].up) {
                idx = i;
                break;
            }
        }
    } else {
        idx = PickTarget(rt);
    }

    if (idx == static_cast<size_t>(-1)) {
        rt.stats.failed++;
        Emit("warn", "[" + session->ruleName + "] 没有可用的目标");
        CloseSession(session);
        return;
    }

    const Endpoint target = rt.rule.targets[idx].endpoint;
    const int timeout = rt.rule.connectTimeoutMs;
    rt.targets[idx].active++;
    session->targetIndex = idx;

    std::weak_ptr<Session> weakSession = session;
    std::string ruleName = session->ruleName;
    TcpConnection::Connect(loop_, target, timeout, [this, weakSession, idx, attempt, ruleName, target](
                                                       TcpConnection::Ptr conn, const std::string& err) {
        auto s = weakSession.lock();
        auto rit = rules_.find(ruleName);
        if (rit == rules_.end()) {
            if (conn) conn->CloseNow();
            return;
        }
        RuleRuntime& ruleRt = *rit->second;
        if (idx < ruleRt.targets.size()) {
            if (ruleRt.targets[idx].active > 0) ruleRt.targets[idx].active--;
        }

        if (!s || s->closed) {
            if (conn) conn->CloseNow();
            return;
        }

        if (!conn) {
            ruleRt.stats.failed++;
            if (idx < ruleRt.targets.size()) {
                ruleRt.targets[idx].up = false;
                ruleRt.targets[idx].lastError = err;
            }
            LOG_WARN("relay", "[" << ruleName << "] 目标连接失败: " << err);
            ConnectToTarget(s, attempt + 1);
            return;
        }

        if (idx < ruleRt.targets.size()) {
            ruleRt.targets[idx].up = true;
            ruleRt.targets[idx].lastOkMs = NowMs();
        }
        s->upstream = conn;
        WireSession(s);
        s->client->Start();
        LOG_DEBUG("relay", "[" << ruleName << "] 会话 #" << s->id << " 已连接到 " << target.ToString());
    });
}

void RelayEngine::WireSession(const std::shared_ptr<Session>& session) {
    std::weak_ptr<Session> weak = session;

    session->client->SetOnData([weak](const char* data, size_t len) {
        auto s = weak.lock();
        if (!s || s->closed || !s->upstream) return;
        s->upstream->Send(data, len);
    });
    session->upstream->SetOnData([weak](const char* data, size_t len) {
        auto s = weak.lock();
        if (!s || s->closed || !s->client) return;
        s->client->Send(data, len);
    });
    session->client->SetOnClose([this, weak]() {
        auto s = weak.lock();
        if (s) CloseSession(s);
    });
    session->upstream->SetOnClose([this, weak]() {
        auto s = weak.lock();
        if (s) CloseSession(s);
    });
}

void RelayEngine::CloseSession(const std::shared_ptr<Session>& session) {
    if (!session || session->closed) return;
    session->closed = true;

    auto it = rules_.find(session->ruleName);
    if (it != rules_.end()) {
        RuleRuntime& rt = *it->second;
        if (session->client) rt.stats.bytesIn += session->client->BytesIn();
        if (session->upstream) rt.stats.bytesOut += session->upstream->BytesOut();
        if (rt.stats.active > 0) rt.stats.active--;
        rt.sessions.erase(session->id);
    }

    if (session->client) reaper_.Retire(session->client);
    if (session->upstream) reaper_.Retire(session->upstream);
}

void RelayEngine::StartHealthCheck(RuleRuntime& rt) {
    const std::string ruleName = rt.rule.name;
    const int intervalMs = std::max(1, rt.rule.healthIntervalSec) * 1000;
    rt.healthTimer = loop_.RunEvery(intervalMs, [this, ruleName, intervalMs]() {
        auto it = rules_.find(ruleName);
        if (it == rules_.end()) return;
        RuleRuntime& ruleRt = *it->second;
        const int64_t now = NowMs();
        for (size_t i = 0; i < ruleRt.targets.size(); ++i) {
            if (ruleRt.targets[i].probing) continue;
            if (now - ruleRt.targets[i].lastCheckMs < intervalMs) continue;
            ruleRt.targets[i].lastCheckMs = now;
            ProbeTarget(ruleName, i);
        }
    });
}

void RelayEngine::ProbeTarget(const std::string& ruleName, size_t index) {
    auto it = rules_.find(ruleName);
    if (it == rules_.end()) return;
    RuleRuntime& rt = *it->second;
    if (index >= rt.rule.targets.size()) return;

    const Endpoint target = rt.rule.targets[index].endpoint;
    rt.targets[index].probing = true;

    TcpConnection::Connect(loop_, target, 3000, [this, ruleName, index, target](TcpConnection::Ptr conn,
                                                                                 const std::string& err) {
        auto rit = rules_.find(ruleName);
        if (rit == rules_.end()) {
            if (conn) conn->CloseNow();
            return;
        }
        RuleRuntime& ruleRt = *rit->second;
        if (index >= ruleRt.targets.size()) {
            if (conn) conn->CloseNow();
            return;
        }
        TargetState& st = ruleRt.targets[index];
        st.probing = false;
        const int64_t now = NowMs();
        st.lastCheckMs = now;
        if (conn) {
            if (!st.up) {
                LOG_INFO("relay", "[" << ruleName << "] 目标恢复: " << target.ToString());
            }
            st.up = true;
            st.lastOkMs = now;
            st.lastError.clear();
            conn->CloseNow();
        } else {
            if (st.up) {
                LOG_WARN("relay", "[" << ruleName << "] 目标不可用: " << target.ToString() << " (" << err << ")");
            }
            st.up = false;
            st.lastError = err;
        }
    });
}

Json RelayEngine::SnapshotRule(const std::string& name) const {
    auto it = rules_.find(name);
    if (it == rules_.end()) return Json();
    const RuleRuntime& rt = *it->second;

    Json j = Json::MakeObject();
    j["name"] = rt.rule.name;
    j["listen"] = rt.rule.listen.ToString();
    j["listenPort"] = static_cast<int>(rt.listener ? rt.listener->LocalPort() : rt.rule.listen.port);
    j["strategy"] = RelayStrategyName(rt.rule.strategy);
    j["healthCheck"] = rt.rule.healthCheck;
    j["active"] = static_cast<uint64_t>(rt.stats.active);
    j["connections"] = static_cast<uint64_t>(rt.stats.connections);
    j["failed"] = static_cast<uint64_t>(rt.stats.failed);
    j["bytesIn"] = static_cast<uint64_t>(rt.stats.bytesIn);
    j["bytesOut"] = static_cast<uint64_t>(rt.stats.bytesOut);

    Json targets = Json::MakeArray();
    for (size_t i = 0; i < rt.rule.targets.size(); ++i) {
        Json t = Json::MakeObject();
        t["endpoint"] = rt.rule.targets[i].endpoint.ToString();
        t["up"] = rt.targets[i].up;
        t["active"] = static_cast<uint64_t>(rt.targets[i].active);
        if (!rt.targets[i].lastError.empty()) t["lastError"] = rt.targets[i].lastError;
        targets.Push(t);
    }
    j["targets"] = targets;
    return j;
}

Json RelayEngine::Snapshot() const {
    Json arr = Json::MakeArray();
    for (const auto& kv : rules_) {
        arr.Push(SnapshotRule(kv.first));
    }
    return arr;
}

}  // namespace orsend
