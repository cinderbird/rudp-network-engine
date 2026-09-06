#pragma once
#include "pch.h"
#include "NetBunch.h"
#include "LockQueue.h"

struct OutReadyBunch;
struct NetMessage;
class NetChannel;

class ChannelBunchBuilder
{
public:
	struct BunchPolicy
	{
		uint32_t MaxPacketBytes = 1024; 
		uint32_t PacketHeaderBytes = 16;
		uint32_t BunchHeaderBytes = 8;
		uint32_t MaxMsgsPerBunch = 255;
		uint32_t MaxCoalesceUs = 2000;
	};

	ChannelBunchBuilder(uint8_t chIndex, bool bControl, bool bOpen, bool bClose, bool bReliable, BunchPolicy policy = {}, NetChannel* Owner = nullptr);

	void Submit(const NetMessage& m);

	void Pump(size_t batchLimit = 256, bool flushTail = true);

	bool TryDequeueReady(std::shared_ptr<OutReadyBunch>& out);

	bool HasReadyBunch() const;

	static void DiscardOutReadyBunch(std::shared_ptr<OutReadyBunch>&& Bunch);

	static bool IsHeaderOnlyBunch(const std::shared_ptr<OutReadyBunch>& Bunch);

private:
	std::vector<NetMessage> ActiveMsgs;
	uint32_t ActivePayload = 0;
	bool ActiveHasInit = false;

	const uint8_t ChIndex;
	const bool bControl;
	const bool bOpen;
	const bool bClose;
	const bool bReliable;

	BunchPolicy Policy;
	uint32_t MaxBunchPayload = 0;

	ThreadsafeQueue<NetMessage> QueuedMessage;
	std::queue<std::shared_ptr<OutReadyBunch>> QueuedOutBunch;
	std::atomic<uint64_t> lastSubmitUs{ 0 };
	std::atomic_flag Assembling = ATOMIC_FLAG_INIT;

	uint64_t LastEmitUs = 0;

	void ResetActive();
	bool Empty() const;
	bool IsFull()  const;
	bool TryInsert(const NetMessage& m);
	void SealAndPush();

	static uint64_t NowUs();

	NetChannel* Owner;
};
