#pragma once
#ifndef PORTMAPPING_H
#define PORTMAPPING_H
#include <iostream>
#include <vector>
#include <queue>
#include <mutex>
#include "TcpSocketClass.h"
#include "ThreadPool.h"

namespace TCP {
	// 端口映射运行模式
	enum class PortMappingRunType : int
	{
		Server = 1, // 服务器模式
		Client = 2  // 客户端模式
	};

	// 服务器连接池信息
	struct ServerConnInfo
	{
		TCPSOCK sock;            // 服务器连接套接字
		std::string serverIp;    // 服务器IP
		int serverPort;          // 服务器端口
		bool isInUse;            // 是否正在使用
		TCPSOCK bindLocalSock;   // 绑定的本地套接字
	};

	// 本地监听信息
	struct LocalListenInfo
	{
		std::string ip;          // 本地监听IP
		int port;                 // 本地监听端口
		TCPSOCK listenSock;      // 监听套接字
		int preConnCount;         // 预连接数量
	};

	// 套接字绑定信息
	struct SockBindInfo
	{
		TCPSOCK localSock;       // 本地套接字
		TCPSOCK serverSock;      // 服务器套接字
	};

	class PortMapping : public TcpSocketClass
	{
	public:
		PortMapping();
		~PortMapping();

		// 添加服务器信息
		bool AddServerInfo(const std::string& ip, int port);
		// 添加本地监听端口，并指定预连接数量
		bool AddLocalListenInfo(const std::string& ip, int port, int preConnCount = 5);
		// 开始端口映射
		bool StartPortMapping();
		// 获取线程池引用
		ThreadPool& GetThreadPool();

		// 错误处理
		bool IsPrintError;
		std::string ErrorMsg;
		int ErrorCode;

	private:
		// 打印错误
		void PrintError();
		// 设置错误信息
		void SetErrorMsg(const std::string& msg, int code);
		// 初始化服务器连接信息
		ServerConnInfo InitServerConnInfo(const std::string& ip, int port, TCPSOCK sock);
		// 初始化本地监听信息
		LocalListenInfo InitLocalListenInfo(const std::string& ip, int port, int preConnCount);
		// 初始化套接字绑定信息
		SockBindInfo InitSockBindInfo(TCPSOCK localSock, TCPSOCK serverSock);
		// 创建预连接到服务器
		bool CreatePreConnections(LocalListenInfo& listenInfo);
		// 从连接池中获取一个可用的服务器连接
		ServerConnInfo* GetAvailableServerConn();
		// 将服务器连接放回连接池
		bool ReturnServerConnToPool(TCPSOCK serverSock);
		// 删除服务器连接
		bool RemoveServerConn(TCPSOCK serverSock);
		// 查找绑定信息（通过本地套接字）
		SockBindInfo* SearchBindInfoByLocalSock(TCPSOCK localSock);
		// 查找绑定信息（通过服务器套接字）
		SockBindInfo* SearchBindInfoByServerSock(TCPSOCK serverSock);
		// 删除绑定信息
		bool RemoveBindInfo(TCPSOCK localSock);
		// 数据转发函数
		void ForwardData(TCPSOCK fromSock, TCPSOCK toSock);
		// 重新连接服务器
		TCPSOCK ReconnectServer(const std::string& ip, int port);

		// 重写TcpSocketClass的回调函数
		void OnServerConn(TCPSOCK sock) override;
		void OnServerMessage(TCPSOCK sock, std::string buf) override;
		void OnServerClose(TCPSOCK sock) override;
		void OnClientMessage(TCPSOCK sock, std::string buf) override;
		void OnClientClose(TCPSOCK sock) override;

	private:
		ThreadPool threadPool;                   // 线程池
		std::vector<ServerConnInfo> serverConnPool;  // 服务器连接池
		std::vector<LocalListenInfo> localListenPool; // 本地监听池
		std::vector<SockBindInfo> sockBindPool; // 套接字绑定池
		std::mutex poolMutex;                    // 连接池互斥锁
		std::vector<std::pair<std::string, int>> serverInfoList; // 服务器信息列表
	};
}
#endif // !PORTMAPPING_H
