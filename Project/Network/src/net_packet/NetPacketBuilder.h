#pragma once
#include "pch.h"
#include "NetPacket.h"

static constexpr uint32_t PacketHeaderSize = 10;
static constexpr uint32_t PacketHeaderBitSize = PacketHeaderSize * 8;

static constexpr uint32_t ConnectionlessPacketHeaderSize = 100; //
static constexpr uint32_t ConnectionlessPacketHeaderBitSize = ConnectionlessPacketHeaderSize * 8;

class ConnectionPacketBuilder
{
public:
	static std::shared_ptr<OutReadyPacket> BuildStart() noexcept;

	template<typename HeaderWriter>
	static void BuildEnd(std::shared_ptr<OutReadyPacket>& PrepreadPacket, HeaderWriter&& Func)
	{
		//step2. PacketHeader
		PacketHeader Ph{ PacketHeaderBitSize };

		//step3
		Func(Ph.GetBitBuffer()); //Outgoing -> PacketNotify.Write 호출

		//step4.
		Ph.HeaderSealed();

		//step5.
		OutReadyPacket::BuildEnd(PrepreadPacket, Ph.GetData(), PacketHeaderSize, Network::Clock::NowUs());
	}

	static bool Append(std::shared_ptr<OutReadyPacket>& Packet, std::shared_ptr<OutReadyBunch> Bunch) noexcept;

	static constexpr uint32_t PacketMaxSize = 1024;
};

class ProcessorPacketBuilder
{
public:
	template<typename HeaderWriter>
	static std::shared_ptr<OutReadyPacket> Build(HeaderWriter&& Func) noexcept
	{

		constexpr bool bHanshakePacket = true;

		//step1. PacketHeader
		PacketHeader Ph{ ConnectionlessPacketHeaderBitSize };

		//step2
		Func(Ph.GetBitBuffer()); //3-way handshake 콜백 함수

		//step3.
		Ph.HeaderSealed();

		auto PreparedPacket = OutReadyPacket::BuildBegin(ConnectionlessPacketHeaderSize, bHanshakePacket);
		OutReadyPacket::BuildEnd(PreparedPacket, Ph.GetData(), ConnectionlessPacketHeaderSize, Network::Clock::NowUs());

		return PreparedPacket;
	}
};
