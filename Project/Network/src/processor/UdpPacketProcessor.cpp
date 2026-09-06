#include "UdpPacketProcessor.h"
#include <cryptopp/sha.h>
#include <cryptopp/hmac.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h> 
#include "NetDriver.h"
#include "DriverSetting.h"
#include "PacketPipeline.h"
#include "PendingNetConnection.h"
#include <NetPacketBuilder.h>
#include "NetPacketReader.h"
#include "NetConnection.h"
#include "PacketEvent.h"
#include "TelemetrySink.h"


void UdpConnectionProcessor::SetSupervisor(std::shared_ptr<NetDriver> InSupervisor)
{
	Driver = InSupervisor;

	MagicHeaderSizeBits = Driver->GetSetting()->GetMagicHeaderSizeBits();
	ExpectMagicHeader = Driver->GetSetting()->GetMagicHeader();
	MagicHeaderOffset = Driver->GetSetting()->GetMagicHeaderOffset();

	HandshakeSecret.resize(COOKIE_BYTE_SIZE);
	::memcpy(HandshakeSecret.data(), Driver->GetSetting()->GetHandshakeSecret().data(), COOKIE_BYTE_SIZE);
}

void UdpConnectionProcessor::NotifyHandshakeBegin()
{
	SendInitialPacket();
}

void UdpConnectionProcessor::SendInitialPacket()
{
	if (PipelineHandler.lock()->Mode == EProcessorMode::Client)
	{
		NetConnection* ServerConn = (Driver != nullptr ? Driver->GetServerConnection().get() : nullptr);
		{
			if (ServerConn != nullptr)
			{
				std::shared_ptr<PendingRemoteConnection> PendingServerConnection = PendingConnectionManager::CreatePendingClient(
					Driver->GetElapsedTimeSec(),
					0,
					0,
					ServerConn->GetRemoteAddress(),
					0);

				RemoteManager.AddConnection(0, 0, PendingServerConnection);

				auto SendInitialHandshakeProcess = [this, PendingServerConnection](BitWriter& Buffer)
					{
						//step1. Header 작성
						BeginHandshakePacket(Buffer, EHandshakePacketType::InitialPacket, (uint8_t)PendingServerConnection->GetHandshakeCount(), PendingServerConnection->GetSessionId(), PendingServerConnection->GetClientId());

						//step2. Filler 추가
						uint8_t SecretIdPad = 0;
						Buffer.WriteBit(SecretIdPad);
						uint8_t PacketSizeFiller[28]{};
						Buffer.Serialize(PacketSizeFiller, std::size(PacketSizeFiller));

						//step3. RandomBit 및 terminal bit 추가
						CapHandshakePacket(Buffer);
					};

				auto ReadyPacket = ProcessorPacketBuilder::Build(SendInitialHandshakeProcess);
				ReadyPacket->PacketSealed();
				auto CopyPacket = PrepLastHandshakePacket(PendingServerConnection, ReadyPacket);
				SendToServer(GetRemoteAddress(), CopyPacket);

				NET_TELEMETRY_EMIT({
					{"type", "handshake"}, {"stage", "initial"}, {"role", "client"}
				});
			}
		}
	}
}

void UdpConnectionProcessor::SendConnectChallenge(std::shared_ptr<PendingRemoteConnection> PendingClient)
{
	if (Driver != nullptr)
	{
		auto SendConnectChallengeProcess = [this, PendingClient](BitWriter& ChallengeHeader)
			{
				//1. Header 작성
				BeginHandshakePacket(ChallengeHeader, PendingClient->GetHanshakePacketType(), (uint8_t)PendingClient->GetHandshakeCount(), PendingClient->GetSessionId(), PendingClient->GetClientId());

				PendingClient->UpdateCookieTime(Driver->GetElapsedTimeSec());

				uint8_t Cookie[COOKIE_BYTE_SIZE];
				GenerateCookie(PendingClient->GetAddress(), Driver->GetSetting()->GetActiveSecret(), PendingClient->GetCookieTime(), Cookie);

				ChallengeHeader.WriteBit(Driver->GetSetting()->GetActiveSecret());
				double cookieTime = PendingClient->GetCookieTime();
				ChallengeHeader << cookieTime;

				ChallengeHeader.Serialize(Cookie, std::size(Cookie));

				CapHandshakePacket(ChallengeHeader);
			};


		auto ReadyPacket = ProcessorPacketBuilder::Build(SendConnectChallengeProcess);
		ReadyPacket->PacketSealed();
		auto CopyPacket = PrepLastHandshakePacket(PendingClient, ReadyPacket);

		SendToClient(PendingClient->GetAddress(), CopyPacket);
	}
}

void UdpConnectionProcessor::SendChallengeResponse(ParsedHandshakeData& HandshakeData, std::shared_ptr<PendingRemoteConnection> PendingClient)
{
	if (Driver->GetSetting()->IsClient())
	{
		auto SendInitialHandshakeProcess = [this, HandshakeData, PendingClient](BitWriter& ResponseHeader)
			{
				//step1. Header 작성
				EHandshakePacketType HandshakePacketType = EHandshakePacketType::Response;
				BeginHandshakePacket(ResponseHeader, HandshakePacketType, (uint8_t)PendingClient->GetHandshakeCount(), PendingClient->GetSessionId(), PendingClient->GetClientId());

				//step2. Cookie 추가
				ResponseHeader.WriteBit(HandshakeData.SecretId);
				double TimeStamp = HandshakeData.TimeStamp;
				ResponseHeader << TimeStamp;
				ResponseHeader.Serialize((void*)HandshakeData.Cookie, COOKIE_BYTE_SIZE);

				//step3. RandomBit 및 terminal bit 추가
				CapHandshakePacket(ResponseHeader);
			};

		auto ReadyPacket = ProcessorPacketBuilder::Build(SendInitialHandshakeProcess);
		ReadyPacket->PacketSealed();
		OutSendPacketEvent* CopyPacket = PrepLastHandshakePacket(PendingClient, ReadyPacket);
		SendToServer(GetRemoteAddress(), CopyPacket);
	}
}

void UdpConnectionProcessor::SendChallengeAck(std::shared_ptr<PendingRemoteConnection> PendingClient, uint8_t InCookie[COOKIE_BYTE_SIZE])
{
	if (Driver != nullptr)
	{
		auto SendChallengeAckProcess = [this, PendingClient, InCookie](BitWriter& AckPacketHeader)
			{
				//1. Header 작성
				BeginHandshakePacket(AckPacketHeader, PendingClient->GetHanshakePacketType(), (uint8_t)PendingClient->GetHandshakeCount(), PendingClient->GetSessionId(), PendingClient->GetClientId());

				double LastUpdateTime = -1.0;
				uint8_t ActiveSecretUnused = 1;

				AckPacketHeader.WriteBit(ActiveSecretUnused);
				AckPacketHeader << LastUpdateTime;
				AckPacketHeader.Serialize(InCookie, COOKIE_BYTE_SIZE);

				CapHandshakePacket(AckPacketHeader);
			};

		auto ReadyPacket = ProcessorPacketBuilder::Build(SendChallengeAckProcess);
		ReadyPacket->PacketSealed();
		auto CopyPacket = PrepLastHandshakePacket(PendingClient, ReadyPacket);
		SendToClient(PendingClient->GetAddress(), CopyPacket);
	}
}

bool UdpConnectionProcessor::GetHandShakeSecret(uint32_t ScretKeyId, std::vector<uint8_t>& SecretToken) const
{
	if (ScretKeyId < SECRET_COUNT)
	{
		SecretToken = HandshakeSecret;
		return true;
	}

	return false;
}

UdpConnectionProcessor::UdpConnectionProcessor()
	: Driver(nullptr)
	, PendingClientIDManager(IDAllocator())
	, RemoteManager(PendingConnectionManager())
{
}

void UdpConnectionProcessor::IncomingConnectionless(std::shared_ptr<RecvPacketReader> PacketReader)
{
	auto& Header = PacketReader->GetHeader();

	//1. MagicHedaer를 읽는다
	uint32_t MagicHeader = 0;
	Header.SerializeBitsWithOffset(&MagicHeader, MagicHeaderOffset, MagicHeaderSizeBits);
	if (MagicHeader != ExpectMagicHeader)
	{
		Header.SetError();
		return;
	}

	//2. ClientId, SessionId를 읽는다
	uint8_t SessionID = 0;
	uint8_t ClientID = 0;
	Header.SerializeBits(&SessionID, SessionIDSizeBits);
	Header.SerializeBits(&ClientID, ClientIDSizeBits);
	if (!(SessionID == ExpectedSessionId && !Header.IsError()))
	{
		Header.SetError();
		return;
	}

	//3. Handshake 희망여부를 읽는다
	bool bHandshakePacket = !!Header.ReadBit();
	bool bParsePass = false;
	//4. Handshake 정보를 읽는다
	ParsedHandshakeData HandshakeData;
	if (bHandshakePacket && (bParsePass = ParseHandshakePacket(Header, HandshakeData)))
	{
		PacketReader->SetConnectionlessPacket(true);

		const bool bInitialConnect = HandshakeData.HandshakePacketType == EHandshakePacketType::InitialPacket && HandshakeData.TimeStamp == 0.0;
		auto CheckMode = PipelineHandler.lock()->Mode;
		if (CheckMode == EProcessorMode::Server)
		{
			//혹시 이전에 시도했던 클라이언트인가?
			std::shared_ptr<PendingRemoteConnection> NewPendingConnection;
			if (RemoteManager.FindConnection(PacketReader->GetHashKey(), NewPendingConnection))
			{
				NewPendingConnection->IncreaseHandshakeCount(1);
			}
			else
			{
				std::shared_ptr<WindowsAddr> Addr = std::make_shared<WindowsAddr>(PacketReader->GetAddress());
				NewPendingConnection = PendingConnectionManager::CreatePendingClient(Driver->GetElapsedTimeSec(), Driver->GetSetting()->GetSessionID(), PendingClientIDManager.Allocate(), Addr, ClientTimeId.load());
				RemoteManager.AddConnection(NewPendingConnection->GetTimdId(), PacketReader->GetHashKey(), NewPendingConnection);
			}

			NewPendingConnection->Touch(Driver->GetElapsedTimeSec());

			if (bInitialConnect)
			{
				//5. SendConnectionChannlenge
				NewPendingConnection->UpdateHanshakePacketType(EHandshakePacketType::Challenge);

				NETWORK_LOG_DEBUG("SendConnectChallenge, Address: {}, Type: {}, TryCount: {}", NewPendingConnection->GetAddress()->ToString(true), ToString(NewPendingConnection->GetHanshakePacketType()), NewPendingConnection->GetHandshakeCount());
				NET_TELEMETRY_EMIT({
					{"type", "handshake"}, {"stage", "challenge"}, {"role", "server"}
				});
				SendConnectChallenge(NewPendingConnection);
			}
			else if (!bInitialConnect && HandshakeData.HandshakePacketType == EHandshakePacketType::Response)
			{
				//6. 쿠키 유효 시간 확인
				bool bChallengeSuccess = false;
				const double CookieDelta = Driver->GetElapsedTimeSec() - NewPendingConnection->GetCookieTime();
				const bool bValidCookieLifetime = CookieDelta >= 0.0 && (MAX_COOKIE_LIFETIME - CookieDelta) > 0.0;

				if (bValidCookieLifetime)
				{
					uint8_t RegenCookie[COOKIE_BYTE_SIZE];
					std::shared_ptr<const NetAddr> CopyAddress = nullptr;
					double CopyCookieTime = 0;

					CopyAddress = NewPendingConnection->GetAddress();
					CopyCookieTime = NewPendingConnection->GetCookieTime();

					GenerateCookie(CopyAddress, Driver->GetSetting()->GetActiveSecret(), CopyCookieTime, RegenCookie);
					//7. 유효 쿠키 확인
					bChallengeSuccess = ::memcmp(HandshakeData.Cookie, RegenCookie, COOKIE_BYTE_SIZE) == 0;

					//8. SendChallengeAck
					if (bChallengeSuccess)
					{
						NewPendingConnection->UpdateHanshakePacketType(EHandshakePacketType::Ack);
						NETWORK_LOG_DEBUG("SendChallengeAck, Address: {}, Type: {}, TryCount: {}", NewPendingConnection->GetAddress()->ToString(true), ToString(NewPendingConnection->GetHanshakePacketType()), NewPendingConnection->GetHandshakeCount());
					NET_TELEMETRY_EMIT({
						{"type", "handshake"}, {"stage", "ack"}, {"role", "server"}
					});

						NewPendingConnection->UpdateServerSeq(*(HandshakeData.Cookie) & (MAX_PACKETID - 1));
						NewPendingConnection->UpdateClientSeq(*(HandshakeData.Cookie + 1) & (MAX_PACKETID - 1));

						NewPendingConnection->UpdateHandshakeComplete(true);
						//9. Ack를 먼저 보내버리면 경우에 따라 AddClient가 완료되기 전에 NMT가 도착한다

						Driver->AddClient(PacketReader->GetHashKey(), NewPendingConnection);

						SendChallengeAck(NewPendingConnection, RegenCookie);
					}
					else
					{
						NETWORK_LOG_WARN("Invalid Cookie, Address: {}, Type: {}, TryCount: {}", NewPendingConnection->GetAddress()->ToString(true), ToString(NewPendingConnection->GetHanshakePacketType()), NewPendingConnection->GetHandshakeCount());
					NET_TELEMETRY_EMIT({
						{"type", "handshake"}, {"stage", "invalid_cookie"}, {"role", "server"}
					});
					}
				}
				else
				{
					NETWORK_LOG_WARN("Invalid Cookie Lifetime, Address: {}, Type: {}, TryCount: {}", NewPendingConnection->GetAddress()->ToString(true), ToString(NewPendingConnection->GetHanshakePacketType()), NewPendingConnection->GetHandshakeCount());
				}
			}
			else
			{
				NETWORK_LOG_WARN("Invalid Response Condition, Address: {}, Type: {}, TryCount: {}", NewPendingConnection->GetAddress()->ToString(true), ToString(NewPendingConnection->GetHanshakePacketType()), NewPendingConnection->GetHandshakeCount());
			}
		}
		else
		{
			NETWORK_LOG_WARN("Invalid Mode");
		}
	}
	else
	{
		if (!bHandshakePacket)
		{
			NETWORK_LOG_WARN("Not a Handshake Packet, Address: {}", PacketReader->GetAddress().ToString(true));
		}
		if (!bParsePass)
		{
			NETWORK_LOG_WARN("Fail to pass Parse, Address: {}", PacketReader->GetAddress().ToString(true));
		}
	}
}

void UdpConnectionProcessor::Incoming(std::shared_ptr<RecvPacketReader> PacketReader)
{
	auto& Header = PacketReader->GetHeader();

	//1. MagicNumber를 읽는다
	uint32_t ReadMagic = 0;
	Header.SerializeBitsWithOffset(&ReadMagic, MagicHeaderOffset, MagicHeaderSizeBits);
	if (ReadMagic != ExpectMagicHeader)
	{
		Header.SetError();
		return;
	}

	//2. SesisonID
	uint8_t SessionID = 0;
	Header.SerializeBits(&SessionID, SessionIDSizeBits);

	//3. ClientID
	uint8_t ClientID = 0;
	Header.SerializeBits(&ClientID, ClientIDSizeBits);

	//4. HandShake 여부
	ParsedHandshakeData HandshakeData;
	if (bool bHandshakePacket = !!Header.ReadBit() && !Header.IsError() && ParseHandshakePacket(Header, HandshakeData))
	{
		PacketReader->SetConnectionlessPacket(true);

		const bool bIsChallengePacket = HandshakeData.HandshakePacketType == EHandshakePacketType::Challenge && HandshakeData.TimeStamp > 0.0;

		std::shared_ptr<PendingRemoteConnection> NewPendingConnection;
		const auto Key = (Driver->IsServer()) ? PacketReader->GetHashKey() : 0;
		if (!(RemoteManager.FindConnection(Key, NewPendingConnection)))
		{
			NETWORK_LOG_WARN("UdpConnectionProcessor::Incoming - no PendingRemoteConnection found for Key: {}, Address: {} -- dropping packet (see KNOWN_ISSUES.md ISSUE-5)", Key, PacketReader->GetAddress().ToString(true));
			return;
		}

		NewPendingConnection->Touch(Driver->GetElapsedTimeSec());

		if (PipelineHandler.lock()->Mode == EProcessorMode::Client)
		{
			if (State == EProcessorState::UnInitialized || State == EProcessorState::InitializedOnLocal)
			{
				if (bIsChallengePacket)
				{
					NETWORK_LOG_DEBUG("SendChallengeResponse, Address: {}, Type: {}, TryCount: {}", NewPendingConnection->GetAddress()->ToString(true), ToString(NewPendingConnection->GetHanshakePacketType()), NewPendingConnection->GetHandshakeCount());
					NET_TELEMETRY_EMIT({
						{"type", "handshake"}, {"stage", "response"}, {"role", "client"}
					});

					NewPendingConnection->UpdateHanshakePacketType(EHandshakePacketType::Response);

					SendChallengeResponse(HandshakeData, NewPendingConnection);
					SetState(EProcessorState::InitializedOnLocal);
				}
				else if (HandshakeData.HandshakePacketType == EHandshakePacketType::Ack && HandshakeData.TimeStamp < 0.0)
				{
					NetConnection* ServerConn = (Driver != nullptr ? Driver->GetServerConnection().get() : nullptr);

					if (ServerConn != nullptr)
					{
						int32_t ServerSequence = *(HandshakeData.Cookie) & (MAX_PACKETID - 1);
						int32_t ClientSequence = *(HandshakeData.Cookie + 1) & (MAX_PACKETID - 1);

						if (ServerConn->InitSequence(ServerSequence, ClientSequence))
						{
							NewPendingConnection->UpdateHanshakePacketType(EHandshakePacketType::Ack);
							NewPendingConnection->UpdateHandshakeComplete(true);

							SetState(EProcessorState::Initialized);

							NETWORK_LOG_DEBUG("3-way handshake end, NMT Start, Address: {}, Type: {}, TryCount: {}", NewPendingConnection->GetAddress()->ToString(true), ToString(NewPendingConnection->GetHanshakePacketType()), NewPendingConnection->GetHandshakeCount());
							NET_TELEMETRY_EMIT({
								{"type", "handshake"}, {"stage", "complete"}, {"role", "client"}
							});

							Driver->GetConnectionlessHandler()->BeginNMTHello();
						}
					}
				}
			}
		}
		else if (PipelineHandler.lock()->Mode == EProcessorMode::Server && HandshakeData.HandshakePacketType == EHandshakePacketType::Response && NewPendingConnection->GetHanshakePacketType() == EHandshakePacketType::Ack)
		{
			uint8_t RegenCookie[COOKIE_BYTE_SIZE];
			std::shared_ptr<const NetAddr> CopyAddress = NewPendingConnection->GetAddress();

			GenerateCookie(CopyAddress, Driver->GetSetting()->GetActiveSecret(), NewPendingConnection->GetCookieTime(), RegenCookie);

			bool bValidCookie = ::memcmp(HandshakeData.Cookie, RegenCookie, COOKIE_BYTE_SIZE) == 0;

			if (bValidCookie)
			{
				NETWORK_LOG_DEBUG("SERVER:RESEND:SendChallengeAck, Address: {}, Type: {}, TryCount: {}", NewPendingConnection->GetAddress()->ToString(true), ToString(NewPendingConnection->GetHanshakePacketType()), NewPendingConnection->GetHandshakeCount());
				SendChallengeAck(NewPendingConnection, RegenCookie);
			}
		}

		//4. HandshakePacket의 경우 더 읽을 필요 없다. End로 설정 후 종료
		Header.SetAtEnd();
	}
}

void UdpConnectionProcessor::OutgoingConnectionless(BitWriter& Buffer)
{
	//아무것도 안 함
}

void UdpConnectionProcessor::Outgoing(BitWriter& Buffer)
{
	//1. MagicHeader
	uint32_t ReadMagic = 0;
	Buffer.SerializeBitsWithOffset(&ReadMagic, MagicHeaderOffset, MagicHeaderSizeBits);

	if (ReadMagic != ExpectMagicHeader)
	{
		Buffer.SetError();
		return;
	}

	//2. SessionId
	uint8_t SessionID = 0;
	Buffer.SerializeBits(&SessionID, SessionIDSizeBits);

	//3. ClientId
	uint8_t ClientID = 0;
	Buffer.SerializeBits(&ClientID, ClientIDSizeBits);

	//4. HandShake 여부
	bool bHandShakePacket = false;
	Buffer.SerializeBits(&bHandShakePacket, 1);
}

void UdpConnectionProcessor::Initialize()
{
	if (PipelineHandler.lock()->Mode == EProcessorMode::Server)
	{
		bInitialized = true;
		SetState(EProcessorState::Initialized);
	}
}

void UdpConnectionProcessor::Tick()
{
	//Status...
}

void UdpConnectionProcessor::BeginHandshakePacket(BitWriter& HandshakePacket, EHandshakePacketType HandshakePacketType, uint8_t SentHandshakePacketCount, uint32_t SessionID, uint32_t ClientID)
{
	uint8_t bHandshakePacket = 1;
	uint8_t bRestartHandshake = 0;
	uint8_t PacketType = static_cast<uint8_t>(HandshakePacketType);

	HandshakePacket.SerializeBitsWithOffset(&ExpectMagicHeader, MagicHeaderOffset, MagicHeaderSizeBits);

	HandshakePacket.SerializeBits(&SessionID, SessionIDSizeBits);

	HandshakePacket.SerializeBits(&ClientID, ClientIDSizeBits);

	HandshakePacket.WriteBit(bHandshakePacket);

	HandshakePacket.WriteBit(bRestartHandshake);

	HandshakePacket.SerializeBits(&PacketType, 8);

	HandshakePacket.SerializeBits(&SentHandshakePacketCount, 8);
}

void UdpConnectionProcessor::CapHandshakePacket(BitWriter& HandshakePacket) const
{
	static thread_local std::mt19937 gen(std::random_device{}());
	std::uniform_int_distribution<int32_t> randomLen(0, RandomDataLengthVarianceBytes);
	int32_t DataLengthBytes = randomLen(gen);
	int32_t RandomDataLengthBytes = BaseRandomDataLengthBytes - DataLengthBytes;

	std::uniform_int_distribution<int> randomByte(0, 255);

	for (int32_t RandIdx = 0; RandIdx < RandomDataLengthBytes; RandIdx++)
	{
		uint8_t RandVal = randomByte(gen);
		HandshakePacket << RandVal;
	}

	HandshakePacket.WriteBit(1);
}

void UdpConnectionProcessor::GenerateCookie(const std::shared_ptr<const NetAddr>& ClientAddress, uint8_t SecretId, double LastUpdateTime, uint8_t(&OutCookie)[COOKIE_BYTE_SIZE]) const
{
	using namespace CryptoPP;
	std::vector<uint8_t> KeyVec;

	if (!GetHandShakeSecret(SecretId, KeyVec))
		return;

	std::string InputData;
	InputData.append(std::to_string(LastUpdateTime));
	InputData.append(ClientAddress->ToString(true));

	HMAC<SHA1> hmac(KeyVec.data(), KeyVec.size());
	hmac.CalculateDigest(OutCookie, reinterpret_cast<const uint8_t*>(InputData.data()), InputData.size());

	std::string hexDigest;
	StringSource ss(OutCookie, SHA1::DIGESTSIZE, true,
		new HexEncoder(
			new StringSink(hexDigest),
			false
		)
	);
}

void UdpConnectionProcessor::SendToServer(const std::shared_ptr<const NetAddr> ServerAddress, OutSendPacketEvent* Packet)
{
	if (Driver != nullptr)
	{
		Driver->SendToRemote(ServerAddress, Packet, true);
	}
}

void UdpConnectionProcessor::SendToClient(const std::shared_ptr<const NetAddr> ClientAddress, OutSendPacketEvent* Packet)
{
	if (Driver != nullptr)
	{
		Driver->SendToRemote(ClientAddress, Packet, true);
	}
}

bool UdpConnectionProcessor::ParseHandshakePacket(BitReader& Header, ParsedHandshakeData& OutResult) const
{
	OutResult.bRestartHandshake = !!Header.ReadBit();

	uint8_t HandshakePacketType = 0;
	Header << HandshakePacketType;
	Header << OutResult.HandshakeTryCount;

	EHandshakePacketType& HandshakePacketTypeEnum = OutResult.HandshakePacketType;
	HandshakePacketTypeEnum = static_cast<EHandshakePacketType>(HandshakePacketType);

	const bool bHandshakePacket = (HandshakePacketTypeEnum == EHandshakePacketType::InitialPacket ||
		HandshakePacketTypeEnum == EHandshakePacketType::Challenge || HandshakePacketTypeEnum == EHandshakePacketType::Response ||
		HandshakePacketTypeEnum == EHandshakePacketType::Ack);

	if (bHandshakePacket)
	{
		OutResult.SecretId = Header.ReadBit();
		Header << OutResult.TimeStamp;
		Header.Serialize(OutResult.Cookie, COOKIE_BYTE_SIZE);
	}

	return !Header.IsError();
}

bool UdpConnectionProcessor::HasPassedChallenge(uint64_t Key, std::shared_ptr<PendingRemoteConnection>& PendingClient)
{
	return RemoteManager.FindConnection(Key, PendingClient);
}

void UdpConnectionProcessor::ConnectionsUpdate(uint8_t TimeId)
{
	//1. Lifetime이 지난 모든 Connection 제거 (M18: 활동 없는 것만 -- ISSUE-5 참고)
	if (Driver->IsServer())
	{
		RemoteManager.TimeKillEvent(TimeId, PendingClientIDManager, Driver->GetElapsedTimeSec(), Driver->GetSetting()->GetPendingConnectionInactivityTimeoutSec());
	}

	//2. 주기(1.0s)마다 패킷 재전송
	ResendHandshake(ClientTimeId.load()); 
	ClientTimeId.fetch_add(((ClientTimeId + 1) % 60), std::memory_order_release); //atomic
}

OutSendPacketEvent* UdpConnectionProcessor::PrepLastHandshakePacket(std::shared_ptr<PendingRemoteConnection> pendingClient, std::shared_ptr<OutReadyPacket> packet)
{
	pendingClient->PrepLastSendPacket(packet);

	auto SendEvent = OutSendPacketEvent::Create(packet);
	return SendEvent;
}

void UdpConnectionProcessor::ResendHandshake(uint8_t TimeId)
{
	if (Driver->IsClient())
	{
		const auto RemoteHashMap = RemoteManager.GetHashMapbyTimeId(0);

		for (const auto& It : RemoteHashMap)
		{
			const auto& ServerStatelessConnection = It.second;
			auto SendPacket = PrepLastHandshakePacket(ServerStatelessConnection, ServerStatelessConnection->GetLastSendPacket());
			Driver->SendToRemote(ServerStatelessConnection->GetAddress(), SendPacket, true);
		}
	}
}

std::shared_ptr<const NetAddr> UdpConnectionProcessor::GetRemoteAddress()
{
	return Driver->GetServerConnection()->GetRemoteAddress();
}

ParsedHandshakeData::ParsedHandshakeData()
{
}


