#include "TcpSocketClass.h"
namespace TCP {
	#ifdef WIN32
		bool TcpSocketClass::InitWinSocket()
		{
			WSADATA wsaData;
			int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
			if (result != 0) {
				std::cout << "WSAStartup ��ʼ��ʧ�ܣ�״̬��" << result << std::endl;
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
					std::cout << "�󶨶˿ں�ʧ��" << std::endl;
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
				std::cout << "�����ͻ��˾��ʧ��" << std::endl;
				return -1;
			}
			else {
				sockaddr_in serverAddr{};
				serverAddr.sin_family = AF_INET;
				serverAddr.sin_port = htons(port);
				if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) != 1) {
					std::cout << "IP ��ַ����ʧ��:" << WSAGetLastError() << std::endl;
					return -1;
				}
				// 4. ���ӷ�����
				if (connect(NewSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
					std::cout << "���ӷ�����ʧ��:" << WSAGetLastError() << std::endl;
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
	TcpSocketClass::TcpSocketClass()
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

		// 1. ��ʼ����֪������ip��port��
		info.ip = std::move(ip);  // ʹ��move���ٿ���
		info.port = port;

		// 2. ��ʼ��sockId��Ĭ����Ϊ��Чֵ���������ӳɹ����ٸ��£�
		info.sockId = sock;  // ����֮ǰ�������Ч���

		// 3. ��ʼ������ʱ�䣨��ǰϵͳʱ�䣩
		info.connTime = getCurrentTimeString();

		// 4. ��ʼ������״̬��Ĭ����Ϊtrue������ս������ӣ�
		info.connStatus = true;

		//����socket��������
		info.type = type;
		return info;
	}
	void TcpSocketClass::PrintSocketPool()
	{
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