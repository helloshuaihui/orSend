// orSend.cpp: 定义应用程序的入口点。
//

//#include "orSend.h"
//
//using namespace std;
//int main()
//{
//	TCP::PortMapping test;
//	test.AddServerBasicInfoPool((std::string)"127.0.0.1",3306);
//	test.AddLocalBasicInfoPool((std::string)"0.0.0.0", 8000);
//	test.StratPortMapping();
//	Sleep(1000000);
//	return 0;
//}

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <string>
#include <WinSock2.h>

#pragma comment(lib, "ws2_32.lib") // 链接Winsock库

// 初始化Winsock
bool InitWinsock() {
    WSADATA wsaData;
    int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (ret != 0) {
        std::cerr << "WSAStartup failed: " << ret << std::endl;
        return false;
    }
    return true;
}

// 清理Winsock
void CleanupWinsock() {
    WSACleanup();
}

// 日志输出
void Log(const std::string& msg) {
    std::cout << "[" << std::this_thread::get_id() << "] " << msg << std::endl;
}

// 错误处理（输出Winsock错误码）
void LogError(const std::string& msg) {
    int err = WSAGetLastError();
    std::cerr << "[" << std::this_thread::get_id() << "] " << msg << " (Error: " << err << ")" << std::endl;
}

// 双向数据转发（核心函数：从from_sock读取数据并转发到to_sock）
void ForwardData(SOCKET from_sock, SOCKET to_sock, const std::string& direction) {
    char buffer[4096]; // 数据缓冲区
    while (true) {
        // 从from_sock接收数据
        int recv_len = recv(from_sock, buffer, sizeof(buffer), 0);
        if (recv_len <= 0) {
            LogError(direction + " 接收数据失败或连接关闭");
            break;
        }
        // 转发到to_sock
        int send_len = send(to_sock, buffer, recv_len, 0);
        if (send_len <= 0) {
            LogError(direction + " 发送数据失败");
            break;
        }
        Log(direction + " 转发 " + std::to_string(send_len) + " 字节");
    }
    // 关闭套接字（触发另一端退出）
    closesocket(from_sock);
    closesocket(to_sock);
}   

// 处理单个客户端连接：连接目标服务器并启动双向转发
void HandleClient(SOCKET client_sock, const std::string& target_ip, int target_port) {
    Log("新客户端连接，尝试连接目标服务器 " + target_ip + ":" + std::to_string(target_port));

    // 1. 连接目标服务器（被转发的终点）
    SOCKET target_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (target_sock == INVALID_SOCKET) {
        LogError("创建目标服务器套接字失败");
        closesocket(client_sock);
        return;
    }

    sockaddr_in target_addr;
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(target_port);
    // 转换目标IP为二进制格式
    if (inet_pton(AF_INET, target_ip.c_str(), &target_addr.sin_addr) <= 0) {
        LogError("目标IP格式错误");
        closesocket(target_sock);
        closesocket(client_sock);
        return;
    }

    // 连接目标服务器
    if (connect(target_sock, (SOCKADDR*)&target_addr, sizeof(target_addr)) == SOCKET_ERROR) {
        LogError("连接目标服务器失败");
        closesocket(target_sock);
        closesocket(client_sock);
        return;
    }
    Log("已连接目标服务器，开始双向转发...");

    // 2. 启动双向转发线程
    // 线程1：客户端 → 目标服务器
    std::thread t1(ForwardData, client_sock, target_sock, "客户端→目标");
    // 线程2：目标服务器 → 客户端
    std::thread t2(ForwardData, target_sock, client_sock, "目标→客户端");

    // 等待转发线程结束（连接断开时）
    t1.join();
    t2.join();

    Log("客户端连接已关闭");
}

int main() {
    // 配置参数（可根据需要修改）
    const std::string listen_ip = "0.0.0.0"; // 监听所有本地IP
    const int listen_port = 8080;            // 入口端口（外部客户端连接此端口）
    const std::string target_ip = "127.0.0.1"; // 目标服务器IP（被转发的终点）
    const int target_port = 3306;              // 目标服务器端口

    // 初始化Winsock
    if (!InitWinsock()) {
        return 1;
    }

    // 创建监听套接字
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        LogError("创建监听套接字失败");
        CleanupWinsock();
        return 1;
    }

    // 绑定监听地址和端口
    sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(listen_port);
    if (inet_pton(AF_INET, listen_ip.c_str(), &listen_addr.sin_addr) <= 0) {
        LogError("监听IP格式错误");
        closesocket(listen_sock);
        CleanupWinsock();
        return 1;
    }

    if (bind(listen_sock, (SOCKADDR*)&listen_addr, sizeof(listen_addr)) == SOCKET_ERROR) {
        LogError("绑定监听端口 " + std::to_string(listen_port) + " 失败（可能端口已被占用）");
        closesocket(listen_sock);
        CleanupWinsock();
        return 1;
    }

    // 开始监听（最大等待连接数为5）
    if (listen(listen_sock, 5) == SOCKET_ERROR) {
        LogError("监听端口失败");
        closesocket(listen_sock);
        CleanupWinsock();
        return 1;
    }

    Log("端口转发程序启动成功！");
    Log("监听：" + listen_ip + ":" + std::to_string(listen_port));
    Log("转发到：" + target_ip + ":" + std::to_string(target_port));
    Log("等待客户端连接...（按Ctrl+C退出）");

    // 循环接收客户端连接
    while (true) {
        SOCKET client_sock = accept(listen_sock, NULL, NULL);
        if (client_sock == INVALID_SOCKET) {
            LogError("接收客户端连接失败");
            continue;
        }
        Log("新客户端已连接");

        // 启动线程处理该客户端（避免阻塞主循环，支持多客户端）
        std::thread client_thread(HandleClient, client_sock, target_ip, target_port);
        client_thread.detach(); // 分离线程，自动回收资源
    }

    // 清理资源（实际不会执行，需手动终止程序）
    closesocket(listen_sock);
    CleanupWinsock();
    return 0;
}
