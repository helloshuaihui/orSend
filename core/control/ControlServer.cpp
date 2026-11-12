#include "ControlServer.h"

#include <algorithm>

#include "Logger.h"

namespace orsend {

namespace {
constexpr size_t kMaxLineLength = 4 * 1024 * 1024;
}

ControlServer::ControlServer(EventLoop& loop, Handler handler) : loop_(loop), handler_(std::move(handler)) {}

ControlServer::~ControlServer() { Stop(); }

bool ControlServer::Start(const Endpoint& endpoint, std::string* error) {
    if (listener_) {
        if (error) *error = "控制接口已在运行";
        return false;
    }
    Endpoint ep = endpoint;
    if (ep.host.empty()) ep.host = "127.0.0.1";

    std::string err;
    listener_ = TcpListener::Create(
        loop_, ep, [this](socket_t fd, const Endpoint& peer) { HandleAccept(fd, peer); }, &err);
    if (!listener_) {
        if (error) *error = err;
        return false;
    }
    port_ = listener_->LocalPort();
    LOG_INFO("control", "控制接口监听 127.0.0.1:" << port_);
    return true;
}

void ControlServer::Stop() {
    if (!listener_) return;
    listener_->Close();
    listener_.reset();

    std::vector<uint64_t> ids;
    for (const auto& kv : clients_) ids.push_back(kv.first);
    for (uint64_t id : ids) {
        auto it = clients_.find(id);
        if (it != clients_.end() && it->second.conn) it->second.conn->CloseNow();
    }
    clients_.clear();
    LOG_INFO("control", "控制接口已停止");
}

void ControlServer::HandleAccept(socket_t fd, const Endpoint& peer) {
    const uint64_t id = nextClientId_++;
    Client client;
    client.conn = std::make_shared<TcpConnection>(loop_, fd, peer);

    client.conn->SetOnData([this, id](const char* data, size_t len) { HandleData(id, data, len); });
    client.conn->SetOnClose([this, id]() { clients_.erase(id); });

    clients_[id] = std::move(client);
    clients_[id].conn->Start();
    LOG_DEBUG("control", "控制接口客户端接入 #" << id << " 来自 " << peer.ToString());
}

void ControlServer::HandleData(uint64_t clientId, const char* data, size_t len) {
    auto it = clients_.find(clientId);
    if (it == clients_.end()) return;
    std::string& buf = it->second.buffer;
    buf.append(data, len);

    if (buf.size() > kMaxLineLength) {
        LOG_WARN("control", "控制接口请求过大，断开连接 #" << clientId);
        if (it->second.conn) it->second.conn->CloseNow();
        clients_.erase(it);
        return;
    }

    size_t pos = 0;
    while (true) {
        size_t nl = buf.find('\n', pos);
        if (nl == std::string::npos) break;
        std::string line = buf.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) HandleLine(clientId, line);
        if (clients_.find(clientId) == clients_.end()) return;
    }

    if (pos > 0) buf.erase(0, pos);
}

void ControlServer::HandleLine(uint64_t clientId, const std::string& line) {
    auto it = clients_.find(clientId);
    if (it == clients_.end()) return;

    Json req;
    std::string parseErr;
    Json resp = Json::MakeObject();

    if (!Json::Parse(line, req, &parseErr)) {
        resp["id"] = 0;
        resp["ok"] = false;
        resp["error"] = "请求 JSON 解析失败: " + parseErr;
        if (it->second.conn) it->second.conn->Send(resp.Dump() + "\n");
        return;
    }

    const std::string cmd = req["cmd"].AsString();
    const Json& args = req["args"];
    resp["id"] = req["id"];

    if (cmd.empty()) {
        resp["ok"] = false;
        resp["error"] = "缺少 cmd 字段";
    } else {
        Json data;
        std::string error;
        bool ok = false;
        try {
            ok = handler_ ? handler_(cmd, args, data, error) : false;
            if (!handler_) error = "控制接口未就绪";
        } catch (const std::exception& e) {
            ok = false;
            error = std::string("处理异常: ") + e.what();
        } catch (...) {
            ok = false;
            error = "处理异常";
        }
        resp["ok"] = ok;
        if (ok) {
            resp["data"] = data;
        } else {
            resp["error"] = error.empty() ? "未知错误" : error;
        }
    }

    auto cur = clients_.find(clientId);
    if (cur == clients_.end() || !cur->second.conn) return;
    cur->second.conn->Send(resp.Dump() + "\n");
}

}  // namespace orsend
