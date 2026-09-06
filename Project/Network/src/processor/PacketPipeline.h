#pragma once
#include "pch.h"
#include "PacketEnum.h"


class PacketProcessor;
class RecvPacketReader;

class PacketPipeline : public std::enable_shared_from_this<PacketPipeline>
{
    using PacketHandlerHandshakeComplete = std::function<void()>;

public:
    PacketPipeline(EProcessorMode Mode);

    void Tick();
    std::shared_ptr<PacketProcessor> AddProcessor(EProcessorType ProcessorType, bool bDefaultInitialize = false);

    void Incoming_Internal(std::shared_ptr<RecvPacketReader> PacketReader);
    void IncomingConnectionless(std::shared_ptr<RecvPacketReader> PacketReader);
    void Incoming(std::shared_ptr<RecvPacketReader> PacketView);

    void Outgoing_Internal(BitWriter& Buffer, bool bConnectionless);
    void OutgoingConnectionless(BitWriter& Buffer);
    void Outgoing(BitWriter& Buffer);
    
    void BeginHandShake(PacketHandlerHandshakeComplete InHandshakeDel);
    void BeginNMTHello();

public:
    EProcessorMode Mode;
    std::vector <std::shared_ptr<PacketProcessor>> ProcessorContainer;
    std::shared_ptr<PacketProcessor> UdpProcessor;
    PacketHandlerHandshakeComplete HandshakeCompleteDel;
};
