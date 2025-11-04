#pragma once
#ifndef PORTMAPPING_H
#define PORTMAPPING_H_H
#include"TcpSocketClass.h"
namespace TCP {
	enum ServerStrategy
	{
		SINGLE=1,
		DELAY=2,
		CNUM=3
	};
	struct SockBingInfo
	{
		TCPSOCK LID;
		TCPSOCK SID;
	};
	struct ServerBasicInfo
	{
		std::string ip; //ip
		int port; //端口
		int delay; //延迟
		bool isOnlie; //是否在线
		int ccout; //当前已连接数
	};
	class PortMapping: public TcpSocketClass
	{
	public:
		ServerStrategy allocateServerType; /*分配服务器模式，默认低延迟*/
		PortMapping();
		~PortMapping();

	private:
		std::vector<TcpSocketInfo> LockSockPool;/*本地socket信息池*/
		std::vector<TcpSocketInfo> ServeSockPool; /*服务器socket信息池*/
		std::vector<ServerBasicInfo> ServerBasicInfoPool; /*服务器基本信息池*/
		std::vector<SockBingInfo> SockBingPool; /*本地sock与服务器sock映射池*/
		//基础函数
		/*初始化绑定*/
		SockBingInfo InitSockBingInfo(TCPSOCK LID,TCPSOCK SID); /*初始化绑定*/
		bool DelSockBingInfo(TCPSOCK LID); /*移除绑定信息*/
		bool DelServeSock(TCPSOCK SID); /*移出服务器socket信息池*/
		bool DelLockSock(TCPSOCK LID); /*移出本地socket信息池*/
		SockBingInfo* SearchSockBingInfoInLID(TCPSOCK LID); /*通过LID查询绑定信息*/
		SockBingInfo* SearchSockBingInfoInSID(TCPSOCK SID); /*通过SID查询绑定信息*/
		ServerBasicInfo* GetServerBasicInfo(ServerStrategy type); /*获取一个服务器信息进行转发,默认低延迟*/
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