#include "DriverSetting.h"

DriverSetting::DriverSetting(NetworkSetting& ConfigSetting)
{
	auto& config = ConfigSetting.Config;

	RemoteAddr = config.RemoteIp + ":" + std::to_string(config.RemotePort);
	RemotePort = config.RemotePort;
	
	DriverMode = config.DriverMode;			//server-client
	
	if (DriverMode == EMode::Server)
	{
		ClientType = EClientType::None;
		ServerType = config.ServerType;
	}
	else if (DriverMode == EMode::Client)
	{
		ClientType = config.ClientType;
		ServerType = EServerType::None;
	}

	bNoTimeouts = config.bNoTimeouts;
	bConnectionlessOnly = config.bConnectionlessOnly;

	KeepAliveTime = config.KeepAliveTime;
	MaxTickRate = config.MaxTickRate;

	InitialConnectTimeout = config.InitialConnectTimeout;
	ConnectionTimeout = config.ConnectionTimeout;

	MaxChannelsSize = config.MaxChannelsSize;

	if (DriverMode == EMode::Server)
	{
		
		DesiredRecvSize = 32768;//=0x8000(Server)
		DesiredSendSize = 32768;//=0x8000(Server)

		SessionID = config.SessionID;
		ActiveSecret = config.ActiveSecret;

		MagicHeaderSizeBits = config.MagicHeaderSizeBits; //default(1) 
		MagicHeader = config.MagicHeader; //default(0)
		MagicHeaderOffset = config.MagicHeaderOffset; //default(0)

	}
	else if (DriverMode == EMode::Client)
	{
		DesiredRecvSize = 8192;//=0x2000(Client)
		DesiredSendSize = 8192;//=0x2000(Client)

		SessionID = 0;
		ActiveSecret = 0;

		MagicHeaderSizeBits = config.MagicHeaderSizeBits; //default(1) 
		MagicHeader = config.MagicHeader; //default(0)
		MagicHeaderOffset = config.MagicHeaderOffset; //default(0)
	}

	HandshakeSecret.resize(COOKIE_BYTE_SIZE, 0);

	PacketDropPermille = config.PacketDropPermille;
	PacketDuplicatePermille = config.PacketDuplicatePermille;
	PacketReorderWindow = config.PacketReorderWindow;
	PacketDropPermilleOutbound = config.PacketDropPermilleOutbound;
	PendingConnectionInactivityTimeoutSec = config.PendingConnectionInactivityTimeoutSec;
	TelemetryPort = config.TelemetryPort;

	LogLevel = config.LogLevel;
	SyntheticTrafficHz = (config.SyntheticTrafficHz == 0) ? 1u : config.SyntheticTrafficHz;
}

const bool DriverSetting::IsServer() const
{
	return DriverMode == EMode::Server;
}

const bool DriverSetting::IsClient() const 
{
	return DriverMode == EMode::Client; 
}

const std::string DriverSetting::GetRemoteAddr() const
{
	return RemoteAddr;
}

const uint32_t DriverSetting::GetSessionID() const
{
	return SessionID;
}

const uint32_t DriverSetting::GetActiveSecret() const
{
	return ActiveSecret;
}

const uint32_t DriverSetting::GetMagicHeaderSizeBits() const
{
	return MagicHeaderSizeBits;
}

const uint32_t DriverSetting::GetMagicHeader() const
{
	return MagicHeader;
}

const uint32_t DriverSetting::GetMagicHeaderOffset() const
{
	return MagicHeaderOffset;
}

const std::vector<uint8_t> DriverSetting::GetHandshakeSecret() const
{
	return HandshakeSecret;
}

const EServerType DriverSetting::GetServerType() const
{
	return IsServer() ? ServerType : EServerType::None;
}

const uint32_t DriverSetting::GetPacketDropPermille() const
{
	return PacketDropPermille;
}

const uint32_t DriverSetting::GetPacketDuplicatePermille() const
{
	return PacketDuplicatePermille;
}

const uint32_t DriverSetting::GetPacketReorderWindow() const
{
	return PacketReorderWindow;
}

const uint32_t DriverSetting::GetPacketDropPermilleOutbound() const
{
	return PacketDropPermilleOutbound;
}

const double DriverSetting::GetPendingConnectionInactivityTimeoutSec() const
{
	return PendingConnectionInactivityTimeoutSec;
}

const uint16_t DriverSetting::GetTelemetryPort() const
{
	return TelemetryPort;
}

const std::string& DriverSetting::GetLogLevel() const
{
	return LogLevel;
}

const uint32_t DriverSetting::GetSyntheticTrafficHz() const
{
	return SyntheticTrafficHz;
}
