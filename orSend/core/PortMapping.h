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

		std::vector<TcpSocketInfo> LockSockPool; //��¼����socket��Ϣ��
		std::vector<TcpSocketInfo> ServeSockPool; //������socket��Ϣ��
		std::vector<SockBingInfo> SockBingPool; //ӳ���
		//��������
		SockBingInfo InitSockBingInfo(TCPSOCK LID,TCPSOCK SID); //��ʼ����
		bool DelSockBingInfo(TCPSOCK LID); //�Ƴ�����Ϣ
		bool DelServeSock(TCPSOCK SID); //�Ƴ�������socket��Ϣ��
		bool DelLockSock(TCPSOCK LID); //�Ƴ�����socket��Ϣ��
		SockBingInfo* SearchSockBingInfoInLID(TCPSOCK LID);
		SockBingInfo* SearchSockBingInfoInSID(TCPSOCK SID);
		//ӳ�亯��
		//socket���ӵȴ�����
		void OnConn(TCPSOCK sock) override;
		void OnServerMessage(TCPSOCK sock, std::string& buf) override;
		void OnServerClose(TCPSOCK sock) override;
		void OnClientMessage(TCPSOCK sock, std::string& buf) override;
		void OnClientClose(TCPSOCK sock) override;
	};
}
#endif 