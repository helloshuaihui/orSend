#include "TunnelServer.h"

#include <algorithm>

#include "Logger.h"

namespace orsend {

TunnelServer::TunnelServer(EventLoop& loop) : loop_(loop), reaper_(loop) {}

TunnelServer::~TunnelServer() { Stop(); }

void TunnelServer::Emit(const std::string& level, const std::string& msg) {
    if (onEvent) onEvent(level, msg);
}

bool TunnelServer::Start(const Options& options, std::string* error) {
    if (controlListener_) {
        if (error) *error = "服务端已在运行";
        return false;
    }
    options_ = options;
    startedAt_ = NowMs();

    std::string err;
    controlListener_ = TcpListener::Create(
        loop_, options_.bind,
        [this](socket_t fd, const Endpoint& peer) { HandleAgentAccept(fd, peer); }, &err);
    if (!controlListener_) {
        if (error) *error = "监听 agent 接入端口失败: " + err;
        return false;
    }

    if (options_.token.empty()) {
        LOG_WARN("tunnel", "未配置 token，任何 agent 都可以接入，请仅在测试环境使用");
    }

    const int checkMs = 15000;
    watchdogTimer_ = loop_.RunEvery(checkMs, [this]() {
        const int64_t now = NowMs();
        std::vector<uint64_t> timeout;
        for (const auto& kv : agents_) {
            if (now - kv.second->lastSeenMs > static_cast<int64_t>(options_.heartbeatTimeoutSec) * 1000) {
                timeout.push_back(kv.first);
            }
        }
        for (uint64_t id : timeout) {
            LOG_WARN("tunnel", "agent #" << id << " 心跳超时，断开连接");
            CloseAgent(id, "heartbeat timeout");
        }
    });

    LOG_INFO("tunnel", "内网穿透服务端已启动，agent 接入地址 " << options_.bind.ToString()
                                                              << "，公网监听 " << options_.publicHost);
    return true;
}

void TunnelServer::Stop() {
    if (watchdogTimer_ != 0) {
        loop_.CancelTimer(watchdogTimer_);
        watchdogTimer_ = 0;
    }
    if (controlListener_) {
        controlListener_->Close();
        controlListener_.reset();
    }

    std::vector<uint64_t> ids;
    for (const auto& kv : agents_) ids.push_back(kv.first);
    for (uint64_t id : ids) CloseAgent(id, "server stopped");

    tunnels_.clear();
    streams_.clear();
    reaper_.Clear();
    LOG_INFO("tunnel", "内网穿透服务端已停止");
}

void TunnelServer::HandleAgentAccept(socket_t fd, const Endpoint& peer) {
    auto session = std::make_shared<AgentSession>();
    session->id = nextAgentId_++;
    session->connectedAt = NowMs();
    session->lastSeenMs = session->connectedAt;
    session->remoteAddr = peer.ToString();
    session->conn = std::make_shared<TcpConnection>(loop_, fd, peer);

    const uint64_t agentId = session->id;
    std::weak_ptr<AgentSession> weakSession = session;

    session->conn->SetOnData([this, agentId](const char* data, size_t len) {
        HandleAgentData(agentId, data, len);
    });
    session->conn->SetOnClose([this, agentId]() { HandleAgentClose(agentId); });

    agents_[agentId] = session;
    session->conn->Start();

    LOG_INFO("tunnel", "agent 接入 #" << agentId << " 来自 " << peer.ToString() << "，等待认证");
}

void TunnelServer::HandleAgentData(uint64_t agentId, const char* data, size_t len) {
    auto it = agents_.find(agentId);
    if (it == agents_.end()) return;
    auto session = it->second;
    session->lastSeenMs = NowMs();
    session->bytesIn += len;

    if (!session->decoder.Feed(data, len)) {
        LOG_WARN("tunnel", "agent #" << agentId << " 协议解析失败，断开连接");
        CloseAgent(agentId, "protocol error");
        return;
    }

    proto::Frame frame;
    while (session->decoder.Next(frame)) {
        // 回调中可能关闭会话，每轮重新确认
        auto cur = agents_.find(agentId);
        if (cur == agents_.end()) return;
        HandleFrame(cur->second, frame);
    }
}

void TunnelServer::HandleFrame(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame) {
    switch (frame.Type()) {
        case proto::MsgType::Hello: HandleHello(session, frame); break;
        case proto::MsgType::Register: HandleRegister(session, frame); break;
        case proto::MsgType::Unregister: HandleUnregister(session, frame); break;
        case proto::MsgType::TunnelOpenAck: HandleOpenAck(session, frame); break;
        case proto::MsgType::Data: HandleDataFrame(session, frame); break;
        case proto::MsgType::Close: HandleCloseFrame(session, frame); break;
        case proto::MsgType::Ping: {
            Json pong = Json::MakeObject();
            pong["time"] = static_cast<int64_t>(NowMs());
            SendFrame(session->id, proto::MsgType::Pong, 0, pong.Dump());
            break;
        }
        default:
            LOG_DEBUG("tunnel", "忽略未知帧类型: " << proto::MsgTypeName(frame.Type()));
            break;
    }
}

void TunnelServer::HandleHello(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame) {
    Json req;
    std::string parseErr;
    if (!Json::Parse(frame.payload, req, &parseErr)) {
        Json ack = Json::MakeObject();
        ack["ok"] = false;
        ack["message"] = "握手数据格式错误: " + parseErr;
        SendFrame(session->id, proto::MsgType::HelloAck, 0, ack.Dump());
        CloseAgent(session->id, "bad hello");
        return;
    }

    const std::string token = req["token"].AsString();
    if (!options_.token.empty() && token != options_.token) {
        Json ack = Json::MakeObject();
        ack["ok"] = false;
        ack["message"] = "token 校验失败";
        SendFrame(session->id, proto::MsgType::HelloAck, 0, ack.Dump());
        LOG_WARN("tunnel", "agent #" << session->id << " token 校验失败，拒绝接入");
        CloseAgent(session->id, "auth failed");
        return;
    }

    session->clientId = req["clientId"].AsString();
    if (session->clientId.empty()) session->clientId = "agent-" + std::to_string(session->id);
    session->name = req["name"].AsString(session->clientId);
    session->version = req["version"].AsString("unknown");
    session->authed = true;

    // 同一个 clientId 重复上线时踢掉旧会话
    for (const auto& kv : agents_) {
        if (kv.first != session->id && kv.second->clientId == session->clientId && kv.second->authed) {
            LOG_INFO("tunnel", "clientId " << session->clientId << " 重新上线，断开旧会话 #" << kv.first);
            loop_.Post([this, id = kv.first]() { CloseAgent(id, "replaced by new session"); });
        }
    }

    Json ack = Json::MakeObject();
    ack["ok"] = true;
    ack["sessionId"] = static_cast<uint64_t>(session->id);
    ack["heartbeatSec"] = options_.heartbeatTimeoutSec / 3;
    ack["serverVersion"] = "1.0.0";
    SendFrame(session->id, proto::MsgType::HelloAck, 0, ack.Dump());

    LOG_INFO("tunnel", "agent 认证通过: " << session->clientId << " (" << session->name << ") v"
                                          << session->version);
}

void TunnelServer::HandleRegister(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame) {
    Json req;
    std::string parseErr;
    Json ack = Json::MakeObject();

    if (!session->authed) {
        ack["ok"] = false;
        ack["message"] = "尚未完成认证";
        SendFrame(session->id, proto::MsgType::RegisterAck, 0, ack.Dump());
        return;
    }

    if (!Json::Parse(frame.payload, req, &parseErr)) {
        ack["ok"] = false;
        ack["message"] = "注册数据格式错误: " + parseErr;
        SendFrame(session->id, proto::MsgType::RegisterAck, 0, ack.Dump());
        return;
    }

    TunnelDef def;
    def.name = req["name"].AsString();
    def.local.host = req["localHost"].AsString("127.0.0.1");
    def.local.port = static_cast<uint16_t>(req["localPort"].AsInt(0));
    def.remotePort = static_cast<uint16_t>(req["remotePort"].AsInt(0));

    ack["name"] = def.name;

    if (def.name.empty() || def.name.find('/') != std::string::npos) {
        ack["ok"] = false;
        ack["message"] = "隧道名非法";
        SendFrame(session->id, proto::MsgType::RegisterAck, 0, ack.Dump());
        return;
    }
    if (def.local.port == 0) {
        ack["ok"] = false;
        ack["message"] = "本地端口不能为 0";
        SendFrame(session->id, proto::MsgType::RegisterAck, 0, ack.Dump());
        return;
    }

    // 每个 agent 的隧道数量限制
    size_t owned = 0;
    for (const auto& kv : tunnels_) {
        if (kv.second->agentId == session->id) ++owned;
    }
    if (owned >= static_cast<size_t>(options_.maxTunnelsPerAgent)) {
        ack["ok"] = false;
        ack["message"] = "隧道数量超过上限";
        SendFrame(session->id, proto::MsgType::RegisterAck, 0, ack.Dump());
        return;
    }

    const std::string key = session->clientId + "/" + def.name;
    auto existing = tunnels_.find(key);
    if (existing != tunnels_.end()) {
        if (existing->second->agentId != session->id) {
            ack["ok"] = false;
            ack["message"] = "该隧道名已被其他客户端占用";
            SendFrame(session->id, proto::MsgType::RegisterAck, 0, ack.Dump());
            return;
        }
        // 同一个 agent 重新注册（重连场景）：先关闭旧的监听
        if (existing->second->listener) existing->second->listener->Close();
        tunnels_.erase(existing);
    }

    Endpoint listenEp(options_.publicHost, def.remotePort);
    std::string err;
    auto tunnel = std::make_shared<TunnelRuntime>();
    tunnel->def = def;
    tunnel->agentId = session->id;
    tunnel->clientId = session->clientId;
    tunnel->key = key;
    tunnel->createdAt = NowMs();

    tunnel->listener = TcpListener::Create(
        loop_, listenEp,
        [this, key](socket_t fd, const Endpoint& peer) { HandlePublicAccept(key, fd, peer); }, &err);
    if (!tunnel->listener) {
        ack["ok"] = false;
        ack["message"] = err;
        SendFrame(session->id, proto::MsgType::RegisterAck, 0, ack.Dump());
        LOG_WARN("tunnel", "隧道 [" << key << "] 监听失败: " << err);
        return;
    }

    tunnel->actualPort = tunnel->listener->LocalPort();
    tunnels_[key] = tunnel;

    ack["ok"] = true;
    ack["remotePort"] = static_cast<int>(tunnel->actualPort);
    ack["publicHost"] = options_.publicHost;
    SendFrame(session->id, proto::MsgType::RegisterAck, 0, ack.Dump());

    LOG_INFO("tunnel", "隧道已注册 [" << key << "] 公网 " << options_.publicHost << ":"
                                      << tunnel->actualPort << " -> 内网 " << def.local.ToString());
}

void TunnelServer::HandleUnregister(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame) {
    Json req;
    if (!Json::Parse(frame.payload, req, nullptr)) return;
    const std::string key = session->clientId + "/" + req["name"].AsString();
    CloseTunnel(key, "unregistered by agent");
}

void TunnelServer::HandleOpenAck(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame) {
    auto it = streams_.find(frame.streamId);
    if (it == streams_.end()) return;
    auto stream = it->second;

    Json req;
    Json::Parse(frame.payload, req, nullptr);
    const bool ok = req["ok"].AsBool(false);

    if (!ok) {
        const std::string msg = req["message"].AsString("agent 无法连接本地目标");
        LOG_WARN("tunnel", "会话 #" << stream->id << " 打开失败: " << msg);
        CloseStream(stream->id, msg, true);
        return;
    }

    stream->acked = true;
    if (stream->publicConn && !stream->publicConn->Closing()) {
        std::weak_ptr<Stream> weakStream = stream;
        const uint64_t agentId = stream->agentId;
        stream->publicConn->SetOnData([this, weakStream, agentId](const char* data, size_t len) {
            auto s = weakStream.lock();
            if (!s || s->closed) return;
            s->bytesIn += len;
            SendFrame(agentId, proto::MsgType::Data, s->id, std::string(data, len));
        });
        stream->publicConn->Start();
    }

    LOG_DEBUG("tunnel", "会话 #" << stream->id << " 已建立 (" << stream->tunnelKey << ")");
}

void TunnelServer::HandleDataFrame(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame) {
    auto it = streams_.find(frame.streamId);
    if (it == streams_.end()) return;
    auto stream = it->second;
    if (stream->closed || !stream->publicConn) return;
    stream->bytesOut += frame.payload.size();
    stream->publicConn->Send(frame.payload.data(), frame.payload.size());
}

void TunnelServer::HandleCloseFrame(const std::shared_ptr<AgentSession>& session, const proto::Frame& frame) {
    Json req;
    std::string reason = "agent closed";
    if (Json::Parse(frame.payload, req, nullptr)) reason = req["reason"].AsString(reason);
    CloseStream(frame.streamId, reason, false);
}

void TunnelServer::HandlePublicAccept(const std::string& tunnelKey, socket_t fd, const Endpoint& peer) {
    auto it = tunnels_.find(tunnelKey);
    if (it == tunnels_.end()) {
        CloseSocket(fd);
        return;
    }
    auto tunnel = it->second;

    auto agentIt = agents_.find(tunnel->agentId);
    if (agentIt == agents_.end() || !agentIt->second->authed) {
        LOG_WARN("tunnel", "隧道 [" << tunnelKey << "] 对应的 agent 已离线，拒绝连接");
        CloseSocket(fd);
        return;
    }

    auto stream = std::make_shared<Stream>();
    stream->id = AllocStreamId();
    stream->agentId = tunnel->agentId;
    stream->tunnelKey = tunnelKey;
    stream->startMs = NowMs();
    stream->publicConn = std::make_shared<TcpConnection>(loop_, fd, peer);

    streams_[stream->id] = stream;
    tunnel->connections++;
    tunnel->activeStreams++;
    agentIt->second->streamsTotal++;

    Json open = Json::MakeObject();
    open["tunnel"] = tunnel->def.name;
    open["remoteAddr"] = peer.ToString();
    SendFrame(tunnel->agentId, proto::MsgType::TunnelOpen, stream->id, open.Dump());

    // 公网侧暂不开始读取：等 agent 回 ACK 后再 Start，天然形成背压
    std::weak_ptr<Stream> weakStream = stream;
    stream->publicConn->SetOnClose([this, weakStream]() {
        auto s = weakStream.lock();
        if (!s || s->closed) return;
        CloseStream(s->id, "public client closed", true);
    });

    LOG_DEBUG("tunnel", "公网连接进入 [" << tunnelKey << "] 会话 #" << stream->id << " 来自 " << peer.ToString());
}

void TunnelServer::CloseStream(uint32_t streamId, const std::string& reason, bool notifyAgent) {
    auto it = streams_.find(streamId);
    if (it == streams_.end()) return;
    auto stream = it->second;
    if (stream->closed) return;
    stream->closed = true;

    auto tIt = tunnels_.find(stream->tunnelKey);
    if (tIt != tunnels_.end()) {
        auto& t = tIt->second;
        t->bytesIn += stream->bytesIn;
        t->bytesOut += stream->bytesOut;
        if (t->activeStreams > 0) t->activeStreams--;
    }

    if (stream->publicConn) reaper_.Retire(stream->publicConn);

    if (notifyAgent) {
        Json closeMsg = Json::MakeObject();
        closeMsg["reason"] = reason;
        SendFrame(stream->agentId, proto::MsgType::Close, streamId, closeMsg.Dump());
    }

    LOG_DEBUG("tunnel", "会话 #" << streamId << " 关闭 (" << stream->tunnelKey << ") 原因: " << reason
                                 << "，上行 " << stream->bytesIn << " B，下行 " << stream->bytesOut << " B");

    streams_.erase(it);
}

void TunnelServer::CloseTunnel(const std::string& key, const std::string& reason) {
    auto it = tunnels_.find(key);
    if (it == tunnels_.end()) return;

    std::vector<uint32_t> ids;
    for (const auto& kv : streams_) {
        if (kv.second->tunnelKey == key) ids.push_back(kv.first);
    }
    for (uint32_t id : ids) CloseStream(id, reason, true);

    if (it->second->listener) it->second->listener->Close();
    LOG_INFO("tunnel", "隧道已关闭 [" << key << "] 原因: " << reason);
    tunnels_.erase(it);
}

void TunnelServer::CloseAgent(uint64_t agentId, const std::string& reason) {
    auto it = agents_.find(agentId);
    if (it == agents_.end()) return;
    auto session = it->second;

    std::vector<std::string> tunnelKeys;
    for (const auto& kv : tunnels_) {
        if (kv.second->agentId == agentId) tunnelKeys.push_back(kv.first);
    }
    for (const auto& k : tunnelKeys) CloseTunnel(k, reason);

    agents_.erase(it);
    if (session->conn) session->conn->CloseNow();

    LOG_INFO("tunnel", "agent 断开 #" << agentId << " (" << session->clientId << ") 原因: " << reason);
}

void TunnelServer::HandleAgentClose(uint64_t agentId) { CloseAgent(agentId, "connection closed"); }

bool TunnelServer::SendFrame(uint64_t agentId, proto::MsgType type, uint32_t streamId,
                             const std::string& payload) {
    auto it = agents_.find(agentId);
    if (it == agents_.end() || !it->second->conn) return false;
    auto& session = it->second;
    if (!session->conn->Connected()) return false;
    session->conn->Send(proto::Encode(static_cast<uint8_t>(type), streamId, payload));
    session->bytesOut += payload.size();
    return true;
}

uint32_t TunnelServer::AllocStreamId() {
    for (int i = 0; i < 10000; ++i) {
        uint32_t id = nextStreamId_++;
        if (id == 0) continue;
        if (streams_.find(id) == streams_.end()) return id;
    }
    return nextStreamId_++;
}

bool TunnelServer::KickAgent(const std::string& clientId) {
    for (const auto& kv : agents_) {
        if (kv.second->clientId == clientId) {
            CloseAgent(kv.first, "kicked by admin");
            return true;
        }
    }
    return false;
}

Json TunnelServer::SnapshotAgents() const {
    Json arr = Json::MakeArray();
    for (const auto& kv : agents_) {
        const auto& s = kv.second;
        Json j = Json::MakeObject();
        j["id"] = static_cast<uint64_t>(s->id);
        j["clientId"] = s->clientId;
        j["name"] = s->name;
        j["version"] = s->version;
        j["remoteAddr"] = s->remoteAddr;
        j["authed"] = s->authed;
        j["connectedAt"] = s->connectedAt;
        j["lastSeen"] = s->lastSeenMs;
        j["uptimeSec"] = (NowMs() - s->connectedAt) / 1000;
        j["streamsTotal"] = static_cast<uint64_t>(s->streamsTotal);
        j["bytesIn"] = static_cast<uint64_t>(s->bytesIn);
        j["bytesOut"] = static_cast<uint64_t>(s->bytesOut);

        Json names = Json::MakeArray();
        for (const auto& tk : tunnels_) {
            if (tk.second->agentId == s->id) names.Push(tk.second->def.name);
        }
        j["tunnels"] = names;
        arr.Push(j);
    }
    return arr;
}

Json TunnelServer::SnapshotTunnels() const {
    Json arr = Json::MakeArray();
    for (const auto& kv : tunnels_) {
        const auto& t = kv.second;
        Json j = Json::MakeObject();
        j["key"] = t->key;
        j["name"] = t->def.name;
        j["clientId"] = t->clientId;
        j["publicPort"] = static_cast<int>(t->actualPort);
        j["publicHost"] = options_.publicHost;
        j["target"] = t->def.local.ToString();
        j["activeStreams"] = static_cast<uint64_t>(t->activeStreams);
        j["connections"] = static_cast<uint64_t>(t->connections);
        j["bytesIn"] = static_cast<uint64_t>(t->bytesIn);
        j["bytesOut"] = static_cast<uint64_t>(t->bytesOut);
        j["createdAt"] = t->createdAt;
        arr.Push(j);
    }
    return arr;
}

Json TunnelServer::Snapshot() const {
    Json j = Json::MakeObject();
    j["running"] = static_cast<bool>(controlListener_);
    j["listen"] = options_.bind.ToString();
    j["publicHost"] = options_.publicHost;
    j["startedAt"] = startedAt_;
    j["uptimeSec"] = startedAt_ > 0 ? (NowMs() - startedAt_) / 1000 : 0;
    j["agentCount"] = static_cast<uint64_t>(agents_.size());
    j["tunnelCount"] = static_cast<uint64_t>(tunnels_.size());
    j["activeStreams"] = static_cast<uint64_t>(streams_.size());

    uint64_t bytesIn = 0;
    uint64_t bytesOut = 0;
    for (const auto& kv : tunnels_) {
        bytesIn += kv.second->bytesIn;
        bytesOut += kv.second->bytesOut;
    }
    j["bytesIn"] = bytesIn;
    j["bytesOut"] = bytesOut;
    j["agents"] = SnapshotAgents();
    j["tunnels"] = SnapshotTunnels();
    return j;
}

}  // namespace orsend
