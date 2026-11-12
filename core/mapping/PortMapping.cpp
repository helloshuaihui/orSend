#include "PortMapping.h"

#include "Logger.h"

namespace orsend {

PortMapping::PortMapping(EventLoop& loop, RelayEngine& engine) : loop_(loop), engine_(engine) {}

bool PortMapping::AddRule(const MappingRule& rule, std::string* error) {
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
    if (rule.targets.empty()) {
        if (error) *error = "目标列表为空";
        return false;
    }

    RelayRule rr;
    rr.name = rule.name;
    rr.listen = rule.listen;
    rr.strategy = rule.strategy;
    rr.healthCheck = rule.healthCheck;
    rr.healthIntervalSec = rule.healthIntervalSec;
    rr.connectTimeoutMs = rule.connectTimeoutMs;
    rr.maxRetry = rule.maxRetry;
    for (const auto& t : rule.targets) {
        RelayTarget rt;
        rt.endpoint = t;
        rr.targets.push_back(rt);
    }

    if (!engine_.AddRule(rr, error)) return false;
    rules_[rule.name] = rule;
    return true;
}

bool PortMapping::RemoveRule(const std::string& name) {
    auto it = rules_.find(name);
    if (it == rules_.end()) return false;
    engine_.RemoveRule(name);
    rules_.erase(it);
    return true;
}

bool PortMapping::HasRule(const std::string& name) const { return rules_.find(name) != rules_.end(); }

const MappingRule* PortMapping::GetRule(const std::string& name) const {
    auto it = rules_.find(name);
    if (it == rules_.end()) return nullptr;
    return &it->second;
}

std::vector<std::string> PortMapping::RuleNames() const {
    std::vector<std::string> out;
    for (const auto& kv : rules_) out.push_back(kv.first);
    return out;
}

size_t PortMapping::LoadFromJson(const Json& array, std::vector<std::string>* errors) {
    size_t ok = 0;
    if (!array.IsArray()) return 0;
    int index = 0;
    for (const auto& item : array.Elements()) {
        ++index;
        MappingRule rule;

        std::string listenText = item["listen"].AsString();
        if (!listenText.empty()) {
            ParseEndpoint(listenText, rule.listen);
        } else {
            rule.listen.host = item["listenHost"].AsString("0.0.0.0");
            rule.listen.port = static_cast<uint16_t>(item["listenPort"].AsInt(0));
        }

        // targets: ["127.0.0.1:80", "127.0.0.1:81"]
        const Json& targets = item["targets"];
        if (targets.IsArray()) {
            for (const auto& t : targets.Elements()) {
                Endpoint ep;
                if (ParseEndpoint(t.AsString(), ep) && ep.Valid()) rule.targets.push_back(ep);
            }
        }
        // 兼容单个 target 写法
        if (rule.targets.empty()) {
            Endpoint ep;
            if (ParseEndpoint(item["target"].AsString(), ep) && ep.Valid()) rule.targets.push_back(ep);
        }

        rule.name = item["name"].AsString();
        if (rule.name.empty()) rule.name = "mapping-" + std::to_string(index);
        rule.strategy = ParseRelayStrategy(item["strategy"].AsString("roundrobin"));
        rule.healthCheck = item["healthCheck"].AsBool(true);
        rule.healthIntervalSec = item["healthIntervalSec"].AsInt(10);
        rule.connectTimeoutMs = item["connectTimeoutMs"].AsInt(5000);
        rule.maxRetry = item["maxRetry"].AsInt(3);

        std::string err;
        if (AddRule(rule, &err)) {
            ++ok;
        } else if (errors) {
            errors->push_back("mapping[" + rule.name + "]: " + err);
        }
    }
    return ok;
}

Json PortMapping::Snapshot() const {
    Json arr = Json::MakeArray();
    for (const auto& kv : rules_) {
        Json j = engine_.SnapshotRule(kv.first);
        if (j.IsNull()) {
            j = Json::MakeObject();
            j["name"] = kv.first;
            j["listen"] = kv.second.listen.ToString();
        }
        j["type"] = "mapping";
        arr.Push(j);
    }
    return arr;
}

}  // namespace orsend
