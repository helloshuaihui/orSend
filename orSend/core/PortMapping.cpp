#include "PortMapping.h"
namespace TCP {
	PortMapping::PortMapping():
		allocateServerType(TCP::ServerStrategy::DELAY)
	{
	}
	PortMapping::~PortMapping()
	{
	}
	bool PortMapping::AddServerBasicInfoPool(std::string &ip, int port)
	{
		ServerBasicInfo SInfo = InitServerBasicInfo(ip,port);
		//测试延迟
		ServerBasicInfoPool.push_back(SInfo);
		return true;
	}
	bool PortMapping::AddLocalBasicInfoPool(std::string& ip, int port)
	{
		LocalPortBasicInfo SInfo = InitLocalBasicInfo(ip, port);
		//测试延迟
		LocalBasicInfoPool.push_back(SInfo);
		return true;
	}
	bool PortMapping::StratPortMapping()
	{
		//检查是否有服务器以及本地数据
		if (ServerBasicInfoPool.size()==0) {
			std::cout << "并未添加服务器数据" << std::endl;
		}
		if (LocalBasicInfoPool.size() == 0) {
			std::cout << "并未添加本地监听的IP以及端口数据" << std::endl;
		}
	
		for (int i = 0;i < LocalBasicInfoPool.size();i++) {
			LocalPortBasicInfo a = LocalBasicInfoPool.at(i);
			std::thread  th([this, a]() -> void {
				//开始监听
				TCPSOCK sock = creatTcpScoketserver(a.ip, a.port);
				if (sock!=-1) {
					StartServer(sock);
				}
			});
			th.detach();
		}
		return false;
	}
	SockBingInfo PortMapping::InitSockBingInfo(TCPSOCK LID, TCPSOCK SID)
	{
		SockBingInfo info;
		info.LID = LID;
		info.SID = SID;
		return info;
	}
	bool PortMapping::DelSockBingInfo(TCPSOCK LID)
	{
		for (int i = 0;i < SockBingPool.size();i++) {
			if (SockBingPool.at(i).LID == LID) {
				SockBingPool.erase(SockBingPool.begin() + i);
				return true;
			}
		}
		return true;
	}
	bool PortMapping::DelServeSock(TCPSOCK SID)
	{
		for (int i = 0;i < ServeSockPool.size();i++) {
			if (ServeSockPool.at(i).sockId == SID) {
				ServeSockPool.erase(ServeSockPool.begin() + i);
				return true;
			}
		}
		return false;
	}
	bool PortMapping::DelLockSock(TCPSOCK LID)
	{
		for (int i = 0;i < LockSockPool.size();i++) {
			if (LockSockPool.at(i).sockId == LID) {
				LockSockPool.erase(LockSockPool.begin() + i);
				return true;
			}
		}
		return false;
	}
	SockBingInfo* PortMapping::SearchSockBingInfoInLID(TCPSOCK LID)
	{
		for (int i = 0;i < SockBingPool.size();i++) {
			if (SockBingPool.at(i).LID == LID) {
				return &SockBingPool.at(i);
			}
		}
		return nullptr;
	}
	SockBingInfo* PortMapping::SearchSockBingInfoInSID(TCPSOCK SID)
	{
		for (int i = 0;i < SockBingPool.size();i++) {
			if (SockBingPool.at(i).SID == SID) {
				return &SockBingPool.at(i);
			}
		}
		return nullptr;
	}
	ServerBasicInfo PortMapping::InitServerBasicInfo(std::string ip, int port)
	{
		ServerBasicInfo info;
		info.ip = ip;
		info.port = port;
		info.ccout = 0;
		info.isOnlie = true;
		info.delay = 0;
		return info;
	}
	LocalPortBasicInfo PortMapping::InitLocalBasicInfo(std::string ip, int port)
	{
		LocalPortBasicInfo info;
		info.ip = ip;
		info.port = port;
		info.isListen = false;
		info.ccout = 0;
		return info;
	}
	ServerBasicInfo* PortMapping::GetServerBasicInfo(ServerStrategy type)
	{
		//策略模式 单一策略 延迟优先 数量优先
		if (type == ServerStrategy::SINGLE) {
			//找到第一个能用的
			for (int i = 0; i < ServerBasicInfoPool.size();i++) {
				ServerBasicInfo *TargetServer = &ServerBasicInfoPool.at(i);
				if (TargetServer->delay != -1 && TargetServer->isOnlie) {
					return TargetServer;
				}
			}
		}else if (type == ServerStrategy::DELAY) {
			//优先找到延迟最低的
			ServerBasicInfo* TargetServer = &ServerBasicInfoPool.at(0);
			for (int i = 1; i < ServerBasicInfoPool.size(); i++) {
				ServerBasicInfo* tmp = &ServerBasicInfoPool.at(i);
				if (tmp->delay < TargetServer->delay) {
					if (tmp->isOnlie) {
						return tmp;
					}
				}
			}
			return TargetServer;
		}else if (type == ServerStrategy::CNUM) {
			//找到数量最少且延迟低的
			ServerBasicInfo* TargetServer = &ServerBasicInfoPool.at(0);
			for (int i = 1; i < ServerBasicInfoPool.size(); i++) {
				ServerBasicInfo* tmp = &ServerBasicInfoPool.at(i);
				if (tmp->ccout < TargetServer->ccout) {
					if (tmp->isOnlie) {
						return tmp;
					}
				}
			}
			return TargetServer;
		}
		return nullptr;
	}
	void PortMapping::forwardData(SOCKET from_sock, SOCKET to_sock, ForwardType forwardType)
	{
		char buffer[TCPMAXBUFSIZE]; // 数据缓冲区
		while (true) {
			// 从from_sock接收数据
			int recv_len = recv(from_sock, buffer, sizeof(buffer), 0);
			if (recv_len <= 0) {
				break;
			}
			// 转发到to_sock
			int send_len = send(to_sock, buffer, recv_len, 0);
			if (send_len <= 0) {
				break;
			}
		}
		// 关闭套接字
		if (ForwardType::CTOS) {
			//重新创建转发

		}
		else {
			closesocket(from_sock);
			closesocket(to_sock);
		}
	}
	void PortMapping::OnServerConn(TCPSOCK sock)
	{
		//绑定 
		ServerBasicInfo* serverBasicInfo = GetServerBasicInfo(allocateServerType);
		if (serverBasicInfo == nullptr) {
			//如果没找到合适的服务器信息 则打印错误信息
			std::cout << "未找到合适的服务器信息,转发失败" << std::endl;
		}
		TCPSOCK ssock = connTcpScokerServer(serverBasicInfo->ip, serverBasicInfo->port);
		if (ssock != -1) {
			SockBingPool.push_back(InitSockBingInfo(sock, ssock)); //绑定并添加信息
			//同时开启消息监听
			std::thread th([this, ssock]()->void {
				StartClient(ssock);
				});
			th.detach();
			//send(ssock, buf.c_str(), buf.size(), 0); //转发消息
		}
		else {
			//如果为-1也打印错误信息
			std::cout << "连接到服务器失败,转发失败" << std::endl;
		}
	}
	void PortMapping::OnServerMessage(TCPSOCK sock, std::string buf)
	{
		//监听到本地端口的信息 转发到 另外一个服务器客户端
		//先找到服务器
		std::cout << "监听到本地端口的数据:" << buf << std::endl;
		SockBingInfo* bingInfo=SearchSockBingInfoInLID(sock);
		if (bingInfo == nullptr) {
			//如果未绑定需要先进行绑定
			ServerBasicInfo* serverBasicInfo = GetServerBasicInfo(allocateServerType);
			if (serverBasicInfo==nullptr) {
				//如果没找到合适的服务器信息 则打印错误信息
				std::cout << "未找到合适的服务器信息,转发失败" << std::endl;
			}
			TCPSOCK ssock = connTcpScokerServer(serverBasicInfo->ip,serverBasicInfo->port);
			if (ssock != -1) {
				SockBingPool.push_back(InitSockBingInfo(sock, ssock)); //绑定并添加信息
				//同时开启消息监听
				std::thread th([this, ssock]()->void {
					StartClient(ssock);
				});
				th.detach();
				send(ssock, buf.c_str(), buf.size(), 0); //转发消息
			}
			else {
				//如果为-1也打印错误信息 并保留消息
				std::cout << "连接到服务器失败,转发失败" << std::endl;
			}
		}
		else {
			std::cout << "将本地数据转发给->"<< bingInfo->SID << std::endl;
			send(bingInfo->SID, buf.c_str(), buf.size(), 0);
		}
		
	}
	void PortMapping::OnServerClose(TCPSOCK sock)
	{
		std::cout << "监听到本地端口的连接关闭->" << sock << std::endl;
		//本地连接关闭 同时关闭与服务器的连接
		SockBingInfo* bingInfo=SearchSockBingInfoInLID(sock);
		if (bingInfo == nullptr) {
			DelLockSock(sock);
		}
		else {
			DelLockSock(sock);
			DelServeSock(bingInfo->SID);
			DelSockBingInfo(sock);
		}

	}
	void PortMapping::OnClientMessage(TCPSOCK sock, std::string buf)
	{
		std::cout << "监听到服务器的数据->" << buf << std::endl;
		//收到来自服务器的消息 转发信息到客户端
		SockBingInfo* bingInfo=SearchSockBingInfoInSID(sock);
		if (bingInfo==nullptr) {
			std::cout << "收到来自服务端的消息，但是服务端并未绑定本地sock，转发失败" << std::endl;
			DelServeSock(bingInfo->SID);
		}
		else {
			send(bingInfo->LID, buf.c_str(), buf.size(), 0); //转发消息
		}
	}
	void PortMapping::OnClientClose(TCPSOCK sock)
	{
		//服务器的连接的client sock关闭
		//服务器关闭后应该取消之前的绑定并给对应的客户端重新分配
		//当连接某些服务时，服务可能会主动发送验证信息，需要在连接到服务器后及时将这部分数据转发到对应的客户端
		std::cout << "监听到与服务器的连接断开->" << sock << std::endl;
		SockBingInfo* bingInfo = SearchSockBingInfoInSID(sock);
		if (bingInfo != nullptr) {
			DelServeSock(bingInfo->SID);
			DelSockBingInfo(bingInfo->LID);
			std::cout << "已经除去" << sock << "的绑定消息消息" << std::endl;
		}
	}
	
}
