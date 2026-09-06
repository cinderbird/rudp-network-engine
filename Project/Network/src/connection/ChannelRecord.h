#pragma once
#include <deque>

struct ConnectionChannelRecord
{
	ConnectionChannelRecord()
		: EntryRecord()
		, LastPacketId(-1)
	{
	}

	struct ChannelRecordEntry
	{
		uint32_t Value : 31;
		uint32_t IsSequence : 1;
	};

	using ChannelRecordEntryQueue = std::deque<ChannelRecordEntry>;

	ChannelRecordEntryQueue EntryRecord;
	int32_t LastPacketId;


	static void PushPacketId(ConnectionChannelRecord& WrittenChannelsRecord, int32_t PacketId);
	static void PushChannelRecord(ConnectionChannelRecord& WrittenChannelsRecord, int32_t PacketId, int32_t ChannelIndex);

	template<class Functor>
	static bool ConsumeChannelRecordsForPacket(ConnectionChannelRecord& WrittenChannelsRecord, int32_t PacketId, Functor&& Func)
	{
		auto& Record = WrittenChannelsRecord.EntryRecord;
		if (Record.empty())
		{
			return true;
		}

		const ChannelRecordEntry PacketEntry = Record.front();

		if (PacketEntry.IsSequence != 1u || PacketEntry.Value != (uint32_t)PacketId)
		{
			return false;
		}
		Record.pop_front();

		uint32_t PreviousChannelIndex = static_cast<uint32_t>(UINT32_MAX);
		while (!Record.empty() && Record.front().IsSequence == 0u)
		{
			const ChannelRecordEntry Entry = Record.front();
			Record.pop_front();

			const uint32_t ChannelIndex = Entry.Value;

			if (ChannelIndex != PreviousChannelIndex)
			{
				Func(PacketId, ChannelIndex);
				PreviousChannelIndex = ChannelIndex;
			}
		}
		return true;
	}
};

