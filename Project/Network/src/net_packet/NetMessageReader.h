#pragma once
#include "pch.h"


class InRecvPacketEvent;
class InBunchReader;

using RecvBunchOwnerRef = std::variant<std::shared_ptr<InRecvPacketEvent>, std::shared_ptr<InBunchReader>>;

class RecvMessageReader
{
public:
	std::span<const uint8_t> View;
	uint16_t MessageId;
	RecvBunchOwnerRef Owner;
};
