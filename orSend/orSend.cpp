// orSend.cpp: 定义应用程序的入口点。
//

#include "orSend.h"

using namespace std;
int main()
{
	#ifdef _WIN32
		SetConsoleOutputCP(65001);  // 设置输出代码页
		SetConsoleCP(65001);        // 设置输入代码页
	#endif
	TCP::PortMapping test;
	test.AddServerBasicInfoPool((std::string)"127.0.0.1",80);
	test.AddLocalBasicInfoPool((std::string)"0.0.0.0", 8000);
	test.StratPortMapping();
	#ifdef _WIN32
		Sleep(1000000);
	#elif __linux__
		sleep(1000000000);
	#endif
	return 0;
}