#pragma once
#include "pch.h"
#include "BufferReader.h"


/*
	Bunch의 메모리를 복사하여 소유하는 Reader
*/
class InBunchReader
{
public:
	InBunchReader(std::span<const uint8_t> BunchView, uint32_t ChSequence, uint8_t MessageCount);

	BufferReader GetReader();

	uint32_t GetSequence() const;

	uint8_t GetMessageCount() const;

private:
	std::vector<uint8_t> Buffer;
	uint32_t ChSequence; //== InReliable[ChIndex]
	uint8_t MessageCount;
};
