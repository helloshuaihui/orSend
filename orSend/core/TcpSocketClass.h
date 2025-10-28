#pragma once
#ifndef TCPSOCKETCLASS_H
#define TCPSOCKETCLASS_H
#include <iostream>
#ifdef WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment(lib, "ws2_32.lib")
#endif // WIN32

namespace TCP {
	class TcpSocketClass
	{
	public:
		#ifdef WIN32
			bool InitWinSocket();
		#endif // !WIN32
		TcpSocketClass();
		~TcpSocketClass();

	private:

	};

}
#endif 