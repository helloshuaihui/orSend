# 代码地图：哪个文件负责什么

一句话版本：

| 你关心的事 | 看这里 |
| --- | --- |
| **后端**（数据面、协议、隧道） | `core/`（C++17，约 5500 行）+ `apps/cli/main.cpp`（400 行，命令行入口） |
| **中间层**（进程托管、控制接口客户端、状态机） | `csharp/OrSend.Shared/` |
| **页面（桌面端）** | `csharp/OrSend.Server.Desktop/`、`csharp/OrSend.Client.Desktop/` + 公共样式 `csharp/OrSend.Desktop.Common/` |
| **页面（网页端）** | `csharp/OrSend.Server/wwwroot/`、`csharp/OrSend.Client/wwwroot/` |

引擎不认识 C#，页面也不直接碰 socket。中间只隔一条本机 `127.0.0.1` 上的换行分隔 JSON 请求，
所以**换页面不用动引擎，改引擎不用动页面**。

```
  页面层    WPF 桌面程序                      ASP.NET Core 网页
                 │                                    │
                 └──────────────┬─────────────────────┘
                                │  共用 OrSend.Shared：ServerRuntime / ClientRuntime
                                ▼
  中间层    EngineHost（拉起并托管 orsend.exe） + EngineClient（换行分隔 JSON-RPC）
                                │  127.0.0.1:7400+
                                ▼
  引擎层    ControlServer → OrSendApp（装配 / 命令分发） → RelayEngine、TunnelServer / TunnelClient
```

---

## 1. 后端：C++ 引擎 `core/`

按依赖方向从下往上读，越靠下越基础。

### 1.1 基础层 `core/common/`

| 文件 | 职责 | 关键类型 |
| --- | --- | --- |
| `Common.h/.cpp` | `"host:port"` 字符串的解析与格式化、字符串/时间小工具 | `Endpoint` |
| `Json.h/.cpp` | 极简 JSON 解析与序列化，零第三方依赖；控制接口和配置文件都用它 | `Json` |
| `Logger.h/.cpp` | 分级日志，同时输出到控制台与文件，并在内存里保留环形缓冲供页面拉取 | `Logger`、`LogEntry` |

### 1.2 网络层 `core/net/`

整个引擎的地基，**所有 IO 都在这里**，上面几层不直接调用 `send/recv`。

| 文件 | 职责 | 关键类型 |
| --- | --- | --- |
| `NetCompat.h/.cpp` | Winsock / POSIX 差异封装（启动清理、错误码、`poll` 兼容） | `NetRuntime` |
| `EventLoop.h/.cpp` | 单线程 poll reactor：fd 事件派发、定时器、`Post()` 跨线程投递 | `EventLoop` |
| `TcpConnection.h/.cpp` | 非阻塞连接：读缓冲、写缓冲、水位背压、优雅关闭（先刷写再 close） | `TcpConnection` |
| `TcpListener.h/.cpp` | 监听与 accept，accept 后回调交给上层 | `TcpListener` |
| `ConnectionReaper.h/.cpp` | 统一关闭路径：停回调 → 刷完缓冲 → 延迟到下一轮循环释放 | `ConnectionReaper` |

> 排障提示：凡是"响应少一截 / 连接莫名断开"，先看 `TcpConnection.cpp` 的 `HandleEvent`
> 和 `ConnectionReaper.cpp`，`docs/ARCHITECTURE.md` 第 5 节讲了为什么这么写。

### 1.3 协议层 `core/protocol/`

| 文件 | 职责 | 关键类型 |
| --- | --- | --- |
| `Protocol.h/.cpp` | 14 字节定长帧头、消息类型常量、增量解码器（半包/粘包处理） | `proto::Frame`、`proto::Decoder` |

细节见 `docs/PROTOCOL.md`。改这里等于改线上协议，两端必须同时升级 `VER`。

### 1.4 业务层 `core/relay/`、`forward/`、`mapping/`、`tunnel/`

| 文件 | 职责 | 关键类型 |
| --- | --- | --- |
| `relay/RelayEngine.h/.cpp` | **真正干活的地方**：一条规则 = 一个 `RuleRuntime`（监听 + 目标池 + 会话表 + 统计），负责挑目标、拨号、双向接线、健康检查、故障转移 | `RelayEngine`、`RelayRule`、`RelayTarget` |
| `forward/PortForward.h/.cpp` | 端口转发的**配置门面**：把 `ForwardRule`（单目标）翻译成 `RelayRule`，自己不转发 | `PortForward`、`ForwardRule` |
| `mapping/PortMapping.h/.cpp` | 端口映射的**配置门面**：多目标 + 策略字符串解析 + 健康检查开关 | `PortMapping`、`MappingRule` |
| `tunnel/TunnelServer.h/.cpp` | 公网侧：token 鉴权、agent 注册、公网端口监听、把公网连接切成 stream 复用进控制连接、心跳超时踢人 | `TunnelServer` |
| `tunnel/TunnelClient.h/.cpp` | 内网侧：连服务器、注册隧道、收到 `OPEN` 时拨号本地服务、心跳、指数退避重连 | `TunnelClient`、`TunnelDef` |

> 转发和映射共用 `RelayEngine`，两者的差别只在"目标是一个还是一组"。
> 所以加负载均衡策略要改 `relay/`，不是改 `forward/`。

### 1.5 编排层 `core/control/`、`core/app/`

| 文件 | 职责 | 关键类型 |
| --- | --- | --- |
| `control/ControlServer.h/.cpp` | 本机控制接口：监听 127.0.0.1，一行一个 JSON 请求、一行一个响应 | `ControlServer` |
| `app/OrSendApp.h/.cpp` | **总装配**：读配置 → 建组件 → 注册命令 → 管生命周期。所有 `xxx.add/remove` 命令的分发都在这 | `OrSendApp` |

### 1.6 进程入口 `apps/cli/main.cpp`

命令行解析与子命令：`run` / `server` / `client` / `forward` / `map` / `init` / `version`。
它把命令行参数拼成一份配置，然后交给 `OrSendApp` 跑，本身不含业务逻辑。

---

## 2. 中间层：`csharp/OrSend.Shared/`

桌面端和网页端**共用**这一层，是"两套界面、一套逻辑"的关键。

| 文件 | 职责 | 关键类型 |
| --- | --- | --- |
| `EngineHost.cs` | 查找 `orsend.exe`（配置的 `ExePath` → 从程序目录向上 8 级 → `PATH`）、拉起子进程、转发它的 stdout/stderr、退出时回收 | `EngineHost`、`EngineOptions` |
| `EngineClient.cs` | 控制接口客户端：连接 127.0.0.1、按行收发 JSON、按 id 配对请求响应、超时 | `EngineClient` |
| `JsonFileStore.cs` | 配置文件的读写（`App_Data/*.json`），带默认值兜底 | `JsonFileStore<T>` |
| `Models.cs` | 与引擎 JSON 一一对应的 DTO：状态、规则、隧道、日志、设置 | `EngineStatus`、`RuleInfo`、`ServerSettings` … |
| `ServerRuntime.cs` | **服务端状态机**：启动/停止引擎、把设置同步成规则、1.5s 轮询快照、日志增量拉取 | `ServerRuntime`、`ServerSnapshot` |
| `ClientRuntime.cs` | **客户端状态机**：连接/断开、隧道增删、重连状态、快照 | `ClientRuntime`、`ClientSnapshot` |

> 想让两个界面同时获得一个新能力（比如"一键导出配置"），写在这一层，两边都能用。

---

## 3. 页面 A：WPF 桌面程序

### 3.1 公共样式 `csharp/OrSend.Desktop.Common/`

| 文件 | 职责 |
| --- | --- |
| `Theme.xaml` | 全部视觉：调色板、`Card` / `Pill` / `Btn` / `BtnPrimary` / `BtnDanger` / `InputBox` / `InputCombo` / `InputCheck` / `NavItem` / `DataTable` 等样式，全部自定义模板 |
| `Converters.cs` | `TabVisibilityConverter`（按 key 切换内容区）、`BoolToVisibilityConverter`、`InverseBoolConverter`、`DurationConverter`、`BytesConverter` |
| `RelayCommand.cs` | MVVM 命令，支持 async，执行中禁止重入 |
| `Ui.cs` | `Fmt`（字节/时长格式化）、`LogRow`（日志按级别着色）、`NavItem`、`WindowFit`（窗口尺寸兜底） |

主色不在这里定，由各程序的 `App.xaml` 覆盖 `AccentBrush`：服务端蓝 `#2563EB`，客户端青绿 `#0D9488`。

### 3.2 服务端桌面 `csharp/OrSend.Server.Desktop/`

| 文件 | 职责 |
| --- | --- |
| `App.xaml` / `App.xaml.cs` | 合并主题、注册转换器、建 `App_Data/server-settings.json` 的 store、构造 `ServerRuntime` 并启动 |
| `MainWindow.xaml` | **界面本体**：自绘标题栏 + 左侧 6 项导航 + 6 个内容区（概览 / 客户端与隧道 / 端口转发 / 端口映射 / 运行日志 / 设置） |
| `MainWindow.xaml.cs` | 只有窗口按钮和日志自动滚动，不写业务 |
| `ServerViewModel.cs` | 全部逻辑：1.5s 轮询、指标卡与表格填充、启停、踢客户端、增删转发/映射、保存设置 |

### 3.3 客户端桌面 `csharp/OrSend.Client.Desktop/`

| 文件 | 职责 |
| --- | --- |
| `App.xaml` / `App.xaml.cs` | 同上，青色主色，配置文件为 `client-settings.json` |
| `MainWindow.xaml` | 标题栏 + 4 个分区（概览 / 隧道 / 运行日志 / 设置） |
| `ClientViewModel.cs` | 连接/断开、隧道增删、按 `ready/connecting/handshaking/backoff/idle` 上色 |

---

## 4. 页面 B：网页控制台

桌面版之外的另一层外壳，适合没有图形界面的服务器，或者远程用浏览器看一眼状态。
它和桌面版调的是同一套 `ServerRuntime` / `ClientRuntime`，所以功能与行为一致。

| 文件 | 职责 |
| --- | --- |
| `OrSend.Server/Program.cs` | ASP.NET Core minimal API：静态页 + `/api/*` 转发到 `ServerRuntime`，端口 5080 |
| `OrSend.Client/Program.cs` | 同上，端口 5081 |
| `OrSend.Server/wwwroot/index.html` | 服务端页面骨架（导航 + 各分区容器） |
| `OrSend.Server/wwwroot/style.css` | 网页端样式，410 行 |
| `OrSend.Server/wwwroot/app.js` | 网页端逻辑：拉 `/api/status`、渲染表格、发操作请求 |
| `OrSend.Client/wwwroot/*` | 客户端页面，结构相同（`style.css` 是同一份拷贝） |
| `OrSend.Client/wwwroot/client.css` | 12 行，只把主色改成青绿，其余复用 `style.css` |

---

## 5. 测试与构建

| 文件 | 职责 |
| --- | --- |
| `tests/e2e_test.py` | 端到端集成测试：起引擎、起 HTTP 源站、跑转发/映射/穿透/鉴权/重连/并发/大报文，共 18 项断言 |
| `build.ps1` | Windows 一键构建（自动补 `vcvars`、Windows SDK 环境） |
| `build.bat` | 双击入口，转调 `build.ps1` |
| `CMakeLists.txt` | 只构建 `orsend_core` 静态库和 `orsend` 可执行文件 |

---

## 6. 改需求时该动哪些文件

这张表按"我要做什么"反查，比从头读代码快。

| 需求 | 需要改的文件 |
| --- | --- |
| 加一种负载均衡策略 | `core/relay/RelayEngine.cpp`（`ParseRelayStrategy` + `select`）、`OrSend.Server.Desktop/ServerViewModel.cs`（`Strategies` 列表）、`OrSend.Server/wwwroot/app.js`（下拉项） |
| 加一个控制命令 | `core/app/OrSendApp.cpp`（`HandleCommand` 分支）、`OrSend.Shared/EngineClient.cs`（封装方法）、对应 `*ViewModel.cs` |
| 加一个桌面导航页 | `MainWindow.xaml`（`NavItem` + 内容区 `Border`）、`*ViewModel.cs`（`TabKey` 与数据）、必要时 `Converters.cs` |
| 改界面配色 | `OrSend.Desktop.Common/Theme.xaml`（语义色）+ 各 `App.xaml`（`AccentBrush` 覆盖） |
| 加一个配置项 | `OrSend.Shared/Models.cs`（设置 DTO）、`core/app/OrSendApp.cpp`（读取）、`OrSend.Shared/*Runtime.cs`（同步给引擎）、桌面 `MainWindow.xaml` 设置页 |
| 改隧道协议 | `core/protocol/Protocol.h` + `docs/PROTOCOL.md`，两端同时升 `VER` |
| 修"响应被截断"类问题 | `core/net/TcpConnection.cpp`、`core/net/ConnectionReaper.cpp`，然后跑 `tests/e2e_test.py` |
| 加一个网页接口 | `OrSend.Server/Program.cs`（`MapPost`）+ `wwwroot/app.js` |

---

## 7. 读代码的推荐顺序

1. `apps/cli/main.cpp` —— 看程序怎么被用起来
2. `core/app/OrSendApp.cpp` —— 看组件怎么装配、命令怎么分发
3. `core/net/EventLoop.cpp` → `TcpConnection.cpp` —— 看 IO 模型
4. `core/relay/RelayEngine.cpp` —— 看转发怎么跑
5. `core/tunnel/TunnelServer.cpp` → `TunnelClient.cpp` —— 看穿透怎么跑
6. `csharp/OrSend.Shared/ServerRuntime.cs` → `ServerViewModel.cs` —— 看界面怎么驱动引擎

配合 `docs/ARCHITECTURE.md` 看设计取舍，配合 `docs/PROTOCOL.md` 看字节格式。
