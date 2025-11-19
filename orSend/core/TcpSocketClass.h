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
		/*客户端连接回调函数*/
		virtual void OnServerConn(TCPSOCK sock);
		/*服务端 接收消息回调函数*/
		virtual void OnServerMessage(TCPSOCK sock, std::string buf); 
		/*服务端 连接断开回调函数*/
		virtual void OnServerClose(TCPSOCK sock);
		/*客户端 接收消息回调函数*/
		virtual void OnClientMessage(TCPSOCK sock, std::string buf);
		/*客户端 连接断开回调函数*/
		virtual void OnClientClose(TCPSOCK sock);
		/*打印socket信息池*/
		void PrintSocketPool(); 
		/*开始监听服务器*/
		bool StartServer(TCPSOCK sock);
		/*开始监听客户端*/
		bool StartClient(TCPSOCK sock);
		TcpSocketClass();
		~TcpSocketClass();
	private:	
		std::mutex socketPoolMutex; // 保护 SocketPool 的线程安全
		std::mutex TmpMutex; // 临时锁
		//工具函数
		/*获取当前时间*/
		std::string getCurrentTimeString();
		//socket处理函数等
		/*socket池*/
		std::vector<TcpSocketInfo> SocketPool;
		/*监听服务器socket*/
		bool ListenServerSocket(TcpSocketInfo& ServerSockt, int MaxConn); 
		/*监听客户端socket消息*/
		bool ListenClientSocket(TcpSocketInfo& ClientSockt);
		/*初始化socket基本状态信息*/
		TcpSocketInfo InitTCPSOCKINFO(std::string ip,int port, TCPSOCK sock,int type);
		/*设置错误信息*/
		void SetErrorMsg(std::string ErrorMsg,int ErrorCode);
		/*从socket池删除基础信息*/
		bool RemoveTcpSocketInfo(TCPSOCK targetSockId);
		/*处理新连接*/
		bool HandleNewConnection(TCPSOCK ServerSocket, std::vector<TCPSOCK> &SockPool);
		/*处理客户端事件*/
		bool HandleClientEvents(fd_set& readSet,std::vector<TCPSOCK>& SockPool);
		/*获取soket基础信息*/
		TcpSocketInfo* GetSockInfo(TCPSOCK sock);
		#ifdef WIN32
				bool InitWinSocket();
		#endif // !WIN32
	};
}
#endif 