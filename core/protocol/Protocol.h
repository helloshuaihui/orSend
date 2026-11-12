// 隧道协议：定长头部 + 变长负载
//
//  0      1      2      3      4-5      6-9        10-13       14..
//  +------+------+------+------+--------+----------+----------+---------+
//  | MAGIC(2)    | VER  | TYPE | FLAGS  | STREAM   | LENGTH   | PAYLOAD |
//  +------+------+------+------+--------+----------+----------+---------+
//
// 一个 TCP 连接（agent <-> server）上复用任意多条 stream，
// 每条 stream 对应公网侧的一个客户端连接。
#pragma once
#ifndef ORSEND_PROTOCOL_H
#define ORSEND_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace orsend {
namespace proto {

constexpr uint16_t kMagic = 0x4F52;  // "OR"
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderSize = 14;
constexpr uint32_t kMaxPayload = 256 * 1024;

enum class MsgType : uint8_t {
    Hello = 0x01,        // agent -> server: 认证 + 身份
    HelloAck = 0x02,     // server -> agent: 认证结果
    Register = 0x10,     // agent -> server: 注册隧道
    RegisterAck = 0x11,  // server -> agent: 注册结果（含实际公网端口）
    Unregister = 0x12,   // agent -> server: 注销隧道
    TunnelOpen = 0x20,   // server -> agent: 有新连接进来
    TunnelOpenAck = 0x21,// agent -> server: 本地目标是否连接成功
    Data = 0x30,         // 双向：stream 数据
    Close = 0x31,        // 双向：关闭 stream
    Ping = 0x40,
    Pong = 0x41,
};

const char* MsgTypeName(MsgType type);

struct Frame {
    uint8_t type = 0;
    uint16_t flags = 0;
    uint32_t streamId = 0;
    std::string payload;

    MsgType Type() const { return static_cast<MsgType>(type); }
};

std::string Encode(uint8_t type, uint32_t streamId, const std::string& payload, uint16_t flags = 0);

// 增量解码器：喂入任意长度的字节流，逐帧取出
class Decoder {
public:
    // 返回 false 表示协议错误（magic/版本/长度非法），调用方应断开连接
    bool Feed(const char* data, size_t len);
    bool Next(Frame& out);
    void Reset();

    size_t Buffered() const { return buf_.size() - readPos_; }
    bool HasError() const { return error_; }

private:
    bool ParseHeader();

    std::string buf_;
    size_t readPos_ = 0;
    bool error_ = false;

    // 当前帧头部解析结果
    bool headerReady_ = false;
    uint8_t type_ = 0;
    uint16_t flags_ = 0;
    uint32_t streamId_ = 0;
    uint32_t payloadLen_ = 0;
};

}  // namespace proto
}  // namespace orsend

#endif  // ORSEND_PROTOCOL_H
