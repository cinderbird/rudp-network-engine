#pragma once
#include <cstdint>

struct MessageCaputre
{
	unsigned char* Buffer;
	const uint16_t TotalSize;
	const uint32_t ChannelId;
};