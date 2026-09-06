#pragma once
#include "pch.h"
#include <nlohmann/json.hpp>
#include <PacketEnum.h>
#include <fstream>
#include <iostream>

struct ServerConfig
{
	std::string RemoteIp;
	int32_t     RemotePort = 0;

	EMode       DriverMode = EMode::None;
	EClientType ClientType = EClientType::None;
	EServerType ServerType = EServerType::None;

	bool    bNoTimeouts = false;
	bool    bConnectionlessOnly = false;

	uint16_t KeepAliveTime = 0;
	uint16_t MaxTickRate = 0;
	uint16_t InitialConnectTimeout = 0;
	uint16_t ConnectionTimeout = 0;
	uint32_t MaxChannelsSize = 0;
	uint32_t DesiredRecvSize = 0;
	uint32_t DesiredSendSize = 0;
	uint32_t SessionID = 0;
	uint8_t  ActiveSecret = 0;
	uint32_t MagicHeaderSizeBits = 0;
	uint32_t MagicHeader = 0;
	uint32_t MagicHeaderOffset = 0;

	uint32_t PacketDropPermille = 0;
	uint32_t PacketDuplicatePermille = 0;
	uint32_t PacketReorderWindow = 0; // 0이면 재정렬 비활성화; 아니면 이만큼 데이터그램을 버퍼링했다가 무작위로 하나 방출

	uint32_t PacketDropPermilleOutbound = 0;

	double PendingConnectionInactivityTimeoutSec = 20.0;

	uint16_t TelemetryPort = 0;

	std::string LogLevel = "trace";

	uint32_t SyntheticTrafficHz = 1;
};

NLOHMANN_JSON_SERIALIZE_ENUM(EMode,
{
  {EMode::None,   "None"},
  {EMode::Client, "Client"},
  {EMode::Server, "Server"}
})

// EClientType ↔ "None"/"TestClient"/"UEClient"
NLOHMANN_JSON_SERIALIZE_ENUM(EClientType,
{
  {EClientType::None,       "None"},
  {EClientType::TestClient, "TestClient"},
  {EClientType::UEClient,   "UEClient"}
})

// EServerType ↔ "None"/"LoginServer"/"GatewayServer"/"GameServer"
NLOHMANN_JSON_SERIALIZE_ENUM(EServerType,
{
  {EServerType::None,          "None"},
  {EServerType::LoginServer,   "LoginServer"},
  {EServerType::GatewayServer, "GatewayServer"},
  {EServerType::GameServer,    "GameServer"}
})

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE
(
	ServerConfig,
	RemoteIp, RemotePort,
	DriverMode, ClientType, ServerType,
	bNoTimeouts, bConnectionlessOnly,
	KeepAliveTime, MaxTickRate,
	InitialConnectTimeout, ConnectionTimeout,
	MaxChannelsSize, DesiredRecvSize,
	DesiredSendSize,
	SessionID, ActiveSecret,
	MagicHeaderSizeBits, MagicHeader, MagicHeaderOffset,
	PacketDropPermille, PacketDuplicatePermille, PacketReorderWindow,
	PacketDropPermilleOutbound,
	PendingConnectionInactivityTimeoutSec,
	TelemetryPort,
	LogLevel, SyntheticTrafficHz
)

struct NetworkSetting
{
    ServerConfig Config;
};

static nlohmann::json ReadJson(std::string FilePath)
{
	std::ifstream jsonIn(FilePath);
	if (!jsonIn)
		return nlohmann::json{};

	nlohmann::json json;
	jsonIn >> json;

	return json;
}

static void ReadNetworkConfig(NetworkSetting& Setting, const std::string Filepath)
{
	//1. Json 파일 읽기
	auto Configjson = ReadJson(Filepath);

	//2. json 정보를 Setting의 Config에 복사
	const auto& Network = Configjson.at("Network");
	Network.get_to(Setting.Config);
}

static std::filesystem::path GetExecutablePath()
{
	std::wstring Path;
	DWORD       Size = MAX_PATH;
	DWORD       Len = 0;

	do
	{
		Path.resize(Size);

		Len = ::GetModuleFileNameW(nullptr, Path.data(), Size);
		if (Len == 0)
		{
			// 오류
			return std::filesystem::path();
		}

		if (Len >= Size)
		{
			Size *= 2;
			continue;
		}

		Path.resize(Len);
		break;

	} while (true);

	if (Path.size() >= MAX_PATH && Path.compare(0, 4, L"\\\\?\\") != 0)
	{
		Path = L"\\\\?\\" + Path;
	}

	return std::filesystem::path(Path);
}

static std::filesystem::path GetExecutableDir()
{
	auto ExePath = GetExecutablePath();
	return ExePath.parent_path();
}

static std::filesystem::path GetConfigJsonPath(const std::filesystem::path& Override = {})
{
	// 1) 매개변수로 명시적 override
	if (!Override.empty() && std::filesystem::exists(Override))
	{
		return Override;
	}

#ifdef _WIN32
	// 2) 환경변수로 override
	size_t len = 0;
	if (_wgetenv_s(&len, nullptr, 0, L"LADELTA_CONFIG_PATH") == 0 && len > 0)
	{
		std::wstring buf(len, L'\0');
		if (_wgetenv_s(&len, buf.data(), buf.size(), L"LADELTA_CONFIG_PATH") == 0 && len > 0)
		{
			if (len > 0) buf.resize(len - 1);

			std::filesystem::path Env(buf);
			if (std::filesystem::exists(Env))
			{
				return Env;
			}
		}
	}
#endif

	auto ExePath = GetExecutablePath();
	auto ExeDir = ExePath.parent_path();
	auto ProjectName = ExePath.stem();
	auto ProjectRoot = ExeDir.parent_path().parent_path();
	auto Candidate = ProjectRoot / "Project" / ProjectName / "config" / "DefaultNetworkEngine.json";
	if (std::filesystem::exists(Candidate))
	{
		return Candidate;
	}
	return GetExecutableDir() / L"Config" / L"DefaultNetworkEngine.json";
}
