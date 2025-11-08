// orSend.cpp: 定义应用程序的入口点。
//

#include "orSend.h"

using namespace std;
int main()
{
	TCP::PortMapping test;
	test.AddServerBasicInfoPool((std::string)"0.0.0.0",7000);
	test.AddLocalBasicInfoPool((std::string)"0.0.0.0", 8000);
	return 0;
}
