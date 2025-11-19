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
	struct LocalPortBasicInfo {
		std::string ip; //ip
		int port; //端口
		int ccout; //当前已连接数
		int isListen; //是否在监听
	};
	class PortMapping: public TcpSocketClass
	{
	public:
		/*分配服务器模式，默认低延迟*/
		ServerStrategy allocateServerType; 
		PortMapping();
		~PortMapping();
		/*添加需要转发的服务器*/
		bool AddServerBasicInfoPool(std::string &ip, int port); 
		/*添加本地监听的端口*/
		bool AddLocalBasicInfoPool(std::string& ip, int port);
		/*开始监听本地以及服务器端口进行转发*/
		bool StratPortMapping();
	private:
		std::vector<TcpSocketInfo> LockSockPool;/*本地socket信息池*/
		std::vector<TcpSocketInfo> ServeSockPool; /*服务器socket信息池*/
		std::vector<ServerBasicInfo> ServerBasicInfoPool; /*服务器基本信息池*/
		std::vector<LocalPortBasicInfo> LocalBasicInfoPool; /*本地端口基本信息池*/
		std::vector<SockBingInfo> SockBingPool; /*本地sock与服务器sock映射池*/
		//基础函数
		/*初始化绑定*/
		SockBingInfo InitSockBingInfo(TCPSOCK LID,TCPSOCK SID); 
		/*移除绑定信息*/
		bool DelSockBingInfo(TCPSOCK LID); 
		/*移出服务器socket信息池*/
		bool DelServeSock(TCPSOCK SID);
		/*移出本地socket信息池*/
		bool DelLockSock(TCPSOCK LID);
		/*通过LID查询绑定信息*/
		SockBingInfo* SearchSockBingInfoInLID(TCPSOCK LID); 
		/*通过SID查询绑定信息*/
		SockBingInfo* SearchSockBingInfoInSID(TCPSOCK SID); 
		/*初始化服务器信息*/
		ServerBasicInfo InitServerBasicInfo(std::string ip,int port);
		/*初始化本地监听信息*/
		LocalPortBasicInfo InitLocalBasicInfo(std::string ip, int port);
		/*获取一个服务器信息进行转发,默认低延迟*/
		ServerBasicInfo* GetServerBasicInfo(ServerStrategy type); 
		//映射函数
		//socket连接等处理函数、
		void OnServerConn(TCPSOCK sock) override;
		void OnServerMessage(TCPSOCK sock, std::string& buf) override;
		void OnServerClose(TCPSOCK sock) override;
		void OnClientMessage(TCPSOCK sock, std::string& buf) override;
		void OnClientClose(TCPSOCK sock) override;
	};
}
#endif 