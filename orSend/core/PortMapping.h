#pragma once
#ifndef PORTMAPPING_H
#define PORTMAPPING_H
#include <iostream>
#include <vector>
#include "TcpSocketClass.h"
namespace TCP {
	enum class PortMappingRunType : int
	{
		Server = 1, //服务器模式
		Client = 2 //客户端模式
	};
	struct PipelineInfo
	{
		TCPSOCK SSock;
		TCPSOCK Csock;
	};
	struct PortMappingSockInfo
	{
		std::string ip;
		int port;
		int delay;
		TCPSOCK sock;
		int type;
	};
	class PortMapping
	{
	public:
		PortMapping();
		~PortMapping();
	private:
		
	};
}
#endif // !PORTMAPPING_H
