#pragma once
#include "pch.h"

class ActorProtocolHandler;
class ControlProtocolHandler;

class ProtocolHandlerManager
{
public:
	void AddControlHandler(std::shared_ptr<ControlProtocolHandler> NewHandler);
	void AddActorHandler(std::shared_ptr<ActorProtocolHandler> NewHandler);

	std::shared_ptr<ControlProtocolHandler> GetControlHandler();
	std::shared_ptr<ActorProtocolHandler> GetActorHandler();

private:
	std::shared_ptr<ControlProtocolHandler> ControlHandler;
	std::shared_ptr<ActorProtocolHandler> ActorHandler;
};
