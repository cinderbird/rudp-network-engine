#pragma once
#include "pch.h"
#include "PacketEnum.h"

class NetAddr;
struct OutReadyPacket;

class PendingRemoteConnection
{
public:
	PendingRemoteConnection(double Time, uint32_t SessionId, uint32_t ClientID, std::shared_ptr<NetAddr> Address, uint8_t TimeId);
	~PendingRemoteConnection();

	void IncreaseHandshakeCount(uint32_t count);
	const uint32_t GetHandshakeCount();

	const uint8_t GetTimdId();
	void UpdateCookieTime(double cookieTime);
	const double GetCookieTime();
	void UpdateHanshakePacketType(EHandshakePacketType type);
	const EHandshakePacketType GetHanshakePacketType();

	void UpdateHandshakeComplete(bool bComplete);
	const bool GetHanshakeComplete();

	const uint32_t GetSessionId();
	void UpdateSessionId(uint32_t Id);

	const uint32_t GetClientId();
	void UpdateClientId(uint32_t Id);

	const uint32_t GetServerSeq();
	void UpdateServerSeq(uint32_t Seq);

	const uint32_t GetClientSeq();
	void UpdateClientSeq(uint32_t Seq);

	const std::shared_ptr<NetAddr> GetAddress();

	void PrepLastSendPacket(std::shared_ptr<OutReadyPacket> Packet);
	const std::shared_ptr<OutReadyPacket> GetLastSendPacket();

	void Touch(double Now);
	const double GetLastActivityTime();

private:
	std::atomic<double> CookieTime{ 0 };
	std::atomic<uint32_t> HandshakeTryCount{ 0 };
	std::atomic<uint8_t> TimeId{ 0 };

	std::atomic<uint32_t> SessionId = 0;
	std::atomic<uint32_t> ClientID = 0;

	std::atomic<int32_t> ServerSequence = 0;
	std::atomic<int32_t> ClientSequence = 0;

	std::shared_ptr<NetAddr> Address;

	std::atomic<EHandshakePacketType> HandshakePacketType{ EHandshakePacketType::InitialPacket };

	std::atomic<bool> bHandshakeComplete{ false };

	std::shared_ptr<OutReadyPacket> LastReadyPacket;

	std::atomic<double> LastActivityTime{ 0 };

	mutable std::mutex Mutex;
};
