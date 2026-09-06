#include "PendingNetConnection.h"

PendingRemoteConnection::PendingRemoteConnection(double Time, uint32_t SessionId, uint32_t ClientID, std::shared_ptr<NetAddr> Address, uint8_t TimeId)
	: CookieTime(Time)
	, SessionId(SessionId)
	, ClientID(ClientID)
	, Address(Address)
	, TimeId(TimeId)
	, LastActivityTime(Time)
{
}

PendingRemoteConnection::~PendingRemoteConnection()
{
}

void PendingRemoteConnection::IncreaseHandshakeCount(uint32_t count)
{
	HandshakeTryCount.fetch_add(count, std::memory_order_release);
}

const uint32_t PendingRemoteConnection::GetHandshakeCount()
{
	return HandshakeTryCount.load();
}

const uint8_t PendingRemoteConnection::GetTimdId()
{
	return TimeId.load();
}

void PendingRemoteConnection::UpdateCookieTime(double cookieTime)
{
	CookieTime.store(cookieTime);
}

const double PendingRemoteConnection::GetCookieTime()
{
	return CookieTime.load();
}

void PendingRemoteConnection::UpdateHanshakePacketType(EHandshakePacketType type)
{
	HandshakePacketType.store(type, std::memory_order_release);
}

const EHandshakePacketType PendingRemoteConnection::GetHanshakePacketType()
{
	return HandshakePacketType.load();
}

void PendingRemoteConnection::UpdateHandshakeComplete(bool bComplete)
{
	bHandshakeComplete.store(bComplete, std::memory_order_release);
}

const bool PendingRemoteConnection::GetHanshakeComplete()
{
	return bHandshakeComplete.load();
}

const uint32_t PendingRemoteConnection::GetSessionId()
{
	return SessionId.load();
}

void PendingRemoteConnection::UpdateSessionId(uint32_t Id)
{
	SessionId.store(Id);
}

const uint32_t PendingRemoteConnection::GetClientId()
{
	return ClientID.load();
}

void PendingRemoteConnection::UpdateClientId(uint32_t Id)
{
	ClientID.store(Id);
}

const uint32_t PendingRemoteConnection::GetServerSeq()
{
	return ServerSequence.load();
}

void PendingRemoteConnection::UpdateServerSeq(uint32_t Seq)
{
	ServerSequence.store(Seq);
}

const uint32_t PendingRemoteConnection::GetClientSeq()
{
	return ClientSequence.load();
}

void PendingRemoteConnection::UpdateClientSeq(uint32_t Seq)
{
	ClientSequence.store(Seq);
}

const std::shared_ptr<NetAddr> PendingRemoteConnection::GetAddress()
{
	return Address;
}

void PendingRemoteConnection::PrepLastSendPacket(std::shared_ptr<OutReadyPacket> Packet)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	LastReadyPacket = Packet;
}

const std::shared_ptr<OutReadyPacket> PendingRemoteConnection::GetLastSendPacket()
{
	return LastReadyPacket;
}

void PendingRemoteConnection::Touch(double Now)
{
	LastActivityTime.store(Now, std::memory_order_release);
}

const double PendingRemoteConnection::GetLastActivityTime()
{
	return LastActivityTime.load(std::memory_order_acquire);
}
