#pragma once
#include <string>

enum { MAX_PACKET_SIZE = 1024 }; // MTU for the connection
enum { LAN_BEACON_MAX_PACKET_SIZE = 1024 }; // MTU for the connection

enum class EProcessorType : uint8_t
{
    Tcp,
    Udp,
};

enum class EMode : uint8_t
{
	None,
	Client,
	Server,
};

enum class EClientType : uint8_t
{
	None,
	TestClient,
	UEClient
};

enum class EServerType : uint8_t
{
	None,
	LoginServer,
	GatewayServer,
	GameServer
};

enum class State : uint8_t
{
	Uninitialized,
	InitializingComponents,
	Initialized
};

enum class EChannelType : uint8_t
{
	Control,
	Actor,
};

enum class EProcessorMode : uint8_t
{
	None,
	Client,					
	Server					
};

enum class EProcessorState : uint8_t
{
	UnInitialized,		
	InitializedOnLocal, 
	InitializeOnRemote, 
	Initialized         
};
enum class EHandshakePacketType : uint8_t
{
	InitialPacket = 0,
	Challenge = 1,
	Response = 2,
	Ack = 3,
	RestartHandshake = 4,
	RestartResponse = 5,
	VersionUpgrade = 6,

	Last = VersionUpgrade
};

inline std::string ToString(EHandshakePacketType Type, bool IsPacketDebug = false)
{
	switch (Type)
	{
		case EHandshakePacketType::InitialPacket:
		{
			if (IsPacketDebug)
				return "InitialPacket: 0";
			return "InitialPacket";
			break;
		}
		case EHandshakePacketType::Challenge:
		{
			if (IsPacketDebug)
				return "Challenge: 1";
			return "Challenge";
			break;
		}
		case EHandshakePacketType::Response:
		{
			if (IsPacketDebug)
				return "Response: 2";
			return "Response";
			break;
		}
		case EHandshakePacketType::Ack:
		{
			if (IsPacketDebug)
				return "Ack: 3";
			return "Ack";
			break;
		}
		case EHandshakePacketType::RestartHandshake:
		{
			if (IsPacketDebug)
				return "RestartHandshake: 4";
			return "RestartHandShake";
			break;
		}
		case EHandshakePacketType::RestartResponse:
		{
			if (IsPacketDebug)
				return "RestartResponse: 5";
			return "RestartResponse";
			break;
		}
		case EHandshakePacketType::VersionUpgrade:
		{
			if (IsPacketDebug)
				return "VersionUpgrade: 6";
			return "VersionUpgrade";
			break;
		}
		default:
		{
			if (IsPacketDebug)
				return "None: -1";
			return "None";
			break;
		}
	}
}

enum class ECheckAddressResolutionResult : uint8_t
{
	None,				
	TryFirstAddress,	
	TryNextAddress,		
	Connected,			
	Error,				
	FindSocketError		
};

enum class EAddressResolutionState : uint8_t
{
	None,					
	Disabled,				
	WaitingForResolves,		
	Connecting,				
	TryNextAddress,			
	Connected,				
	Done,					
	Error					
};

enum EConnectionState
{
	USOCK_Invalid = 0,	
	USOCK_Closed = 1,	
	USOCK_Pending = 2,	
	USOCK_Open = 3,		
};

enum EAcceptConnectionType
{
	Reject,
	Accept,
	Ignore
};

inline const std::string ToString(EAcceptConnectionType EnumVal)
{
	switch (EnumVal)
	{
	case Reject:
	{
		return "Reject";
	}
	case Accept:
	{
		return "Accept";
	}
	case Ignore:
	{
		return "Ignore";
	}
	}
	return "";
}
enum class EProtocolType : uint8_t
{
	None,
	ActorProtocol,
	ControlProtocol
};

inline std::string ToString(EProtocolType Type)
{
	switch (Type)
	{
		case EProtocolType::ActorProtocol: return "Actor";
		case EProtocolType::ControlProtocol: return "Control";
		default: return "None";
	}
}
