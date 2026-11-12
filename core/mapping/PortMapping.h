// 端口映射：一个本地监听端口 -> 一组目标（负载均衡 + 健康检查 + 故障转移）
#pragma once
#ifndef ORSEND_PORTMAPPING_H
#define ORSEND_PORTMAPPING_H

#include <map>
#include <string>
#include <vector>

#include "Common.h"
#include "Json.h"
#include "RelayEngine.h"

namespace orsend {

struct MappingRule {
    std::string name;
    Endpoint listen;
    std::vector<Endpoint> targets;
    RelayStrategy strategy = RelayStrategy::RoundRobin;
    bool healthCheck = true;
    int healthIntervalSec = 10;
    int connectTimeoutMs = 5000;
    int maxRetry = 3;
};

class PortMapping {
public:
    PortMapping(EventLoop& loop, RelayEngine& engine);

    bool AddRule(const MappingRule& rule, std::string* error);
    bool RemoveRule(const std::string& name);
    bool HasRule(const std::string& name) const;
    const MappingRule* GetRule(const std::string& name) const;
    std::vector<std::string> RuleNames() const;
    size_t RuleCount() const { return rules_.size(); }

    size_t LoadFromJson(const Json& array, std::vector<std::string>* errors);
    Json Snapshot() const;

private:
    EventLoop& loop_;
    RelayEngine& engine_;
    std::map<std::string, MappingRule> rules_;
};

}  // namespace orsend

#endif  // ORSEND_PORTMAPPING_H
