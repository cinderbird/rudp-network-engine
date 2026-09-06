#pragma once
#include <vector>
#include "BitController.h"

class BitReader : public BitController
{
public:
	BitReader(const uint8_t* Src = nullptr, int64_t CountBits = 0);

	BitReader(const BitReader&) = default;
	BitReader& operator=(const BitReader&) = default;
	BitReader(BitReader&&) = default;
	BitReader& operator=(BitReader&&) = default;

	void SetData(uint8_t* Src, int64_t CountBits);
	void SetData(BitReader* Src, int64_t CountBits);
	void SetData(std::vector<uint8_t>&& Src, int64_t CountBits);

	void ResetData(BitReader& Src, int64_t CountBits, int64_t CountBitsWithSlack = 0);
	
	void SerializeBits(void* Dest, int64_t LengthBits);
	virtual void SerializeBitsWithOffset(void* Dest, int32_t DestBit, int64_t LengthBits) override;
	void SerializeInt(uint32_t& OutValue, uint32_t ValueMax);
	virtual void SerializeIntPacked(uint32_t& Value) override;

	virtual void SerializeWithPos(void* Dest, int64_t Pos, int64_t LenthBits);

	uint32_t ReadInt(uint32_t Max);
	uint8_t ReadBit();

	void Serialize(void* Dest, int64_t LengthBytes);

	uint8_t* GetData();
	const uint8_t* GetData() const;
	const std::vector<uint8_t>& GetBuffer() const;
	uint8_t* GetDataPosChecked();


	int64_t GetBitsLeft() const;
	int64_t GetBytesLeft() const;
	bool AtEnd();
	int64_t GetNumBytes() const;
	int64_t GetNumBits() const;
	int64_t GetPosBits() const;

	void SetOverflowed(int64_t LengthBits);
	void SetAtEnd();

	virtual void CountMemory(BitController& Ar) const;

protected:
	std::vector<uint8_t> Buffer;

	int64_t Max;
	int64_t Pos;

private:
	uint8_t Shift(uint8_t Cnt);
};
