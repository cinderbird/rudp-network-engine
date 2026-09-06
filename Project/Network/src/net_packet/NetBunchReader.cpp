#include "NetBunchReader.h"

InBunchReader::InBunchReader(std::span<const uint8_t> BunchView, uint32_t ChSequence, uint8_t MessageCount)
	: Buffer(BunchView.begin(), BunchView.end())
	, ChSequence(ChSequence)
	, MessageCount(MessageCount)
{
}

BufferReader InBunchReader::GetReader()
{
	return BufferReader(Buffer.data(), Buffer.size());
}

uint32_t InBunchReader::GetSequence() const
{
	return ChSequence;
}

uint8_t InBunchReader::GetMessageCount() const
{
	return MessageCount;
}
