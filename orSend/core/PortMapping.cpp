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
	}

	void PortMapping::OnMessage(TCPSOCK sock, std::string& buf)
	{
	}

	void PortMapping::OnClose(TCPSOCK sock)
	{
	}
	
}
