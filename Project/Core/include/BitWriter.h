#pragma once
#include <cstdint>
#include <vector>
#include <string>

#include "BitController.h"


class BitWriter : public BitController
{
public:
	BitWriter();
	BitWriter(int64_t InMaxBits, bool InAllowResize = false);

	BitWriter(const BitWriter&) = default;
	BitWriter(BitWriter&&) = default;
	BitWriter& operator=(const BitWriter&) = default;
	BitWriter& operator=(BitWriter&&) = default;
	
	virtual void Serialize(void* Src, int64_t LengthBytes) override;
	virtual void SerializeString(std::string* Src, int64_t LengthBytes);

	virtual void SerializeBits(void* Src, int64_t LengthBits) override;
	virtual void SerializeBitsWithOffset(void* Src, int32_t SourceBit, int64_t LengthBits) override;
	virtual void SerializeInt(uint32_t& Value, uint32_t Max) override ;
	virtual void SerializeIntPacked(uint32_t& Value) override;

	void WriteIntWrapped(uint32_t Value, uint32_t ValueMax);
	void WriteBit(uint8_t In);
	
	uint8_t* GetData();
	const uint8_t* GetData(void) const;
	const std::vector<uint8_t>* GetBuffer();
	
	int64_t GetNumBits() const;
	int64_t GetNumBytes() const;
	int64_t GetMaxBits(void) const;

	void SetOverflowed(int32_t LengthBits);
	void SetAllowOverflow(bool bInAllow);
	bool AllowAppend(int64_t LengthBits);

	void SetAllowResize(bool NewResize);
	virtual void Reset() override;
	void WriteAlign();
	virtual void CountMemory(BitController& Con) const;
	
	void ProtobufSerialize(uint16_t NewPos);
	
private:
	std::vector<uint8_t> Buffer;
	int64_t Pos;
	int64_t Max;

	bool bAllowResize;
	bool bAllowOverflow;
};
