// 应用编排：按配置装配 转发 / 映射 / 内网穿透服务端 / 内网穿透客户端 / 控制接口
#pragma once
#ifndef ORSEND_APP_H
#define ORSEND_APP_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "Common.h"
#include "EventLoop.h"
#include "Json.h"
#include "PortForward.h"
#include "PortMapping.h"
#include "RelayEngine.h"
#include "TunnelClient.h"
#include "TunnelServer.h"

namespace orsend {

class ControlServer;

class OrSendApp {
public:
    static const char* Version();

    OrSendApp();
    ~OrSendApp();

    OrSendApp(const OrSendApp&) = delete;
    OrSendApp& operator=(const OrSendApp&) = delete;

    bool LoadConfigFile(const std::string& path, std::string* error);
    void SetConfig(const Json& config) { config_ = config; }
    const Json& Config() const { return config_; }
    const std::string& ConfigPath() const { return configPath_; }

    // 装配并启动（事件循环在内部线程运行）
    bool Start(std::string* error);
    void Stop();
    void Wait();
    bool Running() const { return running_.load(); }

    Json Status() const;
    EventLoop& Loop() { return loop_; }

    // 控制接口命令分发（在 loop 线程执行）
    bool HandleCommand(const std::string& cmd, const Json& args, Json& data, std::string& error);

    static Json DefaultConfig();

private:
    bool ApplyConfig(std::string* error);
    bool StartServerSection(const Json& cfg, std::string* error);
    bool StartClientSection(const Json& cfg, std::string* error);
    bool StartControlSection(const Json& cfg, std::string* error);
    void ShutdownComponents();
    Json BuildTotals() const;

    EventLoop loop_;
    std::unique_ptr<RelayEngine> relay_;
    std::unique_ptr<PortForward> forward_;
    std::unique_ptr<PortMapping> mapping_;
    std::unique_ptr<TunnelServer> server_;
    std::unique_ptr<TunnelClient> client_;
    std::unique_ptr<ControlServer> control_;

    std::thread loopThread_;
    std::thread::id loopThreadId_;
    std::atomic<bool> running_{false};

    mutable std::mutex waitMutex_;
    std::condition_variable waitCv_;

    Json config_;
    std::string configPath_;
    int64_t startedAt_ = 0;
};

}  // namespace orsend

#endif  // ORSEND_APP_H
