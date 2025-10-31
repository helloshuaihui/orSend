#pragma once
#ifndef TCPSOCKETCLASS_H
#define TCPSOCKETCLASS_H
#include <iostream>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <mutex>
#ifdef WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment(lib, "ws2_32.lib")
	using TCPSOCK = SOCKET;
#endif // WIN32

#include <functional>
constexpr int TCPMAXBUFSIZE = 6000;
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
		//错误处理
		std::string ErrorMsg; //错误信息
		int ErrorCode; //错误代码
		bool IsPrintError; //是否打印错误
		void PrintError(); //错误打印函数
		//基础信息设置
		int MaxListenNum;
		virtual void OnConn(TCPSOCK sock); //客户端连接回调函数
		virtual void OnMessage(TCPSOCK sock, std::string& buf); //接收消息回调函数
		virtual void OnClose(TCPSOCK sock); //连接断开回调函数
		void PrintSocketPool(); //打印socket信息池
		bool StartServer(TCPSOCK sock);
		TcpSocketClass();
		~TcpSocketClass();
	private:	
		std::mutex socketPoolMutex; // 保护 SocketPool 的线程安全
		std::mutex TmpMutex; // 临时锁
		//工具函数
		std::string getCurrentTimeString(); //获取当前时间
		//socket处理函数等
		std::vector<TcpSocketInfo> SocketPool; //socket池
		bool ListenServerSocket(TcpSocketInfo& ServerSockt, int MaxConn); //监听socket
		TcpSocketInfo InitTCPSOCKINFO(std::string ip,int port, TCPSOCK sock,int type); //初始化socket基本状态信息
		void SetErrorMsg(std::string ErrorMsg,int ErrorCode); //设置错误信息
		bool RemoveTcpSocketInfo(TCPSOCK targetSockId); //从socket池塘移出数据
		bool HandleNewConnection(TCPSOCK ServerSocket, std::vector<TCPSOCK> &SockPool); //处理新连接
		bool HandleClientEvents(fd_set& readSet,std::vector<TCPSOCK>& SockPool);
		TcpSocketInfo* GetSockInfo(TCPSOCK sock);
		#ifdef WIN32
				bool InitWinSocket();
		#endif // !WIN32
	};
}
#endif 