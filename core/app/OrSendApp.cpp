#include "OrSendApp.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "ControlServer.h"
#include "Logger.h"

namespace orsend {

const char* OrSendApp::Version() { return "1.0.0"; }

OrSendApp::OrSendApp() { config_ = DefaultConfig(); }

OrSendApp::~OrSendApp() {
    Stop();
    if (loopThread_.joinable()) {
        if (std::this_thread::get_id() != loopThreadId_) {
            loopThread_.join();
        } else {
            loopThread_.detach();
        }
    }
}

Json OrSendApp::DefaultConfig() {
    Json cfg = Json::MakeObject();

    Json log = Json::MakeObject();
    log["level"] = "info";
    log["console"] = true;
    log["file"] = "";
    cfg["log"] = log;

    Json control = Json::MakeObject();
    control["enabled"] = true;
    control["host"] = "127.0.0.1";
    control["port"] = 7400;
    cfg["control"] = control;

    Json server = Json::MakeObject();
    server["enabled"] = false;
    server["bind"] = "0.0.0.0:7000";
    server["token"] = "";
    server["publicHost"] = "0.0.0.0";
    cfg["server"] = server;

    Json client = Json::MakeObject();
    client["enabled"] = false;
    client["server"] = "127.0.0.1:7000";
    client["token"] = "";
    client["clientId"] = "";
    client["name"] = "";
    client["tunnels"] = Json::MakeArray();
    cfg["client"] = client;

    cfg["forward"] = Json::MakeArray();
    cfg["mapping"] = Json::MakeArray();
    return cfg;
}

bool OrSendApp::LoadConfigFile(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "无法打开配置文件: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    Json cfg;
    std::string parseErr;
    if (!Json::Parse(text, cfg, &parseErr)) {
        if (error) *error = "配置文件 JSON 解析失败: " + parseErr;
        return false;
    }
    configPath_ = path;
    // 与默认配置合并，保证缺省字段可用
    Json merged = DefaultConfig();
    for (const auto& kv : cfg.Items()) {
        merged[kv.first] = kv.second;
    }
    config_ = merged;
    return true;
}

bool OrSendApp::Start(std::string* error) {
    if (running_.load()) {
        if (error) *error = "已在运行";
        return false;
    }
    NetRuntime::Init();

    // 日志
    const Json& logCfg = config_["log"];
    Logger::Instance().SetLevel(ParseLogLevel(logCfg["level"].AsString("info")));
    Logger::Instance().SetConsoleEnabled(logCfg["console"].AsBool(true));
    const std::string logFile = logCfg["file"].AsString();
    if (!logFile.empty()) {
        if (!Logger::Instance().OpenFile(logFile)) {
            LOG_WARN("app", "无法写入日志文件: " << logFile);
        }
    }

    LOG_INFO("app", "orSend v" << Version() << " 启动中" << (configPath_.empty() ? "" : ("，配置: " + configPath_)));

    relay_.reset(new RelayEngine(loop_));
    relay_->onEvent = [](const std::string& level, const std::string& msg) {
        if (level == "warn") {
            LOG_WARN("relay", msg);
        } else if (level == "error") {
            LOG_ERROR("relay", msg);
        } else {
            LOG_INFO("relay", msg);
        }
    };
    forward_.reset(new PortForward(loop_, *relay_));
    mapping_.reset(new PortMapping(loop_, *relay_));

    if (!ApplyConfig(error)) return false;

    startedAt_ = NowMs();
    running_.store(true);

    loopThread_ = std::thread([this]() {
        loopThreadId_ = std::this_thread::get_id();
        loop_.Run();
        LOG_DEBUG("app", "事件循环线程结束");
    });

    LOG_INFO("app", "orSend 已启动");
    return true;
}

bool OrSendApp::ApplyConfig(std::string* error) {
    // 端口转发
    std::vector<std::string> errors;
    forward_->LoadFromJson(config_["forward"], &errors);
    for (const auto& e : errors) LOG_WARN("app", e);

    // 端口映射
    errors.clear();
    mapping_->LoadFromJson(config_["mapping"], &errors);
    for (const auto& e : errors) LOG_WARN("app", e);

    // 内网穿透服务端
    if (config_["server"]["enabled"].AsBool(false)) {
        if (!StartServerSection(config_["server"], error)) return false;
    }

    // 内网穿透客户端
    if (config_["client"]["enabled"].AsBool(false)) {
        if (!StartClientSection(config_["client"], error)) return false;
    }

    // 控制接口
    if (config_["control"]["enabled"].AsBool(true)) {
        if (!StartControlSection(config_["control"], error)) {
            LOG_WARN("app", "控制接口启动失败: " << (error ? *error : ""));
        }
    }
    return true;
}

bool OrSendApp::StartServerSection(const Json& cfg, std::string* error) {
    if (!server_) server_.reset(new TunnelServer(loop_));
    if (server_->Running()) {
        if (error) *error = "服务端已在运行";
        return false;
    }
    server_->onEvent = [](const std::string& level, const std::string& msg) {
        if (level == "warn") {
            LOG_WARN("tunnel", msg);
        } else if (level == "error") {
            LOG_ERROR("tunnel", msg);
        }
    };

    TunnelServer::Options opts;
    ParseEndpoint(cfg["bind"].AsString("0.0.0.0:7000"), opts.bind);
    opts.token = cfg["token"].AsString();
    opts.publicHost = cfg["publicHost"].AsString("0.0.0.0");
    opts.heartbeatTimeoutSec = cfg["heartbeatTimeoutSec"].AsInt(90);

    std::string err;
    if (!server_->Start(opts, &err)) {
        if (error) *error = err;
        return false;
    }
    return true;
}

bool OrSendApp::StartClientSection(const Json& cfg, std::string* error) {
    if (!client_) client_.reset(new TunnelClient(loop_));
    if (client_->Running()) {
        if (error) *error = "客户端已在运行";
        return false;
    }
    client_->onEvent = [](const std::string& level, const std::string& msg) {
        if (level == "warn") {
            LOG_WARN("agent", msg);
        } else if (level == "error") {
            LOG_ERROR("agent", msg);
        }
    };

    TunnelClient::Options opts;
    ParseEndpoint(cfg["server"].AsString("127.0.0.1:7000"), opts.server);
    opts.token = cfg["token"].AsString();
    opts.clientId = cfg["clientId"].AsString();
    opts.name = cfg["name"].AsString();
    opts.heartbeatSec = cfg["heartbeatSec"].AsInt(20);
    opts.reconnectMinMs = cfg["reconnectMinMs"].AsInt(1000);
    opts.reconnectMaxMs = cfg["reconnectMaxMs"].AsInt(30000);
    opts.connectTimeoutMs = cfg["connectTimeoutMs"].AsInt(8000);

    const Json& tunnels = cfg["tunnels"];
    if (tunnels.IsArray()) {
        int index = 0;
        for (const auto& item : tunnels.Elements()) {
            ++index;
            TunnelDef def;
            def.name = item["name"].AsString();
            if (def.name.empty()) def.name = "tunnel-" + std::to_string(index);
            def.local.host = item["localHost"].AsString("127.0.0.1");
            def.local.port = static_cast<uint16_t>(item["localPort"].AsInt(0));
            def.remotePort = static_cast<uint16_t>(item["remotePort"].AsInt(0));
            def.enabled = item["enabled"].AsBool(true);
            if (def.local.port == 0) {
                LOG_WARN("app", "隧道 [" << def.name << "] 本地端口为 0，已忽略");
                continue;
            }
            opts.tunnels.push_back(def);
        }
    }

    std::string err;
    if (!client_->Start(opts, &err)) {
        if (error) *error = err;
        return false;
    }
    return true;
}

bool OrSendApp::StartControlSection(const Json& cfg, std::string* error) {
    if (!control_) {
        control_.reset(new ControlServer(loop_, [this](const std::string& cmd, const Json& args, Json& data,
                                                       std::string& err) {
            return HandleCommand(cmd, args, data, err);
        }));
    }
    Endpoint ep;
    ep.host = cfg["host"].AsString("127.0.0.1");
    ep.port = static_cast<uint16_t>(cfg["port"].AsInt(7400));
    return control_->Start(ep, error);
}

void OrSendApp::ShutdownComponents() {
    if (client_) client_->Stop();
    if (server_) server_->Stop();
    if (control_) control_->Stop();
    if (relay_) relay_->StopAll();
}

void OrSendApp::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    LOG_INFO("app", "orSend 正在停止");
    loop_.Post([this]() {
        ShutdownComponents();
        loop_.Stop();
    });
    waitCv_.notify_all();
}

void OrSendApp::Wait() {
    std::unique_lock<std::mutex> lock(waitMutex_);
    waitCv_.wait(lock, [this]() { return !running_.load(); });
}

Json OrSendApp::BuildTotals() const {
    Json t = Json::MakeObject();
    uint64_t rules = 0;
    uint64_t bytesIn = 0;
    uint64_t bytesOut = 0;

    if (forward_) {
        Json arr = forward_->Snapshot();
        rules += arr.Size();
        for (const auto& r : arr.Elements()) {
            bytesIn += r["bytesIn"].AsUInt64();
            bytesOut += r["bytesOut"].AsUInt64();
        }
    }
    if (mapping_) {
        Json arr = mapping_->Snapshot();
        rules += arr.Size();
        for (const auto& r : arr.Elements()) {
            bytesIn += r["bytesIn"].AsUInt64();
            bytesOut += r["bytesOut"].AsUInt64();
        }
    }
    uint64_t tunnels = 0;
    uint64_t agents = 0;
    uint64_t streams = 0;
    if (server_ && server_->Running()) {
        Json s = server_->Snapshot();
        tunnels += s["tunnelCount"].AsUInt64();
        agents += s["agentCount"].AsUInt64();
        streams += s["activeStreams"].AsUInt64();
        bytesIn += s["bytesIn"].AsUInt64();
        bytesOut += s["bytesOut"].AsUInt64();
    }
    if (client_ && client_->Running()) {
        Json c = client_->Snapshot();
        streams += c["activeStreams"].AsUInt64();
        tunnels += c["tunnels"].Size();
    }

    t["rules"] = rules;
    t["tunnels"] = tunnels;
    t["agents"] = agents;
    t["streams"] = streams;
    t["bytesIn"] = bytesIn;
    t["bytesOut"] = bytesOut;
    return t;
}

Json OrSendApp::Status() const {
    Json j = Json::MakeObject();
    j["version"] = Version();
    j["startedAt"] = startedAt_;
    j["uptimeSec"] = startedAt_ > 0 ? (NowMs() - startedAt_) / 1000 : 0;
    j["configPath"] = configPath_;
    j["running"] = running_.load();

    Json control = Json::MakeObject();
    control["running"] = control_ && control_->Running();
    control["port"] = control_ ? static_cast<int>(control_->Port()) : 0;
    control["clients"] = control_ ? static_cast<uint64_t>(control_->ClientCount()) : 0;
    j["control"] = control;

    if (server_) {
        j["server"] = server_->Snapshot();
    } else {
        Json s = Json::MakeObject();
        s["running"] = false;
        j["server"] = s;
    }

    if (client_) {
        j["client"] = client_->Snapshot();
    } else {
        Json c = Json::MakeObject();
        c["running"] = false;
        c["state"] = "idle";
        j["client"] = c;
    }

    j["forward"] = forward_ ? forward_->Snapshot() : Json::MakeArray();
    j["mapping"] = mapping_ ? mapping_->Snapshot() : Json::MakeArray();
    j["totals"] = BuildTotals();
    return j;
}

bool OrSendApp::HandleCommand(const std::string& cmd, const Json& args, Json& data, std::string& error) {
    auto needServer = [&]() -> TunnelServer* {
        if (!server_ || !server_->Running()) {
            error = "服务端未运行";
            return nullptr;
        }
        return server_.get();
    };
    auto needClient = [&]() -> TunnelClient* {
        if (!client_ || !client_->Running()) {
            error = "客户端未运行";
            return nullptr;
        }
        return client_.get();
    };

    if (cmd == "ping") {
        data = Json::MakeObject();
        data["time"] = static_cast<int64_t>(NowMs());
        data["version"] = Version();
        return true;
    }

    if (cmd == "status") {
        data = Status();
        return true;
    }

    if (cmd == "logs") {
        const uint64_t since = args["sinceId"].AsUInt64(0);
        const size_t limit = static_cast<size_t>(args["limit"].AsInt(200));
        auto entries = Logger::Instance().Tail(since, limit);
        Json arr = Json::MakeArray();
        for (const auto& e : entries) {
            Json item = Json::MakeObject();
            item["id"] = e.id;
            item["time"] = e.timeMs;
            item["level"] = LogLevelName(e.level);
            item["tag"] = e.tag;
            item["message"] = e.message;
            arr.Push(item);
        }
        data = Json::MakeObject();
        data["entries"] = arr;
        data["lastId"] = Logger::Instance().LastId();
        return true;
    }

    if (cmd == "shutdown") {
        data = Json::MakeObject();
        data["ok"] = true;
        loop_.Post([this]() { Stop(); });
        return true;
    }

    if (cmd == "server.start") {
        Json cfg = config_["server"];
        if (args.Has("bind")) cfg["bind"] = args["bind"].AsString();
        if (args.Has("token")) cfg["token"] = args["token"].AsString();
        if (args.Has("publicHost")) cfg["publicHost"] = args["publicHost"].AsString();
        config_["server"] = cfg;
        config_["server"]["enabled"] = true;
        if (!StartServerSection(cfg, &error)) return false;
        data = server_->Snapshot();
        return true;
    }

    if (cmd == "server.stop") {
        if (server_) server_->Stop();
        data = Json::MakeObject();
        data["ok"] = true;
        return true;
    }

    if (cmd == "server.agents") {
        if (!needServer()) return false;
        data = server_->SnapshotAgents();
        return true;
    }

    if (cmd == "server.tunnels") {
        if (!needServer()) return false;
        data = server_->SnapshotTunnels();
        return true;
    }

    if (cmd == "server.kick") {
        if (!needServer()) return false;
        const std::string clientId = args["clientId"].AsString();
        if (clientId.empty()) {
            error = "缺少 clientId";
            return false;
        }
        data = Json::MakeObject();
        data["ok"] = server_->KickAgent(clientId);
        if (!data["ok"].AsBool()) error = "未找到该客户端: " + clientId;
        return data["ok"].AsBool();
    }

    if (cmd == "client.start") {
        Json cfg = config_["client"];
        if (args.Has("server")) cfg["server"] = args["server"].AsString();
        if (args.Has("token")) cfg["token"] = args["token"].AsString();
        if (args.Has("clientId")) cfg["clientId"] = args["clientId"].AsString();
        if (args.Has("name")) cfg["name"] = args["name"].AsString();
        if (args.Has("tunnels")) cfg["tunnels"] = args["tunnels"];
        config_["client"] = cfg;
        config_["client"]["enabled"] = true;
        if (!StartClientSection(cfg, &error)) return false;
        data = client_->Snapshot();
        return true;
    }

    if (cmd == "client.stop") {
        if (client_) client_->Stop();
        data = Json::MakeObject();
        data["ok"] = true;
        return true;
    }

    if (cmd == "client.tunnels") {
        if (!client_) {
            data = Json::MakeArray();
            return true;
        }
        data = client_->Snapshot()["tunnels"];
        return true;
    }

    if (cmd == "client.addTunnel") {
        if (!client_) client_.reset(new TunnelClient(loop_));
        TunnelDef def;
        def.name = args["name"].AsString();
        def.local.host = args["localHost"].AsString("127.0.0.1");
        def.local.port = static_cast<uint16_t>(args["localPort"].AsInt(0));
        def.remotePort = static_cast<uint16_t>(args["remotePort"].AsInt(0));
        if (!client_->AddTunnel(def, &error)) return false;
        data = client_->Snapshot()["tunnels"];
        return true;
    }

    if (cmd == "client.removeTunnel") {
        if (!client_) {
            error = "客户端未初始化";
            return false;
        }
        const std::string name = args["name"].AsString();
        if (!client_->RemoveTunnel(name)) {
            error = "隧道不存在: " + name;
            return false;
        }
        data = client_->Snapshot()["tunnels"];
        return true;
    }

    if (cmd == "forward.list") {
        data = forward_ ? forward_->Snapshot() : Json::MakeArray();
        return true;
    }

    if (cmd == "forward.add") {
        ForwardRule rule;
        rule.name = args["name"].AsString();
        if (!ParseEndpoint(args["listen"].AsString(), rule.listen)) {
            error = "listen 格式错误，应为 host:port";
            return false;
        }
        if (!ParseEndpoint(args["target"].AsString(), rule.target)) {
            error = "target 格式错误，应为 host:port";
            return false;
        }
        if (rule.name.empty()) {
            rule.name = "forward-" + std::to_string(rule.listen.port);
        }
        if (!forward_->AddRule(rule, &error)) return false;
        data = forward_->Snapshot();
        return true;
    }

    if (cmd == "forward.remove") {
        if (!forward_ || !forward_->RemoveRule(args["name"].AsString())) {
            error = "规则不存在";
            return false;
        }
        data = forward_->Snapshot();
        return true;
    }

    if (cmd == "mapping.list") {
        data = mapping_ ? mapping_->Snapshot() : Json::MakeArray();
        return true;
    }

    if (cmd == "mapping.add") {
        MappingRule rule;
        rule.name = args["name"].AsString();
        if (!ParseEndpoint(args["listen"].AsString(), rule.listen)) {
            error = "listen 格式错误，应为 host:port";
            return false;
        }
        const Json& targets = args["targets"];
        if (targets.IsArray()) {
            for (const auto& t : targets.Elements()) {
                Endpoint ep;
                if (ParseEndpoint(t.AsString(), ep) && ep.Valid()) rule.targets.push_back(ep);
            }
        } else if (targets.IsString()) {
            for (const auto& part : SplitString(targets.AsString(), ',')) {
                Endpoint ep;
                if (ParseEndpoint(Trim(part), ep) && ep.Valid()) rule.targets.push_back(ep);
            }
        }
        if (rule.targets.empty()) {
            error = "targets 不能为空";
            return false;
        }
        if (rule.name.empty()) {
            rule.name = "mapping-" + std::to_string(rule.listen.port);
        }
        rule.strategy = ParseRelayStrategy(args["strategy"].AsString("roundrobin"));
        rule.healthCheck = args["healthCheck"].AsBool(true);
        if (!mapping_->AddRule(rule, &error)) return false;
        data = mapping_->Snapshot();
        return true;
    }

    if (cmd == "mapping.remove") {
        if (!mapping_ || !mapping_->RemoveRule(args["name"].AsString())) {
            error = "规则不存在";
            return false;
        }
        data = mapping_->Snapshot();
        return true;
    }

    if (cmd == "config.get") {
        data = config_;
        return true;
    }

    error = "未知命令: " + cmd;
    return false;
}

}  // namespace orsend
