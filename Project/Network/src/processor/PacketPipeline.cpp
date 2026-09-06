#include "PacketPipeline.h"
#include "PacketProcessor.h"
#include "UdpPacketProcessor.h"
#include "NetPacketReader.h"

PacketPipeline::PacketPipeline(EProcessorMode Mode)
	: Mode(Mode)
{
}

void PacketPipeline::BeginHandShake(PacketHandlerHandshakeComplete InHandshakeDel)
{
	HandshakeCompleteDel = InHandshakeDel;

	for (const auto& CurComponent : ProcessorContainer)
	{
		CurComponent->NotifyHandshakeBegin();
		break;
	}
}

void PacketPipeline::BeginNMTHello()
{
	if (HandshakeCompleteDel)
	{
		HandshakeCompleteDel();
		HandshakeCompleteDel = nullptr;
	}
}

void PacketPipeline::Tick()
{
	for (const std::shared_ptr<PacketProcessor>& Component : ProcessorContainer)
	{
		if (Component)
		{
			Component->Tick();
		}
	}
}

std::shared_ptr<PacketProcessor> PacketPipeline::AddProcessor(EProcessorType ProcessorType, bool bDefaultInitialize)
{
	std::shared_ptr< PacketProcessor> ReturnVal = nullptr;

	if (ProcessorType == EProcessorType::Udp)
	{
		ReturnVal = std::make_shared<UdpConnectionProcessor>();
		ReturnVal->PipelineHandler = shared_from_this();

		ProcessorContainer.push_back(ReturnVal);

		ReturnVal->PipelineHandler = shared_from_this();

		if (!bDefaultInitialize)
		{
			ReturnVal->Initialize();
		}
	}

	return ReturnVal;
}

void PacketPipeline::Incoming_Internal(std::shared_ptr<RecvPacketReader> PacketReader)
{
	for (const auto& CurComponent : ProcessorContainer)
	{
		if (PacketReader->IsConnectionlessPacket())
		{
			CurComponent->IncomingConnectionless(PacketReader);
		}
		else
		{
			CurComponent->Incoming(PacketReader);
		}
	}
}

void PacketPipeline::IncomingConnectionless(std::shared_ptr<RecvPacketReader> PacketReader)
{
	PacketReader->SetConnectionlessPacket(true);
	Incoming_Internal(PacketReader);
}

void PacketPipeline::Incoming(std::shared_ptr<RecvPacketReader> PacketView)
{
	Incoming_Internal(PacketView);
}

void PacketPipeline::Outgoing_Internal(BitWriter& Buffer, bool bConnectionless)
{
	for (const auto& Processor : ProcessorContainer)
	{
		if (bConnectionless)
		{
			Processor->OutgoingConnectionless(Buffer);
		}
		else
		{
			Processor->Outgoing(Buffer);
		}
	}
}

void PacketPipeline::OutgoingConnectionless(BitWriter& Buffer)
{
	Outgoing_Internal(Buffer, true);
}

void PacketPipeline::Outgoing(BitWriter& Buffer)
{
	Outgoing_Internal(Buffer, false);
}



