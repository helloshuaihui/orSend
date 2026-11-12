# 隧道协议

## 1. 帧格式

所有控制消息和数据都在同一条 TCP 连接上，用定长头部切分：

```
 0    1    2    3    4    5    6    7    8    9   10   11   12   13
+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
|  MAGIC  |VER |TYPE|  FLAGS  |        STREAM ID      |   LENGTH      |
+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
|                          PAYLOAD (LENGTH 字节)                      |
+---------------------------------------------------------------------+
```

| 字段 | 长度 | 说明 |
| --- | --- | --- |
| MAGIC | 2 | `0x4F52`，ASCII `"OR"` |
| VER | 1 | 协议版本，当前 `1` |
| TYPE | 1 | 消息类型 |
| FLAGS | 2 | 保留位，当前恒为 0 |
| STREAM ID | 4 | 大端，0 表示控制消息 |
| LENGTH | 4 | 大端，负载长度，上限 256 KB |

头部固定 14 字节，负载是裸字节（`DATA` 帧不做任何编码）。

## 2. 消息类型

| 值 | 名称 | 方向 | 负载 |
| --- | --- | --- | --- |
| `0x01` | HELLO | agent → server | JSON：`clientId` / `name` / `token` / `version` |
| `0x02` | HELLO_ACK | server → agent | JSON：`ok` / `message` / `sessionId` / `heartbeatSec` |
| `0x10` | REGISTER | agent → server | JSON：`name` / `localHost` / `localPort` / `remotePort` |
| `0x11` | REGISTER_ACK | server → agent | JSON：`name` / `ok` / `remotePort` / `message` |
| `0x12` | UNREGISTER | agent → server | JSON：`name` |
| `0x20` | TUNNEL_OPEN | server → agent | JSON：`tunnel` / `remoteAddr` |
| `0x21` | TUNNEL_OPEN_ACK | agent → server | JSON：`ok` / `message` |
| `0x30` | DATA | 双向 | 裸字节 |
| `0x31` | CLOSE | 双向 | JSON：`reason` |
| `0x40` | PING | agent → server | JSON：`time` |
| `0x41` | PONG | server → agent | JSON：`time` |

## 3. 时序

### 3.1 握手与注册

```
agent                                   server
  │ ── TCP connect ─────────────────────► │
  │ ── HELLO {token, clientId} ────────► │ 校验 token
  │ ◄──────────────── HELLO_ACK {ok} ─── │
  │ ── REGISTER {name,localPort,remotePort} ► │ 分配公网监听端口
  │ ◄────────────── REGISTER_ACK {port} ─ │
  │ ── PING（每 heartbeatSec）──────────► │
  │ ◄──────────────────── PONG ────────── │
```

- token 为空时服务端不校验（仅建议测试环境）
- 同一 `clientId` 重复上线，旧会话会被踢掉
- `remotePort = 0` 时由系统分配，实际端口通过 `REGISTER_ACK` 返回

### 3.2 会话建立

```
server                                  agent
  │ 公网连接进入，分配 streamId          │
  │ ── TUNNEL_OPEN {tunnel} ───────────► │
  │                                      │ 拨号 localHost:localPort
  │ ◄──────────── TUNNEL_OPEN_ACK {ok} ─ │
  │ 开始读公网 socket                     │
  │ ── DATA(streamId) ─────────────────► │
  │ ◄─────────────────── DATA(streamId) ─ │
  │ ── CLOSE(streamId) ────────────────► │
```

`TUNNEL_OPEN_ACK` 失败时，服务端直接关闭公网连接，并把原因写进日志。

### 3.3 断线与重连

```
agent                          server
  │ ── X 连接断开 ─────────────► │ 关闭该 agent 的所有隧道监听与 stream
  │                              │
  │ 等待 backoff（1s→2s→4s…30s）  │
  │ ── 重新 connect ────────────► │
  │ ── HELLO + REGISTER ────────► │ 重新注册全部隧道
```

服务端侧无需保存 agent 状态，重连即重新注册，天然幂等。

## 4. 错误处理

| 场景 | 行为 |
| --- | --- |
| magic / 版本不匹配 | 立即断开连接并记日志 |
| 负载长度 > 256 KB | 视为协议错误，断开 |
| token 错误 | 回 `HELLO_ACK{ok:false}` 后断开；客户端不再重试 |
| 注册同名隧道（不同 agent） | 回 `REGISTER_ACK{ok:false}` |
| 端口被占用 | 回 `REGISTER_ACK{ok:false}`，带系统错误信息 |
| 心跳超时（默认 90s） | 服务端主动断开该 agent |
| agent 掉线 | 该 agent 的公网端口立即关闭，避免"假在线" |

## 5. 扩展约定

新增消息类型时：

1. 在 `core/protocol/Protocol.h` 的 `MsgType` 里追加枚举值，**不要复用旧值**
2. 在 `MsgTypeName()` 里补上名字（日志可读性）
3. 负载统一用 JSON，字段只增不改；解析方对未知字段必须忽略
4. 老版本收到未知 `TYPE` 只记 debug 日志并跳过，不断连接
