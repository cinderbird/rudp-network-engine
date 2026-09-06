#pragma once
#include "pch.h"

class NetChannel;

struct ChannelDefinition
{
	ChannelDefinition();

	int32_t StaticChannelIndex;	

	std::string ChannelName;			

	std::shared_ptr<NetChannel> ChannelClass;
};

