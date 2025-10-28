#include "TcpSocketClass.h"
namespace TCP {
	#ifdef WIN32
		bool TcpSocketClass::InitWinSocket()
		{
			WSADATA wsaData;
			int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
			if (result != 0) {
				std::cout << "WSAStartup ��ʼ��ʧ�ܣ�" << result << std::endl;
				return false;
			}
			else {
				std::cout << "WSAStartup ��ʼ���ɹ���" << result << std::endl;
			}
			return true;
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

	}
}