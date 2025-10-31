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
		std::string connTime; //����ʱ��
		bool connStatus; //��ǰ״̬ true and false
		int type; //socket ����
		int EventLoopStatu; //�Ƿ�ʼ����
	};
	class TcpSocketClass
	{
	public:
		#ifdef WIN32
		TCPSOCK creatTcpScoketserver(std::string ip,int port);
		TCPSOCK connTcpScokerServer(std::string ip, int port);
		#endif // !WIN32
		//������
		std::string ErrorMsg; //������Ϣ
		int ErrorCode; //�������
		bool IsPrintError; //�Ƿ��ӡ����
		void PrintError(); //�����ӡ����
		//������Ϣ����
		int MaxListenNum;
		virtual void OnConn(TCPSOCK sock); //�ͻ������ӻص�����
		virtual void OnMessage(TCPSOCK sock, std::string& buf); //������Ϣ�ص�����
		virtual void OnClose(TCPSOCK sock); //���ӶϿ��ص�����
		void PrintSocketPool(); //��ӡsocket��Ϣ��
		bool StartServer(TCPSOCK sock);
		TcpSocketClass();
		~TcpSocketClass();
	private:	
		std::mutex socketPoolMutex; // ���� SocketPool ���̰߳�ȫ
		std::mutex TmpMutex; // ��ʱ��
		//���ߺ���
		std::string getCurrentTimeString(); //��ȡ��ǰʱ��
		//socket��������
		std::vector<TcpSocketInfo> SocketPool; //socket��
		bool ListenServerSocket(TcpSocketInfo& ServerSockt, int MaxConn); //����socket
		TcpSocketInfo InitTCPSOCKINFO(std::string ip,int port, TCPSOCK sock,int type); //��ʼ��socket����״̬��Ϣ
		void SetErrorMsg(std::string ErrorMsg,int ErrorCode); //���ô�����Ϣ
		bool RemoveTcpSocketInfo(TCPSOCK targetSockId); //��socket�����Ƴ�����
		bool HandleNewConnection(TCPSOCK ServerSocket, std::vector<TCPSOCK> &SockPool); //����������
		bool HandleClientEvents(fd_set& readSet,std::vector<TCPSOCK>& SockPool);
		TcpSocketInfo* GetSockInfo(TCPSOCK sock);
		#ifdef WIN32
				bool InitWinSocket();
		#endif // !WIN32
	};
}
#endif 