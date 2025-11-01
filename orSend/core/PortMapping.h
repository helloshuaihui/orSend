#pragma once
#ifndef PORTMAPPING_H
#define PORTMAPPING_H_H
#include"TcpSocketClass.h"
namespace TCP {
	struct SockBingInfo
	{
		TCPSOCK LID;
		TCPSOCK SID;
	};
	class PortMapping: public TcpSocketClass
	{
	public:
		
		PortMapping();
		~PortMapping();

	private:

		std::vector<TcpSocketInfo> LockSockPool; //记录本地socket信息池
		std::vector<TcpSocketInfo> ServeSockPool; //服务器socket信息池
		std::vector<SockBingInfo> SockBingPool; //映射池
		//基础函数
		SockBingInfo InitSockBingInfo(TCPSOCK LID,TCPSOCK SID); //初始化绑定
		bool DelSockBingInfo(TCPSOCK LID); //移除绑定信息
		bool DelServeSock(TCPSOCK SID); //移出服务器socket信息池
		bool DelLockSock(TCPSOCK LID); //移出本地socket信息池
		SockBingInfo* SearchSockBingInfoInLID(TCPSOCK LID);
		SockBingInfo* SearchSockBingInfoInSID(TCPSOCK SID);
		//映射函数
		//socket连接等处理函数
		void OnConn(TCPSOCK sock) override;
		void OnServerMessage(TCPSOCK sock, std::string& buf) override;
		void OnServerClose(TCPSOCK sock) override;
		void OnClientMessage(TCPSOCK sock, std::string& buf) override;
		void OnClientClose(TCPSOCK sock) override;
	};
}
#endif 