#include "Common.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <sstream>

namespace orsend {

std::string Endpoint::ToString() const {
    if (host.empty()) return ":" + std::to_string(port);
    return host + ":" + std::to_string(port);
}

bool ParseEndpoint(const std::string& text, Endpoint& out) {
    std::string s = Trim(text);
    if (s.empty()) return false;

    // 去掉可能的 scheme 前缀，例如 tcp://1.2.3.4:80
    const size_t scheme = s.find("://");
    if (scheme != std::string::npos) s = s.substr(scheme + 3);

    const size_t colon = s.rfind(':');
    if (colon == std::string::npos) return false;

    std::string hostPart = s.substr(0, colon);
    std::string portPart = s.substr(colon + 1);

    // 支持 [::1]:80 形式（虽然本版本只做 IPv4，但至少能解析出端口）
    if (!hostPart.empty() && hostPart.front() == '[' && hostPart.back() == ']') {
        hostPart = hostPart.substr(1, hostPart.size() - 2);
    }
    if (hostPart.empty()) hostPart = "0.0.0.0";

    char* end = nullptr;
    long port = std::strtol(portPart.c_str(), &end, 10);
    if (end == portPart.c_str() || *end != '\0') return false;
    if (port < 0 || port > 65535) return false;

    out.host = hostPart;
    out.port = static_cast<uint16_t>(port);
    return true;
}

bool ParseHostPort(const std::string& host, int port, Endpoint& out) {
    if (port < 0 || port > 65535) return false;
    std::string h = Trim(host);
    if (h.empty()) h = "0.0.0.0";
    out.host = h;
    out.port = static_cast<uint16_t>(port);
    return true;
}

std::string Trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> SplitString(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::string ToLowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::string FormatBytes(uint64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    char buf[64];
    if (u == 0) {
        std::snprintf(buf, sizeof(buf), "%llu %s", static_cast<unsigned long long>(bytes), units[u]);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f %s", v, units[u]);
    }
    return std::string(buf);
}

std::string FormatDuration(int64_t seconds) {
    if (seconds < 0) seconds = 0;
    int64_t d = seconds / 86400;
    int64_t h = (seconds % 86400) / 3600;
    int64_t m = (seconds % 3600) / 60;
    int64_t s = seconds % 60;

    std::ostringstream os;
    if (d > 0) os << d << "天";
    if (h > 0 || d > 0) os << h << "小时";
    if (m > 0 || h > 0 || d > 0) os << m << "分";
    os << s << "秒";
    return os.str();
}

std::string FormatRate(double bytesPerSec) {
    return FormatBytes(static_cast<uint64_t>(bytesPerSec)) + "/s";
}

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int64_t NowSec() { return NowMs() / 1000; }

std::string FormatTimeLocal(int64_t epochMs, const char* fmt) {
    std::time_t t = static_cast<std::time_t>(epochMs / 1000);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char buf[128];
    std::strftime(buf, sizeof(buf), fmt, &tmBuf);
    return std::string(buf);
}

uint64_t RandomU64() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    return rng();
}

uint32_t RandomU32() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    return rng();
}

}  // namespace orsend
