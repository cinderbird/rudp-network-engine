#pragma once
#include "pch.h"
#include "PacketEnum.h"
#include "PacketProcessor.h"
#include "PendingNetConnectionManager.h"
#include "IDAllocator.h"


struct ParsedHandshakeData;
class NetDriver;
class RecvPacketReader;
class NetAddr;
class PendingRemoteConnection;

struct OutReadyPacket;

class OutSendPacketEvent;

struct ParsedHandshakeData
{
	uint8_t HandshakeTryCount = 0;
	EHandshakePacketType HandshakePacketType = EHandshakePacketType::InitialPacket;

	bool bRestartHandshake = false;
	uint8_t SecretId = 0;
	double TimeStamp = 0.0;

	uint8_t Cookie[COOKIE_BYTE_SIZE]{};
	uint8_t OrigCookie[COOKIE_BYTE_SIZE]{};

public:
	ParsedHandshakeData();
};

class UdpConnectionProcessor : public PacketProcessor
{
	using EHandshakePacketType = EHandshakePacketType;

public:
	UdpConnectionProcessor();

	virtual void IncomingConnectionless(std::shared_ptr<RecvPacketReader> PacketRef) override;
	virtual void Incoming(std::shared_ptr<RecvPacketReader> PacketRef) override;
	virtual void OutgoingConnectionless(BitWriter& Buffer) override;
	virtual void Outgoing(BitWriter& Buffer) override;
	virtual void NotifyHandshakeBegin();
	virtual void Initialize() override;
	virtual void Tick() override;

	void BeginHandshakePacket(BitWriter& HandshakePacket, EHandshakePacketType HandshakePacketType, uint8_t SentHandshakePacketCount, uint32_t SessionID, uint32_t ClientID);
	void CapHandshakePacket(BitWriter& HandshakePacket) const;
	bool ParseHandshakePacket(BitReader& Header, ParsedHandshakeData& OutResult) const;
	void GenerateCookie(const std::shared_ptr<const NetAddr>& ClientAddress, uint8_t SecretId, double LastUpdateTime, uint8_t(&OutCookie)[COOKIE_BYTE_SIZE]) const;

	void SendInitialPacket();
	void SendConnectChallenge(std::shared_ptr<PendingRemoteConnection> PendingClient);
	void SendChallengeResponse(ParsedHandshakeData& HandshakeData, std::shared_ptr<PendingRemoteConnection> PendingClient);
	void SendChallengeAck(std::shared_ptr<PendingRemoteConnection> PendingClient, uint8_t InCookie[COOKIE_BYTE_SIZE]);

	void SendToServer(const std::shared_ptr<const NetAddr> ServerAddress, OutSendPacketEvent* Packet);
	void SendToClient(const std::shared_ptr<const NetAddr> ClientAddress, OutSendPacketEvent* Packet);

	void SetSupervisor(std::shared_ptr<NetDriver> InSupervisor);
	bool HasPassedChallenge(uint64_t Key, std::shared_ptr<PendingRemoteConnection>& PendingClient);
	bool GetHandShakeSecret(uint32_t ScretKeyId, std::vector<uint8_t>& SecretToken) const;

	void ConnectionsUpdate(uint8_t TimeId);
	OutSendPacketEvent* PrepLastHandshakePacket(std::shared_ptr<PendingRemoteConnection> PendingClient, std::shared_ptr<OutReadyPacket> Packet);
	void ResendHandshake(uint8_t TimeId);

private:
	std::shared_ptr<const NetAddr> GetRemoteAddress();

private:
	std::shared_ptr<NetDriver> Driver{};

	uint32_t MagicHeaderSizeBits = 1;
	uint32_t MagicHeaderOffset = 0;

	uint32_t ExpectMagicHeader = 0;
	uint32_t ExpectedSessionId = 0;
	uint32_t ExpectedClientId = 0;

	std::vector<uint8_t> HandshakeSecret;

	uint32_t SessionIDSizeBits = 2;
	uint32_t ClientIDSizeBits = 3;

	float HandshakeResendInterval = 1.f;

	int32_t BaseRandomDataLengthBytes = 16;
	int32_t RandomDataLengthVarianceBytes = 8;

	IDAllocator PendingClientIDManager;
	PendingConnectionManager RemoteManager;

	std::atomic<uint8_t> ClientTimeId{ 59 }; //59부터 시작, 서버는 0부터 삭제 시작, 0삭제 메세지 송출 후 TimeId++ (0 ~ 59)
};
