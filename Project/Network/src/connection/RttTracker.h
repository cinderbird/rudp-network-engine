#pragma once
#include <deque>
#include <array>
#include <cstdint>

struct ConnectionRttTracker
{
	struct PendingSample
	{
		int32_t  PacketId;
		uint64_t SendTimeUs;
	};


	static constexpr size_t kRingSize = 2048;

	static constexpr uint64_t kMaxSampleAgeUs = 10'000'000; // 10초

	static constexpr size_t kMaxPending = kRingSize * 4;

	std::deque<PendingSample> Pending;              // FIFO, PacketId 오름차순
	std::array<uint32_t, kRingSize> SamplesUs{};    // 최근 RTT(마이크로초), 링 기록
	std::array<uint64_t, kRingSize> SampleTimesUs{}; // SamplesUs[i]가 기록된 시각(같은 인덱스)
	size_t WriteIndex{ 0 };
	size_t SampleCount{ 0 };                        // SamplesUs 중 유효 개수, <= kRingSize

	static void RecordSend(ConnectionRttTracker& Tracker, int32_t PacketId, uint64_t SendTimeUs);

	static bool ConsumeAcked(ConnectionRttTracker& Tracker, int32_t AckMarkPacketId, uint64_t NowUs, uint32_t& OutRttUs);

	static void ConsumeNacked(ConnectionRttTracker& Tracker, int32_t NakMarkPacketId);

	struct Stats
	{
		uint32_t P50Us = 0;
		uint32_t P95Us = 0;
		uint32_t P99Us = 0;
		uint32_t MinUs = 0;
		uint32_t MaxUs = 0;
		uint32_t SampleCount = 0;
	};

	static Stats ComputeStats(const ConnectionRttTracker& Tracker, uint64_t NowUs);
};
