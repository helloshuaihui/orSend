// orSend.cpp: 定义应用程序的入口点。
//

#include "orSend.h"

using namespace std;

int main()
{
	TCP::TcpSocketClass test;
	SOCKET aaa = test.creatTcpScoketserver("0.0.0.0",6002);
	SOCKET bbb = test.connTcpScokerServer("127.0.0.1", 6003);
	test.PrintSocketPool();
	return 0;
}
