#include "TunnelClient.h"

#include <algorithm>

#include "Logger.h"

namespace orsend {

namespace {
constexpr const char* kClientVersion = "1.0.0";
}

TunnelClient::TunnelClient(EventLoop& loop) : loop_(loop), reaper_(loop) {}

TunnelClient::~TunnelClient() { Stop(); }

void TunnelClient::Emit(const std::string& level, const std::string& msg) {
    if (onEvent) onEvent(level, msg);
}

const char* TunnelClient::StateName() const {
    switch (state_) {
        case State::Idle: return "idle";
        case State::Connecting: return "connecting";
        case State::Handshaking: return "handshaking";
        case State::Ready: return "ready";
        case State::Backoff: return "backoff";
    }
    return "idle";
}

void TunnelClient::SetState(State s, const std::string& reason) {
    if (state_ == s) return;
    state_ = s;
    LOG_DEBUG("agent", "状态切换 -> " << StateName() << (reason.empty() ? "" : (" (" + reason + ")")));
}

bool TunnelClient::Start(const Options& options, std::string* error) {
    if (running_) {
        if (error) *error = "客户端已在运行";
        return false;
    }
    if (!options.server.Valid()) {
        if (error) *error = "服务端地址无效";
        return false;
    }
    options_ = options;
    if (options_.clientId.empty()) options_.clientId = "agent-" + std::to_string(RandomU32() % 100000);
    if (options_.name.empty()) options_.name = options_.clientId;
    if (options_.heartbeatSec <= 0) options_.heartbeatSec = 20;
    if (options_.reconnectMinMs <= 0) options_.reconnectMinMs = 1000;
    if (options_.reconnectMaxMs < options_.reconnectMinMs) options_.reconnectMaxMs = 30000;

    tunnels_.clear();
    for (const auto& def : options_.tunnels) {
        TunnelState st;
        st.def = def;
        tunnels_[def.name] = st;
    }

    running_ = true;
    currentBackoffMs_ = 0;
    lastError_.clear();
    SetState(State::Idle, "start");
    DoConnect();
    return true;
}

void TunnelClient::Stop() {
    if (!running_ && !conn_) {
        state_ = State::Idle;
        return;
    }
    running_ = false;

    if (reconnectTimer_ != 0) {
        loop_.CancelTimer(reconnectTimer_);
        reconnectTimer_ = 0;
    }
    StopHeartbeat();
    if (handshakeTimer_ != 0) {
        loop_.CancelTimer(handshakeTimer_);
        handshakeTimer_ = 0;
    }

    std::vector<std::shared_ptr<LocalStream>> streams;
    for (auto& kv : streams_) streams.push_back(kv.second);
    streams_.clear();
    for (auto& s : streams) {
        s->closed = true;
        if (s->local) s->local->CloseNow();
    }

    if (conn_) {
        ++generation_;
        conn_->CloseNow();
        conn_.reset();
    }

    reaper_.Clear();

    for (auto& kv : tunnels_) kv.second.registered = false;
    state_ = State::Idle;
    LOG_INFO("agent", "客户端已停止");
}

void TunnelClient::DoConnect() {
    if (!running_) return;
    SetState(State::Connecting, "connecting " + options_.server.ToString());
    LOG_INFO("agent", "正在连接服务端 " << options_.server.ToString());

    ++generation_;
    const uint64_t gen = generation_;

    TcpConnection::Connect(loop_, options_.server, options_.connectTimeoutMs,
                           [this, gen](TcpConnection::Ptr conn, const std::string& err) {
        if (gen != generation_ || !running_) {
            if (conn) conn->CloseNow();
            return;
        }
        if (!conn) {
            lastError_ = err;
            lastErrorMs_ = NowMs();
            LOG_WARN("agent", "连接服务端失败: " << err);
            Emit("warn", "连接服务端失败: " + err);
            ScheduleReconnect();
            return;
        }
        conn_ = conn;
        decoder_.Reset();
        OnConnected();
    });
}

void TunnelClient::OnConnected() {
    connectedAtMs_ = NowMs();
    lastPongMs_ = connectedAtMs_;
    SetState(State::Handshaking, "handshaking");

    const uint64_t gen = generation_;
    conn_->SetOnData([this, gen](const char* data, size_t len) {
        if (gen != generation_) return;
        HandleData(data, len);
    });
    conn_->SetOnClose([this, gen]() {
        if (gen != generation_) return;
        OnDisconnected("connection closed");
    });
    conn_->Start();

    SendHello();

    if (handshakeTimer_ != 0) loop_.CancelTimer(handshakeTimer_);
    handshakeTimer_ = loop_.RunAfter(10000, [this, gen]() {
        handshakeTimer_ = 0;
        if (gen != generation_ || state_ == State::Ready) return;
        lastError_ = "握手超时";
        LOG_WARN("agent", "与服务端握手超时");
        if (conn_) conn_->CloseNow();
    });
}

void TunnelClient::OnDisconnected(const std::string& reason) {
    StopHeartbeat();
    if (handshakeTimer_ != 0) {
        loop_.CancelTimer(handshakeTimer_);
        handshakeTimer_ = 0;
    }

    // 关闭所有本地流
    std::vector<std::shared_ptr<LocalStream>> streams;
    for (auto& kv : streams_) streams.push_back(kv.second);
    streams_.clear();
    for (auto& s : streams) {
        s->closed = true;
        if (s->local) s->local->CloseNow();
    }

    for (auto& kv : tunnels_) kv.second.registered = false;

    if (conn_) {
        conn_.reset();
    }

    if (!running_) {
        state_ = State::Idle;
        return;
    }

    lastError_ = reason;
    lastErrorMs_ = NowMs();
    LOG_WARN("agent", "与服务端连接断开: " << reason);
    ScheduleReconnect();
}

void TunnelClient::ScheduleReconnect() {
    if (!running_) return;
    if (reconnectTimer_ != 0) {
        loop_.CancelTimer(reconnectTimer_);
        reconnectTimer_ = 0;
    }
    if (currentBackoffMs_ <= 0) {
        currentBackoffMs_ = options_.reconnectMinMs;
    } else {
        currentBackoffMs_ = std::min(currentBackoffMs_ * 2, options_.reconnectMaxMs);
    }
    SetState(State::Backoff, "retry in " + std::to_string(currentBackoffMs_) + "ms");
    LOG_INFO("agent", currentBackoffMs_ << "ms 后重连");

    reconnectTimer_ = loop_.RunAfter(currentBackoffMs_, [this]() {
        reconnectTimer_ = 0;
        DoConnect();
    });
}

void TunnelClient::SendHello() {
    Json hello = Json::MakeObject();
    hello["clientId"] = options_.clientId;
    hello["name"] = options_.name;
    hello["token"] = options_.token;
    hello["version"] = kClientVersion;
    SendFrame(proto::MsgType::Hello, 0, hello.Dump());
}

void TunnelClient::RegisterAll() {
    for (const auto& kv : tunnels_) {
        if (!kv.second.def.enabled) continue;
        SendRegister(kv.first);
    }
}

void TunnelClient::SendRegister(const std::string& tunnelName) {
    auto it = tunnels_.find(tunnelName);
    if (it == tunnels_.end()) return;
    Json req = Json::MakeObject();
    req["name"] = it->second.def.name;
    req["localHost"] = it->second.def.local.host;
    req["localPort"] = static_cast<int>(it->second.def.local.port);
    req["remotePort"] = static_cast<int>(it->second.def.remotePort);
    SendFrame(proto::MsgType::Register, 0, req.Dump());
}

void TunnelClient::SendFrame(proto::MsgType type, uint32_t streamId, const std::string& payload) {
    if (!conn_ || !conn_->Connected()) return;
    conn_->Send(proto::Encode(static_cast<uint8_t>(type), streamId, payload));
}

void TunnelClient::HandleData(const char* data, size_t len) {
    if (!decoder_.Feed(data, len)) {
        LOG_WARN("agent", "协议解析失败，断开连接");
        if (conn_) conn_->CloseNow();
        return;
    }
    proto::Frame frame;
    while (decoder_.Next(frame)) {
        HandleFrame(frame);
        if (!conn_ || !conn_->Connected()) return;
    }
}

void TunnelClient::HandleFrame(const proto::Frame& frame) {
    switch (frame.Type()) {
        case proto::MsgType::HelloAck: HandleHelloAck(frame); break;
        case proto::MsgType::RegisterAck: HandleRegisterAck(frame); break;
        case proto::MsgType::TunnelOpen: HandleTunnelOpen(frame); break;
        case proto::MsgType::Data: HandleDataFrame(frame); break;
        case proto::MsgType::Close: HandleCloseFrame(frame); break;
        case proto::MsgType::Ping: {
            lastPongMs_ = NowMs();
            Json pong = Json::MakeObject();
            pong["time"] = static_cast<int64_t>(NowMs());
            SendFrame(proto::MsgType::Pong, 0, pong.Dump());
            break;
        }
        case proto::MsgType::Pong:
            lastPongMs_ = NowMs();
            break;
        default:
            LOG_DEBUG("agent", "忽略未知帧: " << proto::MsgTypeName(frame.Type()));
            break;
    }
}

void TunnelClient::HandleHelloAck(const proto::Frame& frame) {
    Json ack;
    Json::Parse(frame.payload, ack, nullptr);

    if (!ack["ok"].AsBool(false)) {
        const std::string msg = ack["message"].AsString("认证失败");
        lastError_ = msg;
        lastErrorMs_ = NowMs();
        LOG_ERROR("agent", "服务端拒绝接入: " << msg);
        Emit("error", "服务端拒绝接入: " + msg);
        // 认证失败不重连，避免反复撞墙
        running_ = false;
        if (handshakeTimer_ != 0) {
            loop_.CancelTimer(handshakeTimer_);
            handshakeTimer_ = 0;
        }
        if (conn_) conn_->CloseNow();
        state_ = State::Idle;
        return;
    }

    if (handshakeTimer_ != 0) {
        loop_.CancelTimer(handshakeTimer_);
        handshakeTimer_ = 0;
    }

    currentBackoffMs_ = 0;
    lastError_.clear();
    SetState(State::Ready, "ready");
    LOG_INFO("agent", "已连接服务端 " << options_.server.ToString() << "，clientId=" << options_.clientId);

    RegisterAll();
    StartHeartbeat();
}

void TunnelClient::HandleRegisterAck(const proto::Frame& frame) {
    Json ack;
    Json::Parse(frame.payload, ack, nullptr);
    const std::string name = ack["name"].AsString();
    auto it = tunnels_.find(name);
    if (it == tunnels_.end()) return;

    if (ack["ok"].AsBool(false)) {
        it->second.registered = true;
        it->second.publicPort = static_cast<uint16_t>(ack["remotePort"].AsInt(0));
        it->second.lastError.clear();
        LOG_INFO("agent", "隧道 [" << name << "] 注册成功，公网端口 " << it->second.publicPort);
    } else {
        it->second.registered = false;
        it->second.lastError = ack["message"].AsString("注册失败");
        LOG_WARN("agent", "隧道 [" << name << "] 注册失败: " << it->second.lastError);
    }
}

void TunnelClient::HandleTunnelOpen(const proto::Frame& frame) {
    Json req;
    Json::Parse(frame.payload, req, nullptr);
    const std::string tunnelName = req["tunnel"].AsString();
    const std::string remoteAddr = req["remoteAddr"].AsString();
    const uint32_t streamId = frame.streamId;

    auto it = tunnels_.find(tunnelName);
    if (it == tunnels_.end()) {
        Json ack = Json::MakeObject();
        ack["ok"] = false;
        ack["message"] = "未找到隧道: " + tunnelName;
        SendFrame(proto::MsgType::TunnelOpenAck, streamId, ack.Dump());
        return;
    }

    const Endpoint target = it->second.def.local;
    it->second.connections++;

    auto stream = std::make_shared<LocalStream>();
    stream->id = streamId;
    stream->tunnelName = tunnelName;
    stream->startMs = NowMs();
    // 先入表：拨号回调里用的是 weak_ptr，必须有强引用持有它
    streams_[streamId] = stream;

    LOG_DEBUG("agent", "收到隧道请求 [" << tunnelName << "] 来自 " << remoteAddr << "，拨号 "
                                        << target.ToString());

    std::weak_ptr<LocalStream> weakStream = stream;
    TcpConnection::Connect(loop_, target, 5000, [this, weakStream, streamId, target](
                                                       TcpConnection::Ptr conn, const std::string& err) {
        auto s = weakStream.lock();
        if (!s || s->closed) {
            if (conn) conn->CloseNow();
            streams_.erase(streamId);
            return;
        }
        if (!conn) {
            Json ack = Json::MakeObject();
            ack["ok"] = false;
            ack["message"] = "连接本地目标失败: " + err;
            SendFrame(proto::MsgType::TunnelOpenAck, streamId, ack.Dump());
            LOG_WARN("agent", "连接内网目标 " << target.ToString() << " 失败: " << err);
            streams_.erase(streamId);
            return;
        }

        s->local = conn;

        Json ack = Json::MakeObject();
        ack["ok"] = true;
        SendFrame(proto::MsgType::TunnelOpenAck, streamId, ack.Dump());

        std::weak_ptr<LocalStream> weak = s;
        conn->SetOnData([this, weak, streamId](const char* data, size_t len) {
            auto st = weak.lock();
            if (!st || st->closed) return;
            SendFrame(proto::MsgType::Data, streamId, std::string(data, len));
        });
        conn->SetOnClose([this, weak, streamId]() {
            auto st = weak.lock();
            if (!st || st->closed) return;
            CloseStream(streamId, "local target closed", true);
        });
        conn->Start();
    });
}

void TunnelClient::HandleDataFrame(const proto::Frame& frame) {
    auto it = streams_.find(frame.streamId);
    if (it == streams_.end()) return;
    auto& stream = it->second;
    if (stream->closed || !stream->local) return;
    stream->local->Send(frame.payload.data(), frame.payload.size());
}

void TunnelClient::HandleCloseFrame(const proto::Frame& frame) {
    Json req;
    std::string reason = "server closed";
    if (Json::Parse(frame.payload, req, nullptr)) reason = req["reason"].AsString(reason);
    CloseStream(frame.streamId, reason, false);
}

void TunnelClient::CloseStream(uint32_t id, const std::string& reason, bool notifyServer) {
    auto it = streams_.find(id);
    if (it == streams_.end()) return;
    auto stream = it->second;
    if (stream->closed) return;
    stream->closed = true;

    if (stream->local) reaper_.Retire(stream->local);

    if (notifyServer) {
        Json closeMsg = Json::MakeObject();
        closeMsg["reason"] = reason;
        SendFrame(proto::MsgType::Close, id, closeMsg.Dump());
    }

    LOG_DEBUG("agent", "会话 #" << id << " 关闭 (" << stream->tunnelName << ") 原因: " << reason);

    streams_.erase(it);
}

void TunnelClient::StartHeartbeat() {
    StopHeartbeat();
    const int64_t intervalMs = static_cast<int64_t>(options_.heartbeatSec) * 1000;
    heartbeatTimer_ = loop_.RunEvery(intervalMs, [this, intervalMs]() {
        if (!conn_ || !conn_->Connected()) return;
        const int64_t now = NowMs();
        if (now - lastPongMs_ > intervalMs * 3) {
            LOG_WARN("agent", "心跳超时，主动断开重连");
            lastError_ = "心跳超时";
            if (conn_) conn_->CloseNow();
            return;
        }
        Json ping = Json::MakeObject();
        ping["time"] = static_cast<int64_t>(now);
        SendFrame(proto::MsgType::Ping, 0, ping.Dump());
    });
}

void TunnelClient::StopHeartbeat() {
    if (heartbeatTimer_ != 0) {
        loop_.CancelTimer(heartbeatTimer_);
        heartbeatTimer_ = 0;
    }
}

bool TunnelClient::AddTunnel(const TunnelDef& def, std::string* error) {
    if (def.name.empty()) {
        if (error) *error = "隧道名不能为空";
        return false;
    }
    if (tunnels_.find(def.name) != tunnels_.end()) {
        if (error) *error = "隧道已存在: " + def.name;
        return false;
    }
    if (def.local.port == 0) {
        if (error) *error = "本地端口不能为 0";
        return false;
    }
    TunnelState st;
    st.def = def;
    tunnels_[def.name] = st;
    if (state_ == State::Ready) SendRegister(def.name);
    return true;
}

bool TunnelClient::RemoveTunnel(const std::string& name) {
    auto it = tunnels_.find(name);
    if (it == tunnels_.end()) return false;
    if (state_ == State::Ready) {
        Json req = Json::MakeObject();
        req["name"] = name;
        SendFrame(proto::MsgType::Unregister, 0, req.Dump());
    }
    tunnels_.erase(it);
    return true;
}

Json TunnelClient::Snapshot() const {
    Json j = Json::MakeObject();
    j["running"] = running_;
    j["state"] = StateName();
    j["server"] = options_.server.ToString();
    j["clientId"] = options_.clientId;
    j["name"] = options_.name;
    j["connectedAt"] = connectedAtMs_;
    j["uptimeSec"] = (state_ == State::Ready && connectedAtMs_ > 0) ? (NowMs() - connectedAtMs_) / 1000 : 0;
    j["activeStreams"] = static_cast<uint64_t>(streams_.size());
    j["reconnectBackoffMs"] = currentBackoffMs_;
    if (!lastError_.empty()) j["lastError"] = lastError_;

    Json tunnels = Json::MakeArray();
    for (const auto& kv : tunnels_) {
        const TunnelState& t = kv.second;
        Json tj = Json::MakeObject();
        tj["name"] = t.def.name;
        tj["target"] = t.def.local.ToString();
        tj["remotePort"] = static_cast<int>(t.def.remotePort);
        tj["publicPort"] = static_cast<int>(t.publicPort);
        tj["registered"] = t.registered;
        tj["connections"] = static_cast<uint64_t>(t.connections);
        if (!t.lastError.empty()) tj["lastError"] = t.lastError;
        tunnels.Push(tj);
    }
    j["tunnels"] = tunnels;
    return j;
}

}  // namespace orsend
