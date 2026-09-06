#include "PacketProcessor.h"

PacketProcessor::PacketProcessor()
	: bActive(false)
	, State(EProcessorState::UnInitialized)
{

}

bool PacketProcessor::IsInitialized() const
{
    return bInitialized;
}

bool PacketProcessor::RequiresHandshake() const
{
	return bRequiresHandshake;
}

void PacketProcessor::SetState(EProcessorState InState)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	State = InState;
}

bool PacketProcessor::IsActive() const
{
	return bActive;
}

bool PacketProcessor::SetActive(bool Active)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return bActive = Active;
}
