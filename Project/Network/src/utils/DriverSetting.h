#pragma once
#include "pch.h"
#include "ConfigSetting.h"

class NetworkNotify;

class DriverSetting
{
	friend class NetDriver;
	friend class IpNetDriver;

public:
	DriverSetting(NetworkSetting& ConfigSetting);

	const bool IsServer() const;
	const bool IsClient() const;

	const std::string GetRemoteAddr() const;
	const uint32_t GetSessionID() const;
	const uint32_t GetActiveSecret() const;

	const uint32_t GetMagicHeaderSizeBits() const;
	const uint32_t GetMagicHeader() const;
	const uint32_t GetMagicHeaderOffset() const;
	const std::vector<uint8_t> GetHandshakeSecret() const;

	const EServerType GetServerType() const;

	const uint32_t GetPacketDropPermille() const;
	const uint32_t GetPacketDuplicatePermille() const;
	const uint32_t GetPacketReorderWindow() const;
	const uint32_t GetPacketDropPermilleOutbound() const; // M15
	const double GetPendingConnectionInactivityTimeoutSec() const; // M18
	const uint16_t GetTelemetryPort() const; // M19
	const std::string& GetLogLevel() const; // M23
	const uint32_t GetSyntheticTrafficHz() const; // M23

private:
	std::string RemoteAddr;  //ip + port
	uint16_t RemotePort = 0;

	EMode DriverMode;			//server-client
	EClientType ClientType;
	EServerType ServerType;

	bool	bNoTimeouts = false;
	bool	bConnectionlessOnly = true;
	uint16_t	KeepAliveTime = 20;
	uint16_t	MaxTickRate = 60;

	uint16_t InitialConnectTimeout = 60;
	uint16_t ConnectionTimeout = 60;

	uint32_t  MaxChannelsSize = 16;

	uint32_t DesiredRecvSize = 131072;//=0x20000 //32768(Server) =0x8000
	uint32_t DesiredSendSize = 131072;//=0x20000 //32768(Server) =0x8000

	//udpProcessor
	uint32_t SessionID = 0;
	uint8_t ActiveSecret = 0;

	uint32_t MagicHeaderSizeBits = 1;
	uint32_t MagicHeader = 0;
	uint32_t MagicHeaderOffset = 0;

	std::vector<uint8_t> HandshakeSecret{};

	// 수신 측 네트워크 상태 시뮬레이션(M2). PacketSimulator 참고.
	uint32_t PacketDropPermille = 0;
	uint32_t PacketDuplicatePermille = 0;
	uint32_t PacketReorderWindow = 0;

	// 송신 측 드롭 시뮬레이션. 왜 드롭만 있는지는 ConfigSetting.h의 선언부
	// 주석 참고.
	uint32_t PacketDropPermilleOutbound = 0;

	// 설명은 ConfigSetting.h 선언부 주석 참고.
	double PendingConnectionInactivityTimeoutSec = 20.0;

	// 설명은 ConfigSetting.h 선언부 주석 참고.
	uint16_t TelemetryPort = 0;

	// 설명은 ConfigSetting.h 선언부 주석 참고.
	std::string LogLevel = "trace";
	uint32_t SyntheticTrafficHz = 1;
};


