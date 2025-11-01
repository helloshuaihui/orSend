// orSend.cpp: 定义应用程序的入口点。
//

#include "orSend.h"

using namespace std;
//class newclass:public TCP::TcpSocketClass
//{
//    void OnConn(TCPSOCK sock) override
//    {
//        std::cout << "自定义处理：新客户端连接，socket ID: " << sock << std::endl;
//
//    }
//};
int main()
{
	TCP::TcpSocketClass test;
	//SOCKET aaa = test.creatTcpScoketserver("0.0.0.0",6002);
	//SOCKET bbb = test.connTcpScokerServer("127.0.0.1", 6002);
	//test.StartClient(bbb);
	return 0;
}
