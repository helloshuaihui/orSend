# orSend

跨平台的 **TCP 端口转发 / 端口映射 / 内网穿透** 工具。

- **引擎**（C++17）：事件驱动、单线程 reactor、零第三方依赖，Windows / Linux 通用
- **桌面控制台**（C# / WPF）：服务端与客户端各一个桌面程序，自己托管引擎进程，推荐日常使用
- **网页控制台**（C# / ASP.NET Core）：同一套运行时的网页外壳，远程查看状态用
- 命令行、桌面、网页共用同一套二进制隧道协议：命令行能做的事，界面也能做

```
公网用户 ──► 服务器公网端口 18080 ──► TunnelServer ══(复用控制连接)══► TunnelClient ──► 内网 127.0.0.1:8080
```

---

## 1. 功能

| 能力 | 说明 |
| --- | --- |
| 端口转发 | 本地监听端口 → 单一固定目标，点对点直通 |
| 端口映射 | 本地监听端口 → 一组目标，支持轮询 / 最少连接 / 主备 / 随机 + 健康检查 + 故障转移 |
| 内网穿透 | 内网机器主动外连公网服务器，把内网服务暴露到公网端口，**无需公网 IP、无需路由器端口映射** |
| 多路复用 | 一条控制连接上跑任意多条 stream，公网并发连接不额外消耗握手成本 |
| 自动重连 | 客户端指数退避重连，重连后自动重新注册隧道 |
| 心跳保活 | 客户端定时 Ping，服务端超时踢除僵尸会话 |
| 流量统计 | 按规则 / 隧道 / 客户端统计连接数与上下行字节 |
| 运行时改配置 | 控制接口支持动态增删规则，不需要重启进程 |
| 双控制台 | 服务端控制台（管隧道与规则）+ 客户端控制台（管本机隧道），提供 WPF 桌面版与网页版 |

---

## 2. 目录结构

```
orSend/
├── core/                       C++ 核心库（orsend_core）
│   ├── common/                 Endpoint、JSON、日志
│   ├── net/                    EventLoop（poll 多路复用）、TcpConnection、TcpListener
│   ├── protocol/               二进制帧协议 + 增量解码器
│   ├── relay/                  转发引擎（目标池 / 策略 / 健康检查）
│   ├── forward/                端口转发的配置门面
│   ├── mapping/                端口映射的配置门面
│   ├── tunnel/                 TunnelServer / TunnelClient（内网穿透）
│   ├── control/                本地 JSON-RPC 控制接口
│   └── app/                    配置装配与命令分发
├── apps/cli/                   orsend 可执行文件
├── csharp/
│   ├── OrSend.Shared/          模型、控制接口客户端、引擎进程托管、运行时
│   ├── OrSend.Desktop.Common/  WPF 主题与公共控件样式
│   ├── OrSend.Server.Desktop/  服务端桌面程序（WPF，推荐）
│   ├── OrSend.Client.Desktop/  客户端桌面程序（WPF，推荐）
│   ├── OrSend.Server/          服务端网页控制台（可选，http://127.0.0.1:5080）
│   └── OrSend.Client/          客户端网页控制台（可选，http://127.0.0.1:5081）
├── tests/e2e_test.py           端到端集成测试
├── docs/                       文档：代码地图 / 架构 / 协议
├── build.ps1                   Windows 一键构建
└── CMakeLists.txt
```

**哪个文件负责什么**——后端（`core/`）、中间层（`OrSend.Shared`）、桌面页面、网页页面分别在哪，
以及"改某个需求该动哪几个文件"，见 [`docs/CODE_MAP.md`](docs/CODE_MAP.md)。
文档总索引见 [`docs/README.md`](docs/README.md)。

---

## 3. 快速开始

### 3.1 编译 C++ 引擎

**Windows（需要 VS 2022 的「使用 C++ 的桌面开发」工作负载）**

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
# 产物: build\bin\orsend.exe
```

脚本会自动处理两个常见坑：`vcvars` 未注册、Windows SDK 未被识别（自动补齐 `INCLUDE/LIB/PATH`）。

**Linux**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# 产物: build/bin/orsend
```

### 3.2 启动桌面控制台（推荐）

两个 WPF 桌面程序，直接在本机托管 `orsend` 引擎进程，不经过 HTTP：

```bash
cd csharp
dotnet build -c Release

# 服务端控制台（公网机器上运行）
.\OrSend.Server.Desktop\bin\Release\net8.0-windows\OrSend.Server.Desktop.exe

# 客户端控制台（内网机器上运行）
.\OrSend.Client.Desktop\bin\Release\net8.0-windows\OrSend.Client.Desktop.exe
```

界面按左侧导航分区：

| 程序 | 分区 |
| --- | --- |
| 服务端 | 概览 / 客户端与隧道 / 端口转发 / 端口映射 / 运行日志 / 设置 |
| 客户端 | 概览 / 隧道 / 运行日志 / 设置 |

引擎进程由程序自己拉起和回收，配置保存在可执行文件同级的 `App_Data/*.json`。

### 3.3 启动网页控制台（可选，远程访问用）

不想装客户端、或要远程查看状态时，可以用同一套运行时 + 网页界面：

```bash
cd csharp

# 服务端网页控制台（公网机器上运行）
dotnet run --project OrSend.Server -c Release
# 打开 http://127.0.0.1:5080

# 客户端网页控制台（内网机器上运行）
dotnet run --project OrSend.Client -c Release
# 打开 http://127.0.0.1:5081
```

页面会自动查找并拉起 `build/bin/orsend.exe`（从当前目录向上最多 8 级，其次查 `PATH`）。
也可以在 `appsettings.json` 的 `Engine:ExePath` 里写死路径。

---

## 4. 三种用法

### 4.1 端口转发（单机）

把本机 8080 的流量转给 80：

```bash
orsend forward --listen 0.0.0.0:8080 --target 127.0.0.1:80
```

等价配置：

```json
{
  "forward": [
    { "name": "web", "listen": "0.0.0.0:8080", "target": "127.0.0.1:80" }
  ]
}
```

### 4.2 端口映射（一对多）

把 9000 轮询分发到两个后端，目标挂掉自动跳过：

```bash
orsend map --listen 0.0.0.0:9000 \
           --targets 127.0.0.1:8081,127.0.0.1:8082 \
           --strategy roundrobin
```

策略：`roundrobin` / `leastconn` / `failover` / `random`。

### 4.3 内网穿透

**公网服务器**

```bash
orsend server --bind 0.0.0.0:7000 --token 你的口令 --public-host 0.0.0.0
```

**内网机器（agent）**

```bash
orsend client --server 公网IP:7000 --token 你的口令 \
              --client-id home-nas --name "家里的NAS" \
              --tunnel web:8080:18080
```

`--tunnel 名字:内网端口:公网端口`，公网端口写 `0` 表示由服务端自动分配。

之后访问 `http://公网IP:18080` 就是内网机器的 `127.0.0.1:8080`。

**用一份配置同时跑服务端 + 转发 + 映射**

```bash
orsend init config.json     # 生成示例
orsend run -c config.json
```

---

## 5. 配置说明

```json
{
  "log": { "level": "info", "file": "logs/orsend.log", "console": true },

  "control": { "enabled": true, "host": "127.0.0.1", "port": 7400 },

  "server": {
    "enabled": true,
    "bind": "0.0.0.0:7000",
    "token": "change-me",
    "publicHost": "0.0.0.0",
    "heartbeatTimeoutSec": 90
  },

  "client": {
    "enabled": false,
    "server": "1.2.3.4:7000",
    "token": "change-me",
    "clientId": "home-nas",
    "name": "家里的 NAS",
    "heartbeatSec": 20,
    "reconnectMinMs": 1000,
    "reconnectMaxMs": 30000,
    "tunnels": [
      { "name": "web", "localHost": "127.0.0.1", "localPort": 8080, "remotePort": 18080 }
    ]
  },

  "forward": [
    { "name": "local-web", "listen": "0.0.0.0:8080", "target": "127.0.0.1:80" }
  ],

  "mapping": [
    {
      "name": "web-cluster",
      "listen": "0.0.0.0:9000",
      "targets": ["127.0.0.1:8081", "127.0.0.1:8082"],
      "strategy": "roundrobin",
      "healthCheck": true,
      "healthIntervalSec": 10
    }
  ]
}
```

`server` / `client` / `forward` / `mapping` 四段互相独立，可以只启用其中任意几段。

---

## 6. 控制接口

监听 `127.0.0.1`，一行一个 JSON 请求，一行一个 JSON 响应。页面就是通过它驱动引擎的。

```bash
# 查询状态
{"id":1,"cmd":"status"}

# 动态加一条转发
{"id":2,"cmd":"forward.add","args":{"name":"tmp","listen":"0.0.0.0:8080","target":"127.0.0.1:80"}}

# 拉日志
{"id":3,"cmd":"logs","args":{"sinceId":0,"limit":100}}
```

| 命令 | 参数 | 说明 |
| --- | --- | --- |
| `status` | — | 全量状态（服务端 / 客户端 / 规则 / 汇总） |
| `logs` | `sinceId`, `limit` | 增量拉日志 |
| `ping` | — | 存活探测 |
| `server.start` / `server.stop` | `bind`, `token`, `publicHost` | 启停穿透服务端 |
| `server.agents` / `server.tunnels` | — | 在线客户端 / 隧道列表 |
| `server.kick` | `clientId` | 踢掉某个客户端 |
| `client.start` / `client.stop` | `server`, `token`, `clientId`, `name` | 启停穿透客户端 |
| `client.addTunnel` / `client.removeTunnel` | `name`, `localHost`, `localPort`, `remotePort` | 运行时增删隧道 |
| `forward.list` / `forward.add` / `forward.remove` | `name`, `listen`, `target` | 转发规则 |
| `mapping.list` / `mapping.add` / `mapping.remove` | `name`, `listen`, `targets`, `strategy` | 映射规则 |
| `shutdown` | — | 退出进程 |

---

## 7. 测试

```bash
python tests/e2e_test.py
```

覆盖端口转发、端口映射、内网穿透、token 鉴权失败、控制接口、运行时动态规则、断线重连，
共 18 项断言。其中包含三条压力用例：**512KB 大报文**（转发 / 穿透各一条）和
**20 并发转发 / 30 并发穿透**，专门盯住"响应被截断"这类关闭时机的问题。

排障时可以打开详细日志：

```bash
ORSEND_TEST_LOG_LEVEL=debug python tests/e2e_test.py   # 会话建立/关闭都会打印字节数
```

---

## 8. 已知限制

- 只支持 IPv4（`0.0.0.0` / 具体地址），暂不支持 IPv6 与 UDP
- 服务端是单线程 reactor，连接数上限受单核性能约束（数千级并发没问题，再往上需要多线程 loop）
- 隧道内所有 stream 复用一条控制连接，单条流的背压会轻微影响同连接上的其它流
- 暂不支持 TLS；跨公网部署建议放在 Nginx / frp 之类的 TLS 前置之后，或走内网 / VPN

---

## 9. 从旧版本迁移

原 `orSend/` 目录（`TcpSocketClass` + `PortForward` + `PortMapping` + `ThreadPool`）已被重构：

| 旧文件 | 现在的位置 |
| --- | --- |
| `core/TcpSocketClass.*` | 拆成 `core/net/EventLoop` + `TcpConnection` + `TcpListener`，改为非阻塞 reactor |
| `core/PortForward.*` | `core/forward/PortForward`（配置门面）+ `core/relay/RelayEngine`（真正的转发） |
| `core/PortMapping.*` | `core/mapping/PortMapping`（多目标 + 策略 + 健康检查） |
| `core/ThreadPool.*` | 不再需要：一条连接不再占用一个线程 |
| `orSend.cpp` | `apps/cli/main.cpp`，支持子命令 |

重构的核心原因是原来的模型「一个连接两个阻塞线程」，无法支撑内网穿透里成百上千条 stream。
