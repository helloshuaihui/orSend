// 端口转发：一个本地监听端口 -> 一个固定目标地址（点对点直通）
#pragma once
#ifndef ORSEND_PORTFORWARD_H
#define ORSEND_PORTFORWARD_H

#include <map>
#include <string>
#include <vector>

#include "Common.h"
#include "Json.h"
#include "RelayEngine.h"

namespace orsend {

struct ForwardRule {
    std::string name;
    Endpoint listen;  // 本地监听地址，如 0.0.0.0:8080
    Endpoint target;  // 目标地址，如 127.0.0.1:80
};

class PortForward {
public:
    PortForward(EventLoop& loop, RelayEngine& engine);

    bool AddRule(const ForwardRule& rule, std::string* error);
    bool RemoveRule(const std::string& name);
    bool HasRule(const std::string& name) const;
    const ForwardRule* GetRule(const std::string& name) const;
    std::vector<std::string> RuleNames() const;
    size_t RuleCount() const { return rules_.size(); }

    // 从配置数组加载，返回成功加载的条数
    size_t LoadFromJson(const Json& array, std::vector<std::string>* errors);
    Json Snapshot() const;

private:
    EventLoop& loop_;
    RelayEngine& engine_;
    std::map<std::string, ForwardRule> rules_;
};

}  // namespace orsend

#endif  // ORSEND_PORTFORWARD_H
