#include "ProtocolHandlerManager.h"
#include "ControlProtocolHandler.h"
#include "ActorProtocolHandler.h"

void ProtocolHandlerManager::AddControlHandler(std::shared_ptr<ControlProtocolHandler> NewHandler)
{
	ControlHandler = NewHandler;
}

void ProtocolHandlerManager::AddActorHandler(std::shared_ptr<ActorProtocolHandler> NewHandler)
{
	ActorHandler = NewHandler;
}

std::shared_ptr<ControlProtocolHandler> ProtocolHandlerManager::GetControlHandler()
{
	return ControlHandler;
}

std::shared_ptr<ActorProtocolHandler> ProtocolHandlerManager::GetActorHandler()
{
	return ActorHandler;
}

