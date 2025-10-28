#include "TcpSocketClass.h"
namespace TCP {
	#ifdef WIN32
		bool TcpSocketClass::InitWinSocket()
		{
			WSADATA wsaData;
			int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
			if (result != 0) {
				std::cout << "WSAStartup 初始化失败：" << result << std::endl;
				return false;
			}
			else {
				std::cout << "WSAStartup 初始化成功：" << result << std::endl;
			}
			return true;
		}
	#endif // WIN32

	
	TcpSocketClass::TcpSocketClass()
	{
		#ifdef WIN32
			InitWinSocket();
		#endif // WIN32

		std::cout << "欢迎使用tcp服务器" << std::endl;
	}

	TcpSocketClass::~TcpSocketClass()
	{

	}
}