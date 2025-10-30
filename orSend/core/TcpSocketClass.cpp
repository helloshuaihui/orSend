#include "TcpSocketClass.h"
namespace TCP {
	#ifdef WIN32
		bool TcpSocketClass::InitWinSocket()
		{
			WSADATA wsaData;
			int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
			if (result != 0) {
				SetErrorMsg("WSAStartup ��ʼ��ʧ��", result);
				return false;
			}
			else {
				std::cout << "WSAStartup ��ʼ���ɹ�,״̬��" << result << std::endl;
			}
			return true;
		}
		TCPSOCK TcpSocketClass::creatTcpScoketserver(std::string ip, int port)
		{
			TCPSOCK NewSocket = socket(AF_INET, SOCK_STREAM, 0);
			if (INVALID_SOCKET == NewSocket)
			{
				std::cout << "��������˾��ʧ��" << std::endl;
				WSACleanup();
				return -1;
			}
			else {
				SOCKADDR_IN addr = {};
				addr.sin_family = AF_INET;
				addr.sin_port = htons(port);		// �˿ں�
				addr.sin_addr.S_un.S_addr = inet_addr(ip.c_str());   //ip��ַ
				if (bind(NewSocket, (sockaddr*)&addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
				{
					SetErrorMsg("�󶨶˿ں�ʧ��", WSAGetLastError());
					closesocket(NewSocket);
					return -1;
				}
				else {
					std::cout << "socket�����ɹ�" << std::endl;
					SocketPool.push_back(InitTCPSOCKINFO(ip, port, NewSocket, SocketType::server));
				}
			}
			return NewSocket;
		}
		TCPSOCK TcpSocketClass::connTcpScokerServer(std::string ip, int port)
		{
			TCPSOCK NewSocket = socket(AF_INET, SOCK_STREAM, 0);
			if (INVALID_SOCKET == NewSocket)
			{
				SetErrorMsg("�����ͻ��˾��ʧ��", WSAGetLastError());
				return -1;
			}
			else {
				sockaddr_in serverAddr{};
				serverAddr.sin_family = AF_INET;
				serverAddr.sin_port = htons(port);
				if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) != 1) {
					SetErrorMsg("IP ��ַ����ʧ��", WSAGetLastError());
					return -1;
				}
				// 4. ���ӷ�����
				if (connect(NewSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
					SetErrorMsg("���ӷ�����ʧ��", WSAGetLastError());
					return -1;
				}
				else {
					std::cout << "���ӷ������ɹ�"<< std::endl;
					//��ȡ�˿�
					int LocalPort = -1;
					sockaddr_in localAddr{};
					int addrLen = sizeof(localAddr);
					if (getsockname(NewSocket, (SOCKADDR*)&localAddr, &addrLen) == SOCKET_ERROR) {
						std::cout << "��ȡ�˿�ʧ��" << std::endl;
					}
					else {
						LocalPort = ntohs(localAddr.sin_port);
					}
					SocketPool.push_back(InitTCPSOCKINFO(ip, LocalPort, NewSocket,SocketType::client));
				}
			}
			return NewSocket;
		}
	#endif // WIN32
	void TcpSocketClass::PrintError()
	{
		if (this->IsPrintError) {
			std::cout <<"[ERROR]" << this->ErrorMsg << " �������:" << this->ErrorCode << std::endl;
		}
	}
	TcpSocketClass::TcpSocketClass():
		ErrorMsg(""),
		ErrorCode(0),
		IsPrintError(true)
	{
		#ifdef WIN32
			InitWinSocket();
		#endif // WIN32

		std::cout << "��ӭʹ��tcp������" << std::endl;
	}

	TcpSocketClass::~TcpSocketClass()
	{
		#ifdef WIN32
			WSACleanup();
		#endif // WIN32
	}
	std::string TcpSocketClass::getCurrentTimeString()
	{
		// ��ȡ��ǰϵͳʱ�䣨UTCʱ��ת��Ϊ����ʱ�䣩
		auto now = std::chrono::system_clock::now();
		std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
		std::tm localTime;

		// ��ƽ̨�����̰߳�ȫ�ı���ʱ��ת��
		#ifdef _WIN32
			// Windowsʹ��localtime_s���̰߳�ȫ��
			localtime_s(&localTime, &nowTime);
		#else
			// Linuxʹ��localtime_r���̰߳�ȫ��
			localtime_r(&nowTime, &localTime);
		#endif

		// ��ʽ��ʱ��Ϊ�ַ���
		std::stringstream ss;
		ss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
		return ss.str();
	}
	TcpSocketInfo TcpSocketClass::InitTCPSOCKINFO(std::string ip, int port, TCPSOCK sock,int type)
	{
		TcpSocketInfo info;
		info.ip = std::move(ip);  // ʹ��move���ٿ���
		info.port = port; //�˿�
		info.sockId = sock;  // ����֮ǰ�������Ч���
		info.connTime = getCurrentTimeString(); //��ȡ��ǰϵͳʱ��
		info.connStatus = true; //����״̬ Ĭ��true
		info.type = type; //����socket��������
		return info;
	}
	void TcpSocketClass::SetErrorMsg(std::string ErrorMsg, int ErrorCode)
	{
		this->ErrorMsg = ErrorMsg;
		this->ErrorCode = ErrorCode;
		this->PrintError();
	}
	void TcpSocketClass::PrintSocketPool()
	{
		std::cout << "��ʼ��ӡsocket��ǰsocket����" << std::endl;
		for (int i = 0;i < this->SocketPool.size();i++) {
			std::cout
				<< "socket:" << this->SocketPool[i].sockId << "\n"
				<< "ip:" << this->SocketPool[i].ip << "\n"
				<< "port:" << this->SocketPool[i].port << "\n"
				<< "statu:" << this->SocketPool[i].connStatus << "\n"
				<< "ctime:" << this->SocketPool[i].connTime << "\n"
				<< "type:" << ((this->SocketPool[i].type == 1) ? "server" : "client") << "\n"
				<< std::endl;
		}
	}
}