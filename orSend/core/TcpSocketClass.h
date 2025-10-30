#pragma once
#ifndef TCPSOCKETCLASS_H
#define TCPSOCKETCLASS_H
#include <iostream>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#ifdef WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment(lib, "ws2_32.lib")
	using TCPSOCK = SOCKET;
#endif // WIN32

namespace TCP {
	enum SocketType
	{
		server = 1,
		client = 2
	};
	struct TcpSocketInfo
	{
		TCPSOCK sockId; //socket id
		std::string ip; //ip
		int port; //port
		std::string connTime; //连接时间
		bool connStatus; //当前状态 true and false
		int type; //socket 类型
	};
	class TcpSocketClass
	{
	public:
		#ifdef WIN32
		TCPSOCK creatTcpScoketserver(std::string ip,int port);
		TCPSOCK connTcpScokerServer(std::string ip, int port);
		#endif // !WIN32
		void PrintSocketPool();
		std::string ErrorMsg; //错误信息
		int ErrorCode; //错误代码
		bool IsPrintError; //是否打印错误
		void PrintError(); //错误打印函数
		TcpSocketClass();
		~TcpSocketClass();
	private:	
		std::string getCurrentTimeString();
		TcpSocketInfo InitTCPSOCKINFO(std::string ip,int port, TCPSOCK sock,int type);
		std::vector<TcpSocketInfo> SocketPool;
		void SetErrorMsg(std::string ErrorMsg,int ErrorCode); //设置错误信息

		#ifdef WIN32
				bool InitWinSocket();
		#endif // !WIN32
	};

}
#endif 