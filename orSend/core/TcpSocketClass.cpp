#include "TcpSocketClass.h"
namespace TCP {
	#ifdef WIN32
		bool TcpSocketClass::InitWinSocket()
		{
			WSADATA wsaData;
			int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
			if (result != 0) {
				SetErrorMsg("WSAStartup 初始化失败", result);
				return false;
			}
			else {
				std::cout << "WSAStartup 初始化成功,状态：" << result << std::endl;
			}
			return true;
		}
		TCPSOCK TcpSocketClass::creatTcpScoketserver(std::string ip, int port)
		{
			TCPSOCK NewSocket = socket(AF_INET, SOCK_STREAM, 0);
			if (INVALID_SOCKET == NewSocket)
			{
				SetErrorMsg("创建服务端句柄失败", WSAGetLastError());
				WSACleanup();
				return -1;
			}
			else {
				SOCKADDR_IN addr = {};
				addr.sin_family = AF_INET;
				addr.sin_port = htons(port);		// 端口号
				addr.sin_addr.S_un.S_addr = inet_addr(ip.c_str());   //ip地址
				if (bind(NewSocket, (sockaddr*)&addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
				{
					SetErrorMsg("绑定端口号失败", WSAGetLastError());
					closesocket(NewSocket);
					return -1;
				}
				else {
					std::cout << "socket创建成功" << std::endl;
					SocketPool.push_back(InitTCPSOCKINFO(ip, port, NewSocket, SocketType::server));
				}
			}
			return NewSocket;
		}
		TCPSOCK TcpSocketClass::connTcpScokerServer(std::string ip, int port)
		{
			TCPSOCK NewSocket = socket(AF_INET, SOCK_STREAM, 0);
			if (INVALID_SOCKET == NewSocket)
			{
				SetErrorMsg("创建客户端句柄失败", WSAGetLastError());
				return -1;
			}
			else {
				sockaddr_in serverAddr{};
				serverAddr.sin_family = AF_INET;
				serverAddr.sin_port = htons(port);
				if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) != 1) {
					SetErrorMsg("IP 地址解析失败", WSAGetLastError());
					return -1;
				}
				// 4. 连接服务器
				if (connect(NewSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
					SetErrorMsg("连接服务器失败", WSAGetLastError());
					return -1;
				}
				else {
					std::cout << "连接服务器成功"<< std::endl;
					//获取端口
					int LocalPort = -1;
					sockaddr_in localAddr{};
					int addrLen = sizeof(localAddr);
					if (getsockname(NewSocket, (SOCKADDR*)&localAddr, &addrLen) == SOCKET_ERROR) {
						SetErrorMsg("获取端口失败", WSAGetLastError());
					}
					else {
						LocalPort = ntohs(localAddr.sin_port);
					}
					SocketPool.push_back(InitTCPSOCKINFO(ip, LocalPort, NewSocket,SocketType::client));
				}
			}
			return NewSocket;
		}
	#endif // WIN32
	void TcpSocketClass::PrintError()
	{
		if (this->IsPrintError) {
			std::cout <<"[ERROR]" << this->ErrorMsg << " 错误代码:" << this->ErrorCode << std::endl;
		}
	}
	void TcpSocketClass::OnServerConn(TCPSOCK sock)
	{
		PrintSocketPool();
	}
	void TcpSocketClass::OnServerMessage(TCPSOCK sock, std::string buf)
	{
	}
	void TcpSocketClass::OnServerClose(TCPSOCK sock)
	{
		PrintSocketPool();
	}
	void TcpSocketClass::OnClientMessage(TCPSOCK sock, std::string buf){}
	void TcpSocketClass::OnClientClose(TCPSOCK sock)
	{
		std::cout << "来自 " << sock << "的连接断开" << std::endl;
		PrintSocketPool();
	}
	TcpSocketClass::TcpSocketClass():
		ErrorMsg(""),
		ErrorCode(0),
		IsPrintError(true),
		MaxListenNum(64),
		isListenMsgEvents(false)
	{
		#ifdef WIN32
			InitWinSocket();
		#endif // WIN32
		std::cout << "欢迎使用tcp服务器" << std::endl;
	}
	TcpSocketClass::~TcpSocketClass()
	{
		#ifdef WIN32
			WSACleanup();
		#endif // WIN32
	}
	std::string TcpSocketClass::getCurrentTimeString()
	{
		// 获取当前系统时间（UTC时间转换为本地时间）
		auto now = std::chrono::system_clock::now();
		std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
		std::tm localTime;
		// 跨平台处理线程安全的本地时间转换
		#ifdef _WIN32
			// Windows使用localtime_s（线程安全）
			localtime_s(&localTime, &nowTime);
		#else
			// Linux使用localtime_r（线程安全）
			localtime_r(&nowTime, &localTime);
		#endif

		// 格式化时间为字符串
		std::stringstream ss;
		ss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
		return ss.str();
	}
	TcpSocketInfo TcpSocketClass::InitTCPSOCKINFO(std::string ip, int port, TCPSOCK sock,int type)
	{
		TcpSocketInfo info;
		info.ip = std::move(ip);  // 使用move减少拷贝
		info.port = port; //端口
		info.sockId = sock;  // 复用之前定义的无效句柄
		info.connTime = getCurrentTimeString(); //获取当前系统时间
		info.connStatus = true; //连接状态 默认true
		info.type = type; //定义socket连接类型
		info.EventLoopStatu = false; //默认未监听
		return info;
	}
	void TcpSocketClass::SetErrorMsg(std::string ErrorMsg, int ErrorCode)
	{
		this->ErrorMsg = ErrorMsg;
		this->ErrorCode = ErrorCode;
		this->PrintError();
	}
	bool TcpSocketClass::ListenServerSocket(TcpSocketInfo& ServerSockt, int MaxConn)
	{
		int SSocket = ServerSockt.sockId;
		if (listen(SSocket, MaxConn) == SOCKET_ERROR) {
			std::cerr << "监听失败，错误码：" << WSAGetLastError() << std::endl;
			SetErrorMsg("监听失败", WSAGetLastError());
			closesocket(SSocket);
			RemoveTcpSocketInfo(SSocket);
			return false;
		}
		else {
			ServerSockt.EventLoopStatu = true;
			std::vector<TCPSOCK> TmpSocketPool;
			while (ServerSockt.EventLoopStatu) {
				fd_set readSet{};
				FD_ZERO(&readSet);
				// 将监听套接字加入读集合（监控新连接）
				FD_SET(SSocket, &readSet);
				for (TCPSOCK TmpSocket : TmpSocketPool) {
					FD_SET(TmpSocket, &readSet);
				}
				// 设置超时（NULL 表示无限等待）
				timeval timeout{};
				timeout.tv_sec = 1;  // 1秒超时（避免无限阻塞，方便退出）
				timeout.tv_usec = 0;
				TCPSOCK maxSock = SSocket;
				for (TCPSOCK clientSock : TmpSocketPool) {
					if (clientSock > maxSock) {
						maxSock = clientSock;
					}
				}
				int ret = select(maxSock + 1, &readSet, nullptr, nullptr, &timeout);
				if (ret == SOCKET_ERROR) {
					SetErrorMsg("select 失败", WSAGetLastError());
					continue;
				}else if (ret == 0) {
					// 超时，继续循环
					continue;
				}
				// 处理监听套接字事件（新连接）
				if (FD_ISSET(SSocket, &readSet)) {
					HandleNewConnection(SSocket, TmpSocketPool);
					ret--;  // 减少剩余事件计数
					if (ret == 0) continue;  // 无其他事件，继续循环
				}
				//处理消息事件
				HandleClientEvents(readSet, TmpSocketPool);
			}
		}
		return false;
	}
	bool TcpSocketClass::ListenClientSocket(TcpSocketInfo& ClientSockt)
	{
		TCPSOCK SSocket = ClientSockt.sockId;  // 传入的socket句柄
		ClientSockt.EventLoopStatu = true;
		while (ClientSockt.EventLoopStatu)
		{
			char buffer[TCPMAXBUFSIZE] = { 0 };
			std::string buff;
			int recvLen = recv(SSocket, buffer, TCPMAXBUFSIZE, 0);
			buff = buffer;
			if (buff.size() > 0) {
				// 接收数据成功
				OnClientMessage(SSocket, buff);
			}
			else {
				// 连接断开或错误
				if (recvLen == 0) {
				}
				else {
					//打印错误消息
					SetErrorMsg("接收数据失败", WSAGetLastError());
				}
				// 关闭套接字并从列表中移除
				ClientSockt.EventLoopStatu = false; //停止循环 只是从池塘移除了并未释放所以得清除
				closesocket(SSocket);
				RemoveTcpSocketInfo(SSocket); //从池塘删除 
				OnClientClose(SSocket);
				return false;
			}
		}
		return false;
	}
	bool TcpSocketClass::RemoveTcpSocketInfo(TCPSOCK DeleteSockId)
	{
		std::unique_lock<std::mutex> look(socketPoolMutex);
		for (int i = 0; i < SocketPool.size(); i++) {
			if (DeleteSockId == SocketPool.at(i).sockId) {
				SocketPool.erase(SocketPool.begin()+i);
				return true;
			}
		}
		return false;
	}
	bool TcpSocketClass::HandleNewConnection(TCPSOCK ServerSocket,std::vector<TCPSOCK> &SockPool)
	{
		sockaddr_in clientAddr{};
		int clientAddrLen = sizeof(clientAddr);
		TCPSOCK clientSock = accept(ServerSocket,
			reinterpret_cast<SOCKADDR*>(&clientAddr),
			&clientAddrLen);
		if (clientSock == INVALID_SOCKET) {
			SetErrorMsg("接收客户端连接失败", WSAGetLastError());
			return false;
		}
		char clientIp[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
		int clientPort = ntohs(clientAddr.sin_port);
		if (isListenMsgEvents) {
			//判断是否加入事件监听
			SockPool.push_back(clientSock);
		}
		std::unique_lock<std::mutex> look(socketPoolMutex);
		SocketPool.push_back(InitTCPSOCKINFO(std::string(clientIp), clientPort, clientSock, SocketType::LocalClient));
		look.unlock();
		OnServerConn(clientSock);
		return true;
	}
	bool TcpSocketClass::HandleClientEvents(fd_set& readSet, std::vector<TCPSOCK>& SockPool)
	{
		for (int i = 0; i < SockPool.size();i++) {
			TCPSOCK clientSock = SockPool.at(i);
			if (FD_ISSET(clientSock, &readSet)) {
				char buffer[TCPMAXBUFSIZE] = {0};
				std::string buff;
				int recvLen = recv(clientSock, buffer, TCPMAXBUFSIZE, 0);
				buff = buffer;
				if (buff.size() > 0) {
					// 接收数据成功
					OnServerMessage(clientSock, buff);
				}
				else {
					// 连接断开或错误
					if (recvLen == 0) {
						
					}
					else {
						//打印错误消息
						SetErrorMsg("接收数据失败", WSAGetLastError());
					}
					// 关闭套接字并从列表中移除
					closesocket(clientSock);
					SockPool.erase(SockPool.begin() + i); //删除一个套接字
					RemoveTcpSocketInfo(clientSock); //从池塘删除
					OnServerClose(clientSock);
				}
			}
		}
		return false;
	}
	TcpSocketInfo* TcpSocketClass::GetSockInfo(TCPSOCK sock)
	{
		std::unique_lock<std::mutex> look(socketPoolMutex);
		for (int i = 0; i < SocketPool.size();i++) {
			if (sock == SocketPool.at(i).sockId) {
				return  &SocketPool.at(i);
			}
		}
		return nullptr;
	}
	void TcpSocketClass::PrintSocketPool()
	{
		std::cout << "开始打印socket当前socket数据" << std::endl;
		for (int i = 0;i < this->SocketPool.size();i++) {
			std::cout
				<< "socket:" << this->SocketPool[i].sockId << "\n"
				<< "ip:" << this->SocketPool[i].ip << "\n"
				<< "port:" << this->SocketPool[i].port << "\n"
				<< "statu:" << this->SocketPool[i].connStatus << "\n"
				<< "ctime:" << this->SocketPool[i].connTime << "\n"
				<< "type:" << ((this->SocketPool[i].type == 1) ? "server" : "client") << "\n"
				<< std::endl;
		}
	}
	bool TcpSocketClass::StartServer(TCPSOCK sock)
	{
		TcpSocketInfo *SockInfo = GetSockInfo(sock);
		if (SockInfo == nullptr) {
			SetErrorMsg("未找到该socket id信息",0);
			return false;
		}
		else {
			ListenServerSocket(*SockInfo, MaxListenNum);
		}
	}
	bool TcpSocketClass::StartClient(TCPSOCK sock)
	{
		TcpSocketInfo* SockInfo = GetSockInfo(sock);
		if (SockInfo == nullptr) {
			SetErrorMsg("未找到该socket id信息", 0);
			return false;
		}
		else {
			ListenClientSocket(*SockInfo);
		}
		return false;
	}
}