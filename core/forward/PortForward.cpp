#include "PortForward.h"

#include "Logger.h"

namespace orsend {

PortForward::PortForward(EventLoop& loop, RelayEngine& engine) : loop_(loop), engine_(engine) {}

bool PortForward::AddRule(const ForwardRule& rule, std::string* error) {
    if (rule.name.empty()) {
        if (error) *error = "规则名不能为空";
        return false;
    }
    if (rules_.find(rule.name) != rules_.end() || engine_.HasRule(rule.name)) {
        if (error) *error = "规则名已被占用: " + rule.name;
        return false;
    }
    if (rule.listen.port == 0) {
        if (error) *error = "监听端口不能为 0";
        return false;
    }
    if (!rule.target.Valid()) {
        if (error) *error = "目标地址无效";
        return false;
    }

    RelayRule rr;
    rr.name = rule.name;
    rr.listen = rule.listen;
    RelayTarget t;
    t.endpoint = rule.target;
    rr.targets.push_back(t);
    rr.strategy = RelayStrategy::Failover;
    rr.maxRetry = 1;
    rr.healthCheck = false;

    if (!engine_.AddRule(rr, error)) return false;
    rules_[rule.name] = rule;
    return true;
}

bool PortForward::RemoveRule(const std::string& name) {
    auto it = rules_.find(name);
    if (it == rules_.end()) return false;
    engine_.RemoveRule(name);
    rules_.erase(it);
    return true;
}

bool PortForward::HasRule(const std::string& name) const { return rules_.find(name) != rules_.end(); }

const ForwardRule* PortForward::GetRule(const std::string& name) const {
    auto it = rules_.find(name);
    if (it == rules_.end()) return nullptr;
    return &it->second;
}

std::vector<std::string> PortForward::RuleNames() const {
    std::vector<std::string> out;
    for (const auto& kv : rules_) out.push_back(kv.first);
    return out;
}

size_t PortForward::LoadFromJson(const Json& array, std::vector<std::string>* errors) {
    size_t ok = 0;
    if (!array.IsArray()) return 0;
    int index = 0;
    for (const auto& item : array.Elements()) {
        ++index;
        ForwardRule rule;

        // 支持 "listen": "0.0.0.0:8080" + "target": "127.0.0.1:80"
        // 也支持 listenHost/listenPort/targetHost/targetPort 拆开写
        std::string listenText = item["listen"].AsString();
        if (!listenText.empty()) {
            ParseEndpoint(listenText, rule.listen);
        } else {
            rule.listen.host = item["listenHost"].AsString("0.0.0.0");
            rule.listen.port = static_cast<uint16_t>(item["listenPort"].AsInt(0));
        }

        std::string targetText = item["target"].AsString();
        if (!targetText.empty()) {
            ParseEndpoint(targetText, rule.target);
        } else {
            rule.target.host = item["targetHost"].AsString("127.0.0.1");
            rule.target.port = static_cast<uint16_t>(item["targetPort"].AsInt(0));
        }

        rule.name = item["name"].AsString();
        if (rule.name.empty()) rule.name = "forward-" + std::to_string(index);

        std::string err;
        if (AddRule(rule, &err)) {
            ++ok;
        } else if (errors) {
            errors->push_back("forward[" + rule.name + "]: " + err);
        }
    }
    return ok;
}

Json PortForward::Snapshot() const {
    Json arr = Json::MakeArray();
    for (const auto& kv : rules_) {
        Json j = engine_.SnapshotRule(kv.first);
        if (j.IsNull()) {
            j = Json::MakeObject();
            j["name"] = kv.first;
            j["listen"] = kv.second.listen.ToString();
        }
        j["type"] = "forward";
        j["target"] = kv.second.target.ToString();
        arr.Push(j);
    }
    return arr;
}

}  // namespace orsend
