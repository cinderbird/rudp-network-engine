#pragma once
#include "pch.h"
#include "ChannelRecord.h"
#include "NetBunch.h"

struct PacketBufferView
{
	const uint8_t* BufferPtr{ nullptr };
	uint32_t Size{ 0 };
};

struct OutReadyPacket
{
	//뷰
	std::vector<PacketBufferView> BufferViews;

	//데이터
	std::vector<uint8_t> Header;
	std::vector<std::shared_ptr<OutReadyBunch>> Bunchs;

	//메타
	uint32_t HeaderSize{ 0 };
	uint32_t TotalSize{ 0 };
	uint64_t SendTimeUs{ 0 };
	bool DirtyFlag{ false };
	bool bProcessorPacket{ false };

	uint32_t OutPacketId{ 0 };

	static std::shared_ptr<OutReadyPacket> BuildBegin(uint32_t HeaderSize, bool ProcessorPacket = false);

	static void BuildEnd(std::shared_ptr<OutReadyPacket>& PreparedPacket, const uint8_t* HeaderPtr, uint32_t HeaderSize, uint64_t NowUs);

	void Push(std::shared_ptr<OutReadyBunch> Bunch);

	void PacketSealed();

	static constexpr uint32_t OutReadyPacketBunchPreSize = 10;
	static constexpr uint32_t OutReadyPacketBufferViewPreSize = OutReadyPacketBunchPreSize;
};

struct PacketHeader
{
	explicit PacketHeader(uint32_t ReserveBits = 128) noexcept;

	void HeaderSealed();

	BitWriter& GetBitBuffer();
	const uint8_t* GetData() const;
	const uint32_t GetSize() const;

private:
	void PreSizeHold();

	BitWriter Buffer;
	bool      bSealed{};
	bool      bOverflow{};
};
