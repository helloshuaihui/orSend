#include "TcpListener.h"

#include <cstring>

#include "Logger.h"
#include "TcpConnection.h"

namespace orsend {

TcpListener::Ptr TcpListener::Create(EventLoop& loop, const Endpoint& endpoint, AcceptCallback cb,
                                     std::string* error) {
    auto fail = [&](const std::string& msg) -> Ptr {
        if (error) *error = msg;
        return nullptr;
    };

    if (endpoint.port == 0 && endpoint.host.empty()) return fail("监听地址无效");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(endpoint.port);

    std::string host = endpoint.host.empty() ? "0.0.0.0" : endpoint.host;
    if (host == "*" || host == "any") host = "0.0.0.0";

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) {
            return fail("无法解析监听地址: " + host);
        }
        addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }

    socket_t fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidSocket) {
        return fail("创建监听 socket 失败: " + SocketErrorString(LastSocketError()));
    }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#ifdef SO_REUSEPORT
    // Linux 下同时开启，便于快速重启
#ifndef _WIN32
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&one), sizeof(one));
#endif
#endif

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const std::string msg = "绑定 " + endpoint.ToString() + " 失败: " + SocketErrorString(LastSocketError());
        CloseSocket(fd);
        return fail(msg);
    }

    if (::listen(fd, SOMAXCONN) != 0) {
        const std::string msg = "监听 " + endpoint.ToString() + " 失败: " + SocketErrorString(LastSocketError());
        CloseSocket(fd);
        return fail(msg);
    }

    SetNonBlockingImpl(fd, true);

    uint16_t actualPort = endpoint.port;
    if (actualPort == 0) {
        sockaddr_in bound{};
#ifdef _WIN32
        int len = sizeof(bound);
#else
        socklen_t len = sizeof(bound);
#endif
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
            actualPort = ntohs(bound.sin_port);
        }
    }

    auto listener = Ptr(new TcpListener(loop, fd, endpoint, actualPort));
    listener->onAccept_ = std::move(cb);
    std::weak_ptr<TcpListener> weak = listener;
    loop.AddFd(fd, EventLoop::kReadable, [weak](uint32_t) {
        auto self = weak.lock();
        if (self) self->HandleAccept();
    });
    return listener;
}

TcpListener::TcpListener(EventLoop& loop, socket_t fd, const Endpoint& bind, uint16_t actualPort)
    : loop_(loop), fd_(fd), bindEndpoint_(bind), localPort_(actualPort) {}

TcpListener::~TcpListener() { Close(); }

void TcpListener::Close() {
    if (fd_ == kInvalidSocket) return;
    loop_.RemoveFd(fd_);
    CloseSocket(fd_);
    fd_ = kInvalidSocket;
}

void TcpListener::HandleAccept() {
    for (;;) {
        sockaddr_in peer{};
#ifdef _WIN32
        int len = sizeof(peer);
#else
        socklen_t len = sizeof(peer);
#endif
        socket_t client = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer), &len);
        if (client == kInvalidSocket) {
            const int err = LastSocketError();
            if (IsWouldBlock(err)) return;
            LOG_WARN("listener", "accept 失败: " << SocketErrorString(err));
            return;
        }

        SetNonBlockingImpl(client, true);
        int one = 1;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
        setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&one), sizeof(one));

        char ipBuf[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &peer.sin_addr, ipBuf, sizeof(ipBuf));
        Endpoint peerEp(std::string(ipBuf), ntohs(peer.sin_port));

        if (onAccept_) onAccept_(client, peerEp);
    }
}

}  // namespace orsend
