#include "PortMapping.h"
namespace TCP {
	PortMapping::PortMapping()
	{
	}

	PortMapping::~PortMapping()
	{
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
		return false;
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

	void PortMapping::OnConn(TCPSOCK sock)
	{
		//本地连接进来 创建一个与服务端的连接
	}

	void PortMapping::OnServerMessage(TCPSOCK sock, std::string& buf)
	{
		//监听到本地端口的信息 转发到 另外一个服务器客户端
	}

	void PortMapping::OnServerClose(TCPSOCK sock)
	{
		//本地连接关闭 同时关闭与服务器的连接

	}
	void PortMapping::OnClientMessage(TCPSOCK sock, std::string& buf)
	{
		//收到来自服务器的消息 转发信息到客户端
	}

	void PortMapping::OnClientClose(TCPSOCK sock)
	{
		/*分两种
			1.连接到映射本地server的 client sock关闭
			2.本地与服务器的连接的client sock关闭
		*/
	}
	
}
