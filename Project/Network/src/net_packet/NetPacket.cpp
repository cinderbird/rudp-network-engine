#include "NetPacket.h"

PacketHeader::PacketHeader(uint32_t ReserveBits) noexcept
	: Buffer(static_cast<int64_t>(ReserveBits), false)
{
	PreSizeHold();
}

void PacketHeader::HeaderSealed()
{
	if (bSealed)
	{
		return;
	}

	const uint32_t Bytes = (uint32_t)Buffer.GetNumBytes();

	if (Bytes > 255u)
	{
		bOverflow = true;
	}

	const uint8_t Size = static_cast<uint8_t>(Bytes & 0xFFu);
	uint8_t* BufferPtr = Buffer.GetData();

	assert(BufferPtr != nullptr);
	BufferPtr[0] = Size;
	bSealed = true;
}

BitWriter& PacketHeader::GetBitBuffer()
{
	return Buffer;
}

const uint8_t* PacketHeader::GetData() const
{
	return Buffer.GetData();
}

const uint32_t PacketHeader::GetSize() const
{
	return (uint32_t)Buffer.GetNumBytes();
}

void PacketHeader::PreSizeHold()
{
	uint8_t PreSizeHolder = 0;
	Buffer.Serialize(&PreSizeHolder, 1);
}

std::shared_ptr<OutReadyPacket> OutReadyPacket::BuildBegin(uint32_t HeaderSize, bool ProcessorPacket)
{
	auto NewPacket = std::make_shared<OutReadyPacket>();

	//step1. 메타데이터 갱신 및 사전 크기 지정
	NewPacket->Header.resize(HeaderSize);
	NewPacket->HeaderSize = HeaderSize;
	NewPacket->TotalSize += HeaderSize;

	NewPacket->DirtyFlag = true;
	NewPacket->bProcessorPacket = ProcessorPacket;


	if (NewPacket->bProcessorPacket == false)
	{
		NewPacket->Bunchs.reserve(OutReadyPacketBunchPreSize);
		NewPacket->BufferViews.reserve(OutReadyPacketBufferViewPreSize);
	}

	return NewPacket;
}

void OutReadyPacket::BuildEnd(std::shared_ptr<OutReadyPacket>& PreparedPacket, const uint8_t* HeaderPtr, uint32_t HeaderSize, uint64_t NowUs)
{
	//step1 데이터 복사
	::memcpy(PreparedPacket->Header.data(), HeaderPtr, HeaderSize);

	//step2. 메타데이터 갱신
	PreparedPacket->SendTimeUs = NowUs;
}

void OutReadyPacket::Push(std::shared_ptr<OutReadyBunch> Bunch)
{
	if (bProcessorPacket)
	{
		return;
	}

	TotalSize += Bunch->TotalSize;

	Bunchs.push_back(Bunch);
	DirtyFlag = true;
}

void OutReadyPacket::PacketSealed()
{
	//step1.
	if (!DirtyFlag)
	{
		return;
	}

	//step2
	BufferViews.clear();

	//step3. BufferView
	//3.1 Header Buffer View
	BufferViews.push_back(PacketBufferView{ Header.data(), HeaderSize });

	if (bProcessorPacket)
	{
#ifndef NDEBUG
		uint64_t TestTotalMessageSize = 0;
		for (const auto& View : BufferViews)
		{
			TestTotalMessageSize += View.Size;
		}

		assert(TestTotalMessageSize == TotalSize);
#endif
		DirtyFlag = false;
		return;
	}

	//3.2 Bunchs View

	for (const auto& Bunch : Bunchs)
	{
		BufferViews.push_back(PacketBufferView{ Bunch->HeaderBytes.get(), Bunch->HeaderSize });

		for (const auto& Message : Bunch->Messages)
		{
			BufferViews.push_back(PacketBufferView{ static_cast<const uint8_t*>(Message.BufferPtr), Message.Size });
		}
	}

#ifndef NDEBUG
	uint64_t TestTotalMessageSize = 0;
	for (const auto& View : BufferViews)
	{
		TestTotalMessageSize += View.Size;
	}

	assert(TestTotalMessageSize == TotalSize);
#endif

	DirtyFlag = false;
}
