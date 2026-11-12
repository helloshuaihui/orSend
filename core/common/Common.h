// orSend - 跨平台 TCP 端口转发 / 端口映射 / 内网穿透
// 公共基础类型与工具函数
#pragma once
#ifndef ORSEND_COMMON_H
#define ORSEND_COMMON_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orsend {

// 网络端点。目前只支持 IPv4 字面量地址与通配地址（0.0.0.0）。
struct Endpoint {
    std::string host;
    uint16_t port = 0;

    Endpoint() = default;
    Endpoint(std::string h, uint16_t p) : host(std::move(h)), port(p) {}

    std::string ToString() const;
    bool Valid() const { return !host.empty() && port != 0; }
};

// 解析 "0.0.0.0:8080" / "127.0.0.1:80" / ":8080"（host 留空表示通配）
bool ParseEndpoint(const std::string& text, Endpoint& out);

// 解析 "host:port" 或纯端口号（portOnly 为 true 时）
bool ParseHostPort(const std::string& host, int port, Endpoint& out);

std::string Trim(const std::string& s);
std::vector<std::string> SplitString(const std::string& s, char sep);
std::string ToLowerAscii(std::string s);
bool IEquals(const std::string& a, const std::string& b);

// 人类可读的格式化
std::string FormatBytes(uint64_t bytes);
std::string FormatDuration(int64_t seconds);
std::string FormatRate(double bytesPerSec);

// 时间
int64_t NowMs();
int64_t NowSec();
std::string FormatTimeLocal(int64_t epochMs, const char* fmt = "%Y-%m-%d %H:%M:%S");

// 随机数（用于 stream id / 会话 id）
uint64_t RandomU64();
uint32_t RandomU32();

}  // namespace orsend

#endif  // ORSEND_COMMON_H
