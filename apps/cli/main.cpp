// orsend 命令行入口
//
//   orsend run     -c config.json          按配置启动全部功能
//   orsend server  -c config.json          只启动内网穿透服务端
//   orsend client  -c config.json          只启动内网穿透客户端
//   orsend forward --listen 0.0.0.0:8080 --target 127.0.0.1:80
//   orsend map     --listen 0.0.0.0:9000 --targets 127.0.0.1:80,127.0.0.1:81
//   orsend init    [config.json]           生成示例配置
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "../core/app/OrSendApp.h"
#include "../core/common/Logger.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

using namespace orsend;

namespace {

std::atomic<bool> g_stopRequested{false};
OrSendApp* g_app = nullptr;

#ifdef _WIN32
BOOL WINAPI ConsoleCtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_stopRequested.store(true);
        if (g_app) g_app->Stop();
        return TRUE;
    }
    return FALSE;
}
#else
void SignalHandler(int) {
    g_stopRequested.store(true);
    if (g_app) g_app->Stop();
}
#endif

void InstallSignalHandlers() {
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    signal(SIGPIPE, SIG_IGN);
#endif
}

// Windows 控制台默认按 ANSI 代码页传参（中文系统即 GBK），
// 这里统一转成 UTF-8，避免日志 / JSON 里出现乱码或非法字节。
#ifdef _WIN32
bool IsValidUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t extra = 0;
        if (c < 0x80) {
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= s.size() && extra > 0) return false;
        for (size_t k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

std::string AcpToUtf8(const std::string& s) {
    if (s.empty()) return s;
    const int wlen = MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (wlen <= 0) return s;
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), &w[0], wlen);
    const int u8len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return s;
    std::string out(static_cast<size_t>(u8len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wlen, &out[0], u8len, nullptr, nullptr);
    return out;
}

#endif
void PrintBanner() {
    std::cout << "\n";
    std::cout << "  ____        ____                 _ \n";
    std::cout << " / __ \\      / ___|  ___ _ __   __| |\n";
    std::cout << "| |  | |____ \\___ \\ / _ \\ '_ \\ / _` |\n";
    std::cout << "| |__| |____| ___) |  __/ | | | (_| |\n";
    std::cout << " \\____/     |____/ \\___|_| |_|\\__,_|\n";
    std::cout << " 端口转发 / 端口映射 / 内网穿透  v" << OrSendApp::Version() << "\n\n";
}

// 输出被重定向（例如被页面托管）时不要打印 banner，避免污染日志
bool StdoutIsTty() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

void PrintUsage() {
    std::cout << "用法:\n"
              << "  orsend run     -c <config.json>                按配置启动全部功能\n"
              << "  orsend server  -c <config.json> [--bind h:p] [--token t] [--public-host h]\n"
              << "  orsend client  -c <config.json> --server h:p --token t [--tunnel n:localPort:remotePort]...\n"
              << "  orsend forward --listen h:p --target h:p [--name n] [--control-port p]\n"
              << "  orsend map     --listen h:p --targets h:p,h:p [--strategy roundrobin] [--name n]\n"
              << "  orsend init    [config.json]                   生成示例配置\n"
              << "  orsend version\n"
              << "\n常用参数:\n"
              << "  -c, --config <path>      配置文件路径\n"
              << "  --control-port <port>    本地控制接口端口（0 表示不开启，默认 7400）\n"
              << "  --log-level <level>      trace|debug|info|warn|error\n"
              << "  --log-file <path>        日志文件路径\n"
              << std::endl;
}

struct Args {
    std::string command;
    std::map<std::string, std::string> options;
    std::vector<std::string> tunnels;
    std::vector<std::string> positional;

    bool Has(const std::string& key) const { return options.find(key) != options.end(); }
    std::string Get(const std::string& key, const std::string& def = std::string()) const {
        auto it = options.find(key);
        return it == options.end() ? def : it->second;
    }
};

Args ParseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-c" || a == "--config") {
            if (i + 1 < argc) args.options["config"] = argv[++i];
        } else if (a == "--tunnel") {
            if (i + 1 < argc) args.tunnels.push_back(argv[++i]);
        } else if (a.rfind("--", 0) == 0) {
            std::string key = a.substr(2);
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                args.options[key] = argv[++i];
            } else {
                args.options[key] = "true";
            }
        } else if (a == "-h" || a == "--help") {
            args.options["help"] = "true";
        } else if (args.command.empty()) {
            args.command = a;
        } else {
            args.positional.push_back(a);
        }
    }
    return args;
}

bool ParseTunnelSpec(const std::string& spec, TunnelDef& out) {
    // name:localPort:remotePort  或  name:localHost:localPort:remotePort
    auto parts = SplitString(spec, ':');
    if (parts.size() == 3) {
        out.name = parts[0];
        out.local.host = "127.0.0.1";
        out.local.port = static_cast<uint16_t>(std::atoi(parts[1].c_str()));
        out.remotePort = static_cast<uint16_t>(std::atoi(parts[2].c_str()));
        return out.local.port != 0;
    }
    if (parts.size() == 4) {
        out.name = parts[0];
        out.local.host = parts[1];
        out.local.port = static_cast<uint16_t>(std::atoi(parts[2].c_str()));
        out.remotePort = static_cast<uint16_t>(std::atoi(parts[3].c_str()));
        return out.local.port != 0;
    }
    return false;
}

void WriteSampleConfig(const std::string& path) {
    Json cfg = OrSendApp::DefaultConfig();
    cfg["log"]["level"] = "info";
    cfg["log"]["file"] = "logs/orsend.log";

    cfg["server"]["enabled"] = true;
    cfg["server"]["bind"] = "0.0.0.0:7000";
    cfg["server"]["token"] = "change-me-please";
    cfg["server"]["publicHost"] = "0.0.0.0";

    cfg["client"]["enabled"] = false;
    cfg["client"]["server"] = "your-server-ip:7000";
    cfg["client"]["token"] = "change-me-please";
    cfg["client"]["clientId"] = "home-nas";
    cfg["client"]["name"] = "家里的 NAS";

    Json tunnel = Json::MakeObject();
    tunnel["name"] = "web";
    tunnel["localHost"] = "127.0.0.1";
    tunnel["localPort"] = 8080;
    tunnel["remotePort"] = 18080;
    cfg["client"]["tunnels"].Push(tunnel);

    Json fwd = Json::MakeObject();
    fwd["name"] = "local-web";
    fwd["listen"] = "0.0.0.0:8080";
    fwd["target"] = "127.0.0.1:80";

    Json mapRule = Json::MakeObject();
    mapRule["name"] = "web-cluster";
    mapRule["listen"] = "0.0.0.0:9000";
    mapRule["strategy"] = "roundrobin";
    mapRule["healthCheck"] = true;
    Json targets = Json::MakeArray();
    targets.Push("127.0.0.1:8081");
    targets.Push("127.0.0.1:8082");
    mapRule["targets"] = targets;

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "无法写入 " << path << std::endl;
        return;
    }
    out << cfg.Dump(2) << "\n";
    std::cout << "示例配置已生成: " << path << std::endl;
    std::cout << "请修改 server.token / client.server 后再启动。" << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    // 把命令行参数统一成 UTF-8
    std::vector<std::string> argStorage;
    argStorage.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        std::string s = argv[i] ? argv[i] : "";
        argStorage.push_back(IsValidUtf8(s) ? s : AcpToUtf8(s));
    }
    std::vector<char*> argPtrs;
    argPtrs.reserve(static_cast<size_t>(argc));
    for (auto& s : argStorage) argPtrs.push_back(const_cast<char*>(s.c_str()));
    argv = argPtrs.data();
#endif

    Args args = ParseArgs(argc, argv);

    const bool interactive = StdoutIsTty();

    if (args.command.empty() || args.Has("help")) {
        PrintBanner();
        PrintUsage();
        return args.command.empty() && !args.Has("help") ? 1 : 0;
    }

    if (args.command == "version" || args.command == "-v") {
        std::cout << "orsend " << OrSendApp::Version() << std::endl;
        return 0;
    }

    if (args.command == "init") {
        const std::string path = args.positional.empty() ? "config.json" : args.positional[0];
        WriteSampleConfig(path);
        return 0;
    }

    if (args.command != "run" && args.command != "server" && args.command != "client" &&
        args.command != "forward" && args.command != "map") {
        std::cerr << "未知命令: " << args.command << std::endl;
        PrintUsage();
        return 1;
    }

    OrSendApp app;

    // 读取配置文件
    const std::string configPath = args.Get("config");
    if (!configPath.empty()) {
        std::string err;
        if (!app.LoadConfigFile(configPath, &err)) {
            std::cerr << err << std::endl;
            return 1;
        }
    }

    Json cfg = app.Config();

    // 通用参数
    if (args.Has("log-level")) cfg["log"]["level"] = args.Get("log-level");
    if (args.Has("log-file")) cfg["log"]["file"] = args.Get("log-file");
    if (args.Has("control-port")) {
        const int port = std::atoi(args.Get("control-port").c_str());
        cfg["control"]["enabled"] = port > 0;
        if (port > 0) cfg["control"]["port"] = port;
    }

    // 命令相关的默认启用项
    if (args.command == "server") {
        cfg["server"]["enabled"] = true;
        cfg["client"]["enabled"] = false;
        if (args.Has("bind")) cfg["server"]["bind"] = args.Get("bind");
        if (args.Has("token")) cfg["server"]["token"] = args.Get("token");
        if (args.Has("public-host")) cfg["server"]["publicHost"] = args.Get("public-host");
    } else if (args.command == "client") {
        cfg["server"]["enabled"] = false;
        cfg["client"]["enabled"] = true;
        if (args.Has("server")) cfg["client"]["server"] = args.Get("server");
        if (args.Has("token")) cfg["client"]["token"] = args.Get("token");
        if (args.Has("client-id")) cfg["client"]["clientId"] = args.Get("client-id");
        if (args.Has("name")) cfg["client"]["name"] = args.Get("name");
        if (!args.tunnels.empty()) {
            Json list = Json::MakeArray();
            for (const auto& spec : args.tunnels) {
                TunnelDef def;
                if (!ParseTunnelSpec(spec, def)) {
                    std::cerr << "隧道参数格式错误: " << spec << "（应为 name:localPort:remotePort）" << std::endl;
                    return 1;
                }
                Json item = Json::MakeObject();
                item["name"] = def.name;
                item["localHost"] = def.local.host;
                item["localPort"] = static_cast<int>(def.local.port);
                item["remotePort"] = static_cast<int>(def.remotePort);
                list.Push(item);
            }
            cfg["client"]["tunnels"] = list;
        }
    } else if (args.command == "forward") {
        cfg["server"]["enabled"] = false;
        cfg["client"]["enabled"] = false;
        ForwardRule rule;
        rule.name = args.Get("name");
        if (!ParseEndpoint(args.Get("listen"), rule.listen)) {
            std::cerr << "--listen 参数无效（示例 0.0.0.0:8080）" << std::endl;
            return 1;
        }
        if (!ParseEndpoint(args.Get("target"), rule.target)) {
            std::cerr << "--target 参数无效（示例 127.0.0.1:80）" << std::endl;
            return 1;
        }
        if (rule.name.empty()) rule.name = "forward-" + std::to_string(rule.listen.port);
        Json item = Json::MakeObject();
        item["name"] = rule.name;
        item["listen"] = rule.listen.ToString();
        item["target"] = rule.target.ToString();
        cfg["forward"].Push(item);
    } else if (args.command == "map") {
        cfg["server"]["enabled"] = false;
        cfg["client"]["enabled"] = false;
        Endpoint listen;
        if (!ParseEndpoint(args.Get("listen"), listen)) {
            std::cerr << "--listen 参数无效（示例 0.0.0.0:9000）" << std::endl;
            return 1;
        }
        Json item = Json::MakeObject();
        item["name"] = args.Get("name", "mapping-" + std::to_string(listen.port));
        item["listen"] = listen.ToString();
        item["strategy"] = args.Get("strategy", "roundrobin");
        Json targets = Json::MakeArray();
        for (const auto& part : SplitString(args.Get("targets"), ',')) {
            if (!Trim(part).empty()) targets.Push(Trim(part));
        }
        if (targets.Size() == 0) {
            std::cerr << "--targets 参数无效（示例 127.0.0.1:80,127.0.0.1:81）" << std::endl;
            return 1;
        }
        item["targets"] = targets;
        cfg["mapping"].Push(item);
    }

    app.SetConfig(cfg);

    if (interactive) PrintBanner();
    std::string err;
    if (!app.Start(&err)) {
        std::cerr << "启动失败: " << err << std::endl;
        return 1;
    }

    g_app = &app;
    InstallSignalHandlers();

    if (interactive) std::cout << "按 Ctrl+C 退出\n" << std::endl;
    app.Wait();
    app.Stop();
    g_app = nullptr;

    if (interactive) std::cout << "已退出" << std::endl;
    return 0;
}
