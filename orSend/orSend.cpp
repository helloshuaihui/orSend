// orSend.cpp: 定义应用程序的入口点。
//

#include "orSend.h"

using namespace std;
int main()
{
	TCP::PortMapping test;
	test.AddServerBasicInfoPool((std::string)"127.0.0.1",3306);
	test.AddLocalBasicInfoPool((std::string)"0.0.0.0", 8000);
	test.StratPortMapping();
	Sleep(1000000);
	return 0;
}
