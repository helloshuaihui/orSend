#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <cstring>
#include <atomic>
#include <mutex>

// 链接 Winsock 库
#pragma comment(lib, "ws2_32.lib")

// 服务器配置
const uint16_t SERVER_PORT = 8888;       // 监听端口
const int MAX_BUFFER_SIZE = 1024;        // 接收缓冲区大小
const int MAX_CLIENT_COUNT = 10;         // 最大客户端数量（select 支持的 fd_set 大小有限制）

class SelectTcpServer {
public:
    SelectTcpServer() : m_listenSock(INVALID_SOCKET), m_isRunning(false) {}

    ~SelectTcpServer() {
        Stop();
    }

    // 启动服务器
    bool Start() {
        // 1. 初始化 Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup 失败，错误码：" << WSAGetLastError() << std::endl;
            return false;
        }

        // 2. 创建监听套接字（TCP）
        m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSock == INVALID_SOCKET) {
            std::cerr << "创建监听套接字失败，错误码：" << WSAGetLastError() << std::endl;
            WSACleanup();
            return false;
        }

        // 3. 设置地址复用（避免端口占用）
        int opt = 1;
        if (setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<const char*>(&opt), sizeof(opt)) == SOCKET_ERROR) {
            std::cerr << "setsockopt 失败，错误码：" << WSAGetLastError() << std::endl;
            Cleanup();
            return false;
        }

        // 4. 绑定端口
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;  // 监听所有本地 IP
        serverAddr.sin_port = htons(SERVER_PORT); // 转换为网络字节序

        if (bind(m_listenSock, reinterpret_cast<SOCKADDR*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "绑定端口失败，错误码：" << WSAGetLastError() << std::endl;
            Cleanup();
            return false;
        }

        // 5. 开始监听（最大等待队列 5）
        if (listen(m_listenSock, 5) == SOCKET_ERROR) {
            std::cerr << "监听失败，错误码：" << WSAGetLastError() << std::endl;
            Cleanup();
            return false;
        }

        m_isRunning = true;
        std::cout << "服务器启动成功，监听端口：" << SERVER_PORT << "（按 Ctrl+C 停止）" << std::endl;

        // 6. 启动事件循环
        EventLoop();
        return true;
    }

    // 停止服务器
    void Stop() {
        if (!m_isRunning) return;

        m_isRunning = false;
        std::cout << "\n服务器正在停止..." << std::endl;

        // 关闭监听套接字
        if (m_listenSock != INVALID_SOCKET) {
            closesocket(m_listenSock);
            m_listenSock = INVALID_SOCKET;
        }

        // 关闭所有客户端套接字
        std::lock_guard<std::mutex> lock(m_clientMutex);
        for (SOCKET clientSock : m_clientSocks) {
            if (clientSock != INVALID_SOCKET) {
                closesocket(clientSock);
            }
        }
        m_clientSocks.clear();

        // 清理 Winsock
        WSACleanup();
        std::cout << "服务器已停止" << std::endl;
    }

private:
    // 事件循环（核心逻辑）
    void EventLoop() {
        while (m_isRunning) {
            // 初始化套接字集合
            fd_set readSet;
            FD_ZERO(&readSet);

            // 将监听套接字加入读集合（监控新连接）
            FD_SET(m_listenSock, &readSet);

            // 将所有客户端套接字加入读集合（监控数据到达）
            std::lock_guard<std::mutex> lock(m_clientMutex);
            for (SOCKET clientSock : m_clientSocks) {
                FD_SET(clientSock, &readSet);
            }

            // 设置超时（NULL 表示无限等待）
            timeval timeout{};
            timeout.tv_sec = 1;  // 1秒超时（避免无限阻塞，方便退出）
            timeout.tv_usec = 0;

            // 确定最大套接字句柄（select 第一个参数需要）
            SOCKET maxSock = m_listenSock;
            for (SOCKET clientSock : m_clientSocks) {
                if (clientSock > maxSock) {
                    maxSock = clientSock;
                }
            }

            // 调用 select 等待事件（Windows 下第一个参数需 +1）
            int ret = select(static_cast<int>(maxSock) + 1, &readSet, nullptr, nullptr, &timeout);
            if (ret == SOCKET_ERROR) {
                std::cerr << "select 失败，错误码：" << WSAGetLastError() << std::endl;
                continue;
            }
            else if (ret == 0) {
                // 超时，继续循环（用于检测 m_isRunning 状态）
                continue;
            }

            // 处理监听套接字事件（新连接）
            if (FD_ISSET(m_listenSock, &readSet)) {
                HandleNewConnection();
                ret--;  // 减少剩余事件计数
                if (ret == 0) continue;  // 无其他事件，继续循环
            }

            // 处理客户端套接字事件（数据到达或断开）
            HandleClientEvents(readSet);
        }
    }

    // 处理新连接
    void HandleNewConnection() {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);

        // 接受新连接
        SOCKET clientSock = accept(m_listenSock,
            reinterpret_cast<SOCKADDR*>(&clientAddr),
            &clientAddrLen);
        if (clientSock == INVALID_SOCKET) {
            std::cerr << "接受连接失败，错误码：" << WSAGetLastError() << std::endl;
            return;
        }

        // 检查是否超过最大客户端数量
        std::lock_guard<std::mutex> lock(m_clientMutex);
        if (m_clientSocks.size() >= MAX_CLIENT_COUNT) {
            std::cerr << "客户端数量已达上限，拒绝连接" << std::endl;
            closesocket(clientSock);
            return;
        }

        // 获取客户端 IP 和端口
        char clientIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
        uint16_t clientPort = ntohs(clientAddr.sin_port);

        // 添加到客户端列表
        m_clientSocks.push_back(clientSock);
        std::cout << "新客户端连接：" << clientIp << ":" << clientPort
            << "（当前连接数：" << m_clientSocks.size() << "）" << std::endl;
    }

    // 处理客户端事件（数据到达或断开）
    void HandleClientEvents(const fd_set& readSet) {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        // 使用迭代器遍历，方便删除断开的连接
        for (auto it = m_clientSocks.begin(); it != m_clientSocks.end();) {
            SOCKET clientSock = *it;

            // 检查当前客户端是否有事件
            if (FD_ISSET(clientSock, &readSet)) {
                char buffer[MAX_BUFFER_SIZE];
                int recvLen = recv(clientSock, buffer, MAX_BUFFER_SIZE - 1, 0);

                if (recvLen > 0) {
                    // 接收数据成功
                    buffer[recvLen] = '\0';
                    std::cout << "收到客户端数据（sock=" << clientSock << "）：" << buffer << std::endl;

                    // 示例：回复客户端
                    std::string response = "服务器已收到：" + std::string(buffer);
                    send(clientSock, response.c_str(), response.length(), 0);
                }
                else {
                    // 连接断开或错误
                    if (recvLen == 0) {
                        std::cout << "客户端断开连接（sock=" << clientSock << "）" << std::endl;
                    }
                    else {
                        std::cerr << "接收数据失败（sock=" << clientSock << "），错误码：" << WSAGetLastError() << std::endl;
                    }

                    // 关闭套接字并从列表中移除
                    closesocket(clientSock);
                    
                    it = m_clientSocks.erase(it);  // 迭代器后移
                    std::cout << "当前连接数：" << m_clientSocks.size() << std::endl;
                    continue;  // 跳过后续自增
                }
            }
            ++it;  // 迭代器自增
        }
    }

    // 清理资源
    void Cleanup() {
        if (m_listenSock != INVALID_SOCKET) {
            closesocket(m_listenSock);
            m_listenSock = INVALID_SOCKET;
        }
        WSACleanup();
    }

private:
    SOCKET m_listenSock;                     // 监听套接字
    std::atomic<bool> m_isRunning;           // 服务器运行状态（线程安全）
    std::vector<SOCKET> m_clientSocks;       // 客户端套接字列表
    std::mutex m_clientMutex;                // 客户端列表互斥锁（线程安全）
};

int a() {
    SelectTcpServer server;
    if (!server.Start()) {
        std::cerr << "服务器启动失败" << std::endl;
        return 1;
    }
    return 0;
}