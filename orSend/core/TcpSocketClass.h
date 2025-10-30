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
#include <mutex>

namespace TCP {
	enum SocketType
	{
		server = 1,
		client = 2,
		LocalClient = 3
	};
	struct TcpSocketInfo
	{
		TCPSOCK sockId; //socket id
		std::string ip; //ip
		int port; //port
		std::string connTime; //连接时间
		bool connStatus; //当前状态 true and false
		int type; //socket 类型
		int EventLoopStatu; //是否开始监听
	};
	class TcpSocketClass
	{
	public:
		#ifdef WIN32
		TCPSOCK creatTcpScoketserver(std::string ip,int port);
		TCPSOCK connTcpScokerServer(std::string ip, int port);
		#endif // !WIN32
		void PrintSocketPool(); //打印socket信息池
		std::string ErrorMsg; //错误信息
		int ErrorCode; //错误代码
		bool IsPrintError; //是否打印错误
		void PrintError(); //错误打印函数
		TcpSocketClass();
		~TcpSocketClass();
	private:	
		std::mutex socketPoolMutex; // 保护 SocketPool 的线程安全
		std::string getCurrentTimeString(); //获取当前时间
		TcpSocketInfo InitTCPSOCKINFO(std::string ip,int port, TCPSOCK sock,int type); //初始化socket基本状态信息
		std::vector<TcpSocketInfo> SocketPool; //socket池
		void SetErrorMsg(std::string ErrorMsg,int ErrorCode); //设置错误信息
		bool ListenServerSocket(TcpSocketInfo& ServerSockt,int MaxConn); //监听socket
		bool RemoveTcpSocketInfo(TCPSOCK targetSockId); //从socket池塘移出数据
		bool HandleNewConnection(TCPSOCK ServerSocket, std::vector<TCPSOCK> &SockPool); //处理新连接
		#ifdef WIN32
				bool InitWinSocket();
		#endif // !WIN32
	};

}
#endif 