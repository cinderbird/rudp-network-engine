#pragma once
#include "pch.h"
#include "PacketEnum.h"

class RecvPacketReader;
class PacketPipeline;

class PacketProcessor
{
    friend class PacketPipeline;
public:
	PacketProcessor();

	virtual void IncomingConnectionless(std::shared_ptr<RecvPacketReader> PacketRef) = 0;
	virtual void Incoming(std::shared_ptr<RecvPacketReader> PacketRef) = 0;

	virtual void OutgoingConnectionless(BitWriter& Buffer) = 0;
	virtual void Outgoing(BitWriter& Buffer) = 0;

    virtual void Initialize() = 0;
	virtual void Tick() = 0;
	virtual void NotifyHandshakeBegin() = 0;

	bool IsActive() const;
	bool SetActive(bool Active);
	void SetState(EProcessorState InState);
	bool IsInitialized() const;
	bool RequiresHandshake() const;

protected:
	std::weak_ptr<PacketPipeline> PipelineHandler;

	bool bActive;
	bool bRequiresHandshake = false;
	bool bInitialized = false;
	EProcessorState State;

	mutable std::mutex Mutex;
};