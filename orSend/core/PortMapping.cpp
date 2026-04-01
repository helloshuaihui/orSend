#include "PortMapping.h"

namespace TCP {

	PortMapping::PortMapping()
		: IsPrintError(true)
		, ErrorMsg("")
		, ErrorCode(0)
	{
	}

	PortMapping::~PortMapping()
	{
		threadPool.Shutdown();
		// 关闭所有服务器连接
		std::lock_guard<std::mutex> lock(poolMutex);
		for (auto& conn : serverConnPool) {
			if (conn.sock != -1) {
				closesocket(conn.sock);
			}
		}
		// 关闭所有监听套接字
		for (auto& listen : localListenPool) {
			if (listen.listenSock != -1) {
				closesocket(listen.listenSock);
			}
		}
	}

	ThreadPool& PortMapping::GetThreadPool()
	{
		return threadPool;
	}

	void PortMapping::PrintError()
	{
		if (IsPrintError && !ErrorMsg.empty()) {
			std::cout << "[PORTMAPPING ERROR] " << ErrorMsg << " 错误代码:" << ErrorCode << std::endl;
		}
	}

	void PortMapping::SetErrorMsg(const std::string& msg, int code)
	{
		ErrorMsg = msg;
		ErrorCode = code;
		PrintError();
	}

	ServerConnInfo PortMapping::InitServerConnInfo(const std::string& ip, int port, TCPSOCK sock)
	{
		ServerConnInfo info;
		info.sock = sock;
		info.serverIp = ip;
		info.serverPort = port;
		info.isInUse = false;
		info.bindLocalSock = -1;
		return info;
	}

	LocalListenInfo PortMapping::InitLocalListenInfo(const std::string& ip, int port, int preConnCount)
	{
		LocalListenInfo info;
		info.ip = ip;
		info.port = port;
		info.listenSock = -1;
		info.preConnCount = preConnCount;
		return info;
	}

	SockBindInfo PortMapping::InitSockBindInfo(TCPSOCK localSock, TCPSOCK serverSock)
	{
		SockBindInfo info;
		info.localSock = localSock;
		info.serverSock = serverSock;
		return info;
	}

	bool PortMapping::AddServerInfo(const std::string& ip, int port)
	{
		serverInfoList.push_back(std::make_pair(ip, port));
		return true;
	}

	bool PortMapping::AddLocalListenInfo(const std::string& ip, int port, int preConnCount)
	{
		LocalListenInfo info = InitLocalListenInfo(ip, port, preConnCount);
		localListenPool.push_back(info);
		return true;
	}

	bool PortMapping::CreatePreConnections(LocalListenInfo& listenInfo)
	{
		if (serverInfoList.empty()) {
			SetErrorMsg("没有添加服务器信息", -1);
			return false;
		}

		std::lock_guard<std::mutex> lock(poolMutex);

		// 使用第一个服务器创建预连接
		std::string serverIp = serverInfoList[0].first;
		int serverPort = serverInfoList[0].second;

		for (int i = 0; i < listenInfo.preConnCount; i++) {
			TCPSOCK sock = connTcpScokerServer(serverIp, serverPort);
			if (sock != -1) {
				ServerConnInfo connInfo = InitServerConnInfo(serverIp, serverPort, sock);
				serverConnPool.push_back(connInfo);
				std::cout << "创建预连接成功: " << serverIp << ":" << serverPort << " sock=" << sock << std::endl;
			}
			else {
				SetErrorMsg("创建预连接失败", -2);
			}
		}

		return true;
	}

	ServerConnInfo* PortMapping::GetAvailableServerConn()
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		for (auto& conn : serverConnPool) {
			if (!conn.isInUse && conn.sock != -1) {
				conn.isInUse = true;
				return &conn;
			}
		}

		// 如果没有可用连接，尝试创建新连接
		if (!serverInfoList.empty()) {
			std::string serverIp = serverInfoList[0].first;
			int serverPort = serverInfoList[0].second;
			TCPSOCK sock = connTcpScokerServer(serverIp, serverPort);
			if (sock != -1) {
				ServerConnInfo connInfo = InitServerConnInfo(serverIp, serverPort, sock);
				connInfo.isInUse = true;
				serverConnPool.push_back(connInfo);
				std::cout << "动态创建新连接: " << serverIp << ":" << serverPort << " sock=" << sock << std::endl;
				return &serverConnPool.back();
			}
		}

		return nullptr;
	}

	bool PortMapping::ReturnServerConnToPool(TCPSOCK serverSock)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		for (auto& conn : serverConnPool) {
			if (conn.sock == serverSock) {
				conn.isInUse = false;
				conn.bindLocalSock = -1;
				std::cout << "连接放回连接池: sock=" << serverSock << std::endl;
				return true;
			}
		}

		return false;
	}

	bool PortMapping::RemoveServerConn(TCPSOCK serverSock)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		for (auto it = serverConnPool.begin(); it != serverConnPool.end(); ++it) {
			if (it->sock == serverSock) {
				if (it->sock != -1) {
					closesocket(it->sock);
				}
				serverConnPool.erase(it);
				std::cout << "移除服务器连接: sock=" << serverSock << std::endl;
				return true;
			}
		}

		return false;
	}

	SockBindInfo* PortMapping::SearchBindInfoByLocalSock(TCPSOCK localSock)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		for (auto& bind : sockBindPool) {
			if (bind.localSock == localSock) {
				return &bind;
			}
		}

		return nullptr;
	}

	SockBindInfo* PortMapping::SearchBindInfoByServerSock(TCPSOCK serverSock)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		for (auto& bind : sockBindPool) {
			if (bind.serverSock == serverSock) {
				return &bind;
			}
		}

		return nullptr;
	}

	bool PortMapping::RemoveBindInfo(TCPSOCK localSock)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		for (auto it = sockBindPool.begin(); it != sockBindPool.end(); ++it) {
			if (it->localSock == localSock) {
				sockBindPool.erase(it);
				return true;
			}
		}

		return false;
	}

	TCPSOCK PortMapping::ReconnectServer(const std::string& ip, int port)
	{
		return connTcpScokerServer(ip, port);
	}

	void PortMapping::ForwardData(TCPSOCK fromSock, TCPSOCK toSock)
	{
		char buffer[TCPMAXBUFSIZE];
		while (true) {
			int recv_len = recv(fromSock, buffer, sizeof(buffer), 0);
			if (recv_len <= 0) {
				break;
			}
			int send_len = send(toSock, buffer, recv_len, 0);
			if (send_len <= 0) {
				break;
			}
		}
		closesocket(fromSock);
		closesocket(toSock);
	}

	bool PortMapping::StartPortMapping()
	{
		if (serverInfoList.empty()) {
			std::cout << "并未添加服务器数据" << std::endl;
			return false;
		}
		if (localListenPool.empty()) {
			std::cout << "并未添加本地监听的IP以及端口数据" << std::endl;
			return false;
		}

		// 为每个本地监听创建预连接
		for (auto& listenInfo : localListenPool) {
			CreatePreConnections(listenInfo);
		}

		// 启动本地监听
		for (auto& listenInfo : localListenPool) {
			try {
				threadPool.AddTask([this, listenInfo]() -> void {
					TCPSOCK sock = creatTcpScoketserver(listenInfo.ip, listenInfo.port);
					if (sock != -1) {
						StartServer(sock);
					}
				});
			}
			catch (const std::exception& e) {
				std::cout << "添加监听任务失败: " << e.what() << std::endl;
			}
		}

		return true;
	}

	void PortMapping::OnServerConn(TCPSOCK sock)
	{
		std::cout << "新的本地连接: sock=" << sock << std::endl;

		// 从连接池中获取一个可用的服务器连接
		ServerConnInfo* serverConn = GetAvailableServerConn();
		if (serverConn == nullptr) {
			SetErrorMsg("没有可用的服务器连接", -3);
			closesocket(sock);
			return;
		}

		serverConn->bindLocalSock = sock;

		// 添加绑定信息
		{
			std::lock_guard<std::mutex> lock(poolMutex);
			sockBindPool.push_back(InitSockBindInfo(sock, serverConn->sock));
		}

		std::cout << "绑定连接: 本地sock=" << sock << " 服务器sock=" << serverConn->sock << std::endl;

		// 启动数据转发
		try {
			threadPool.AddTask([this, sock, serverSock = serverConn->sock]() {
				ForwardData(sock, serverSock);
			});
			threadPool.AddTask([this, sock, serverSock = serverConn->sock]() {
				ForwardData(serverSock, sock);
			});
		}
		catch (const std::exception& e) {
			std::cout << "添加转发任务失败: " << e.what() << std::endl;
		}
	}

	void PortMapping::OnServerMessage(TCPSOCK sock, std::string buf)
	{
		SockBindInfo* bindInfo = SearchBindInfoByLocalSock(sock);
		if (bindInfo == nullptr) {
			std::cout << "收到本地消息但未绑定服务器连接: sock=" << sock << std::endl;
			return;
		}

		send(bindInfo->serverSock, buf.c_str(), buf.size(), 0);
	}

	void PortMapping::OnServerClose(TCPSOCK sock)
	{
		std::cout << "本地连接关闭: sock=" << sock << std::endl;

		SockBindInfo* bindInfo = SearchBindInfoByLocalSock(sock);
		if (bindInfo != nullptr) {
			TCPSOCK serverSock = bindInfo->serverSock;

			// 将服务器连接放回连接池
			ReturnServerConnToPool(serverSock);

			// 移除绑定信息
			RemoveBindInfo(sock);

			std::cout << "本地连接关闭，服务器连接已放回池: 服务器sock=" << serverSock << std::endl;
		}
		else {
			closesocket(sock);
		}
	}

	void PortMapping::OnClientMessage(TCPSOCK sock, std::string buf)
	{
		SockBindInfo* bindInfo = SearchBindInfoByServerSock(sock);
		if (bindInfo == nullptr) {
			std::cout << "收到服务器消息但未绑定本地连接: sock=" << sock << std::endl;
			return;
		}

		send(bindInfo->localSock, buf.c_str(), buf.size(), 0);
	}

	void PortMapping::OnClientClose(TCPSOCK sock)
	{
		std::cout << "服务器连接关闭: sock=" << sock << std::endl;

		SockBindInfo* bindInfo = SearchBindInfoByServerSock(sock);
		if (bindInfo != nullptr) {
			TCPSOCK localSock = bindInfo->localSock;

			// 移除绑定信息
			RemoveBindInfo(localSock);

			// 移除旧的服务器连接
			RemoveServerConn(sock);

			// 尝试重新连接服务器并分配
			if (!serverInfoList.empty()) {
				std::string serverIp = serverInfoList[0].first;
				int serverPort = serverInfoList[0].second;
				TCPSOCK newServerSock = ReconnectServer(serverIp, serverPort);

				if (newServerSock != -1) {
					// 添加新连接到池
					{
						std::lock_guard<std::mutex> lock(poolMutex);
						ServerConnInfo newConn = InitServerConnInfo(serverIp, serverPort, newServerSock);
						newConn.isInUse = true;
						newConn.bindLocalSock = localSock;
						serverConnPool.push_back(newConn);
					}

					// 重新绑定
					{
						std::lock_guard<std::mutex> lock(poolMutex);
						sockBindPool.push_back(InitSockBindInfo(localSock, newServerSock));
					}

					std::cout << "服务器连接断开，已重新连接并重新绑定: 新sock=" << newServerSock << std::endl;

					// 启动新的数据转发
					try {
						threadPool.AddTask([this, localSock, newServerSock]() {
							ForwardData(localSock, newServerSock);
						});
						threadPool.AddTask([this, localSock, newServerSock]() {
							ForwardData(newServerSock, localSock);
						});
					}
					catch (const std::exception& e) {
						std::cout << "添加重新转发任务失败: " << e.what() << std::endl;
					}
				}
				else {
					std::cout << "服务器连接断开，重新连接失败" << std::endl;
					closesocket(localSock);
				}
			}
		}
		else {
			// 如果没有绑定，直接移除连接
			RemoveServerConn(sock);
		}
	}

}
