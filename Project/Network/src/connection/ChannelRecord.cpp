#include "ChannelRecord.h"

void ConnectionChannelRecord::PushPacketId(ConnectionChannelRecord& WrittenChannelsRecord, int32_t PacketId)
{
	if (PacketId != WrittenChannelsRecord.LastPacketId)
	{
		ConnectionChannelRecord::ChannelRecordEntry PacketEntry = { uint32_t(PacketId), 1u };
		WrittenChannelsRecord.EntryRecord.push_back(PacketEntry);
		WrittenChannelsRecord.LastPacketId = PacketId;
	}
}

void ConnectionChannelRecord::PushChannelRecord(ConnectionChannelRecord& WrittenChannelsRecord, int32_t PacketId, int32_t ChannelIndex)
{
	PushPacketId(WrittenChannelsRecord, PacketId);

	ConnectionChannelRecord::ChannelRecordEntry ChannelEntry = { uint32_t(ChannelIndex), 0u };
	WrittenChannelsRecord.EntryRecord.push_back(ChannelEntry);
}
