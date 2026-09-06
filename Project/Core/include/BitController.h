#pragma once
#include <cstdint>
#include <windows.h>

class BitController
{
protected:
	uint8_t ArIsSaving : 1;
	uint8_t ArIsPersistent : 1;
	uint8_t ArIsNetArchive : 1;
	uint8_t ArIsLoading : 1;
	uint8_t ArForceByteSwapping : 1;
	uint8_t ArIsError : 1;

public:
	int64_t ArMaxSerializeSize;
	
public:
	BitController();
	
	virtual void Reset();
	virtual void SetIsLoading(bool bInIsLoading);

	virtual void CountBytes(size_t InNum, size_t InMax);

	virtual void SerializeBool(bool& D);
	virtual void Serialize(void* V, int64_t Length);
	virtual void SerializeBits(void* Src, int64_t LengthBits);
	virtual void SerializeBitsWithOffset(void* Src, int32_t SourceBit, int64_t LengthBits);
	virtual void SerializeInt(uint32_t& Value, uint32_t Max);
	virtual void SerializeIntPacked(uint32_t& Value);

	virtual BitController& GetInnermostState();
	

	BitController& ByteOrderSerialize(void* V, int32_t Length);

	bool IsLoading() const;

	bool IsError() const;
	void SetError();
	void ClearError();
	void SetIsSaving(bool bInIsSaving);
	void SetIsPersistent(bool bInIsPersistent);

	friend BitController& operator<<(BitController& Ar, wchar_t& Value);
	friend BitController& operator<<(BitController& Ar, uint8_t& Value);
	friend BitController& operator<<(BitController& Ar, int8_t& Value);
	friend BitController& operator<<(BitController& Ar, uint16_t& Value);
	friend BitController& operator<<(BitController& Ar, int16_t& Value);
	friend BitController& operator<<(BitController& Ar, uint32_t& Value);
	friend BitController& operator<<(BitController& Ar, int32_t& Value);
	friend BitController& operator<<(BitController& Ar, long& Value);
	friend BitController& operator<<(BitController& Ar, float& Value);
	friend BitController& operator<<(BitController& Ar, double& Value);
	friend BitController& operator<<(BitController& Ar, uint64_t& Value);
	friend BitController& operator<<(BitController& Ar, int64_t& Value);
	friend BitController& operator<<(BitController& Ar, bool& D);

private:
	BitController& SerializeByteOrderSwapped(void* V, int32_t Length);
	BitController& SerializeByteOrderSwapped(uint16_t& Value);
	BitController& SerializeByteOrderSwapped(uint32_t& Value);
	BitController& SerializeByteOrderSwapped(uint64_t& Value);
	BitController& SerializeByteOrderSwapped(unsigned long& Value);

	template<typename T>
	BitController& ByteOrderSerialize(T& Value);
	
	bool IsByteSwapping();

	BitController* NextProxy = nullptr;

	template<typename T> 
	void ForEachState(T Func);
};

template<typename T>
inline BitController& BitController::ByteOrderSerialize(T& Value)
{
	static_assert(std::is_signed<T>::value == false, "To reduce the number of template instances, cast 'Value' to a uint16_t&, uint32_t& or uint64_t& prior to the call or use ByteOrderSerialize(void*, int32_t).");

	if (!IsByteSwapping()) // Most likely case (hot path)
	{
		Serialize(&Value, sizeof(T));
		return *this;
	}
	return SerializeByteOrderSwapped(Value);
}

template<typename T>
inline void BitController::ForEachState(T Func)
{
	BitController& RootState = GetInnermostState();
	Func(RootState);

	for (BitController* Proxy = RootState.NextProxy; Proxy; Proxy = Proxy->NextProxy)
	{
		Func(*Proxy);
	}
}

