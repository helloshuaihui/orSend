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
				std::cout << "创建服务端句柄失败" << std::endl;
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
						std::cout << "获取端口失败" << std::endl;
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
	TcpSocketClass::TcpSocketClass():
		ErrorMsg(""),
		ErrorCode(0),
		IsPrintError(true)
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
		return info;
	}
	void TcpSocketClass::SetErrorMsg(std::string ErrorMsg, int ErrorCode)
	{
		this->ErrorMsg = ErrorMsg;
		this->ErrorCode = ErrorCode;
		this->PrintError();
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
}