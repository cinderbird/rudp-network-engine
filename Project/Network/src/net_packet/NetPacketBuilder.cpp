#include "NetPacketBuilder.h"


std::shared_ptr<OutReadyPacket> ConnectionPacketBuilder::BuildStart() noexcept
{
	//step1. 
	return OutReadyPacket::BuildBegin(PacketHeaderSize);
}

bool ConnectionPacketBuilder::Append(std::shared_ptr<OutReadyPacket>& Packet, std::shared_ptr<OutReadyBunch> Bunch) noexcept
{
	uint32_t RequireSize = Bunch->TotalSize;

	if (RequireSize > PacketMaxSize)
	{
		return false;
	}

	if (Packet->TotalSize + RequireSize > PacketMaxSize)
	{
		return false;
	}

	Packet->Push(Bunch);

	return true;
}
