// 平台 socket 兼容层：统一 Windows / Linux 的类型与错误码
#pragma once
#ifndef ORSEND_NETCOMPAT_H
#define ORSEND_NETCOMPAT_H

#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601  // 需要 WSAPoll
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

namespace orsend {
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;

inline int LastSocketError() { return WSAGetLastError(); }
inline bool IsWouldBlock(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
inline bool IsInProgress(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEINVAL; }
inline bool IsConnReset(int e) {
    return e == WSAECONNRESET || e == WSAECONNABORTED || e == WSAENETRESET || e == WSAESHUTDOWN ||
           e == WSAENOTCONN || e == WSAETIMEDOUT;
}
inline void CloseSocket(socket_t s) {
    if (s != kInvalidSocket) ::closesocket(s);
}
inline void ShutdownWrite(socket_t s) { ::shutdown(s, SD_SEND); }
inline void ShutdownBoth(socket_t s) { ::shutdown(s, SD_BOTH); }
inline int SetNonBlockingImpl(socket_t s, bool on) {
    u_long mode = on ? 1 : 0;
    return ::ioctlsocket(s, FIONBIO, &mode);
}
inline std::string SocketErrorString(int code) {
    switch (code) {
        case 0: return "success";
        case WSAECONNREFUSED: return "连接被拒绝 (10061)";
        case WSAETIMEDOUT: return "连接超时 (10060)";
        case WSAENETUNREACH: return "网络不可达 (10051)";
        case WSAEHOSTUNREACH: return "主机不可达 (10065)";
        case WSAEADDRINUSE: return "地址已被占用 (10048)";
        case WSAEACCES: return "权限不足 (10013)";
        case WSAECONNRESET: return "连接被重置 (10054)";
        case WSAEWOULDBLOCK: return "资源暂时不可用 (10035)";
        default: return "socket 错误 " + std::to_string(code);
    }
}

// ---- poll 抽象（Windows 使用 WSAPoll，避免 select 的 FD_SETSIZE 限制）----
using pollfd_t = WSAPOLLFD;
inline int PollOnce(pollfd_t* fds, unsigned long count, int timeoutMs) {
    return ::WSAPoll(fds, count, timeoutMs);
}
}  // namespace orsend

#else  // POSIX

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace orsend {
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;

inline int LastSocketError() { return errno; }
inline bool IsWouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS; }
inline bool IsInProgress(int e) { return e == EINPROGRESS || e == EWOULDBLOCK || e == EAGAIN; }
inline bool IsConnReset(int e) {
    return e == ECONNRESET || e == EPIPE || e == ENOTCONN || e == ETIMEDOUT || e == ECONNABORTED;
}
inline void CloseSocket(socket_t s) {
    if (s != kInvalidSocket) ::close(s);
}
inline void ShutdownWrite(socket_t s) { ::shutdown(s, SHUT_WR); }
inline void ShutdownBoth(socket_t s) { ::shutdown(s, SHUT_RDWR); }
inline int SetNonBlockingImpl(socket_t s, bool on) {
    int flags = ::fcntl(s, F_GETFL, 0);
    if (flags < 0) return -1;
    if (on) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return ::fcntl(s, F_SETFL, flags);
}
inline std::string SocketErrorString(int code) {
    switch (code) {
        case 0: return "success";
        case ECONNREFUSED: return "连接被拒绝";
        case ETIMEDOUT: return "连接超时";
        case ENETUNREACH: return "网络不可达";
        case EHOSTUNREACH: return "主机不可达";
        case EADDRINUSE: return "地址已被占用";
        case EACCES: return "权限不足";
        case ECONNRESET: return "连接被重置";
        case EWOULDBLOCK: return "资源暂时不可用";
        default: return "socket 错误 " + std::to_string(code);
    }
}

using pollfd_t = struct pollfd;
inline int PollOnce(pollfd_t* fds, unsigned long count, int timeoutMs) {
    return ::poll(fds, static_cast<nfds_t>(count), timeoutMs);
}
}  // namespace orsend

#endif

namespace orsend {

// 进程级网络运行时（Windows 下即 WSAStartup / WSACleanup）
class NetRuntime {
public:
    static bool Init();
    static void Shutdown();
};

}  // namespace orsend

#endif  // ORSEND_NETCOMPAT_H
