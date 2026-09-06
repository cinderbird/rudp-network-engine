#include "RttTracker.h"
#include <algorithm>

void ConnectionRttTracker::RecordSend(ConnectionRttTracker& Tracker, int32_t PacketId, uint64_t SendTimeUs)
{
	Tracker.Pending.push_back({ PacketId, SendTimeUs });

	if (Tracker.Pending.size() > kMaxPending)
	{
		Tracker.Pending.pop_front();
	}
}

bool ConnectionRttTracker::ConsumeAcked(ConnectionRttTracker& Tracker, int32_t AckMarkPacketId, uint64_t NowUs, uint32_t& OutRttUs)
{

	while (!Tracker.Pending.empty() && Tracker.Pending.front().PacketId < AckMarkPacketId)
	{
		Tracker.Pending.pop_front();
	}

	if (Tracker.Pending.empty() || Tracker.Pending.front().PacketId != AckMarkPacketId)
	{
		return false;
	}

	const uint64_t SendTimeUs = Tracker.Pending.front().SendTimeUs;
	Tracker.Pending.pop_front();

	if (NowUs < SendTimeUs)
	{
		return false;
	}

	const uint64_t RttUs64 = NowUs - SendTimeUs;
	const uint32_t RttUs = static_cast<uint32_t>(std::min<uint64_t>(RttUs64, UINT32_MAX));

	Tracker.SamplesUs[Tracker.WriteIndex] = RttUs;
	Tracker.SampleTimesUs[Tracker.WriteIndex] = NowUs;
	Tracker.WriteIndex = (Tracker.WriteIndex + 1) % kRingSize;
	Tracker.SampleCount = std::min(Tracker.SampleCount + 1, kRingSize);

	OutRttUs = RttUs;
	return true;
}

void ConnectionRttTracker::ConsumeNacked(ConnectionRttTracker& Tracker, int32_t NakMarkPacketId)
{

	while (!Tracker.Pending.empty() && Tracker.Pending.front().PacketId < NakMarkPacketId)
	{
		Tracker.Pending.pop_front();
	}

	if (!Tracker.Pending.empty() && Tracker.Pending.front().PacketId == NakMarkPacketId)
	{
		Tracker.Pending.pop_front();
	}
}

ConnectionRttTracker::Stats ConnectionRttTracker::ComputeStats(const ConnectionRttTracker& Tracker, uint64_t NowUs)
{
	Stats Result;

	if (Tracker.SampleCount == 0)
	{
		return Result;
	}

	std::array<uint32_t, kRingSize> Copy{};
	size_t Count = 0;
	for (size_t i = 0; i < Tracker.SampleCount; ++i)
	{
		const uint64_t Age = (NowUs >= Tracker.SampleTimesUs[i]) ? (NowUs - Tracker.SampleTimesUs[i]) : 0;
		if (Age <= kMaxSampleAgeUs)
		{
			Copy[Count++] = Tracker.SamplesUs[i];
		}
	}

	if (Count == 0)
	{
		return Result;
	}

	const auto Begin = Copy.begin();
	const auto End = Copy.begin() + Count;

	auto NthPercentile = [&](double Percentile) -> uint32_t
		{

			std::array<uint32_t, kRingSize> Work{};
			std::copy(Begin, End, Work.begin());
			size_t Index = static_cast<size_t>(Percentile * (Count - 1));
			if (Index >= Count)
			{
				Index = Count - 1;
			}
			auto WorkBegin = Work.begin();
			std::nth_element(WorkBegin, WorkBegin + Index, WorkBegin + Count);
			return *(WorkBegin + Index);
		};

	Result.P50Us = NthPercentile(0.50);
	Result.P95Us = NthPercentile(0.95);
	Result.P99Us = NthPercentile(0.99);
	Result.MinUs = *std::min_element(Begin, End);
	Result.MaxUs = *std::max_element(Begin, End);
	Result.SampleCount = static_cast<uint32_t>(Count);

	return Result;
}
