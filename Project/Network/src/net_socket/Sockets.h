#pragma once
#include "pch.h"
#include "SocketTypes.h"

class NetAddr;

using ESocketType = ENetworkProtocolPlags;
using ESocketInternetProtocolFamily = EInternetProtocolPlags;

class NetworkSocket
{
protected:

	struct PlatformSocket
	{
		SOCKET WSocket;
	};

	PlatformSocket Socket;
	ESocketType SocketType;
	ESocketInternetProtocolFamily SocketInternetProtocolFamily;
	ENetworkProtocolPlags SocketProtocol;

	std::string SocketDescription; //Debugging String

public:
	NetworkSocket(SOCKET InSocket);

	NetworkSocket();

	NetworkSocket(ENetworkProtocolPlags InSocketType, EInternetProtocolPlags InSocketProtocolFamily);
	
	NetworkSocket(ENetworkProtocolPlags InSocketType, EInternetProtocolPlags InSocketProtocolFamily, std::string& InDescription);

	NetworkSocket(NetworkSocket* InSocket);

	// std::unique_ptr<NetworkSocket>을 받는 생성자 추가
	explicit NetworkSocket(std::unique_ptr<NetworkSocket> BaseSocket);

	// 이동 생성자 및 이동 할당 연산자 추가
	NetworkSocket(NetworkSocket&& other) noexcept = default;
	NetworkSocket& operator=(NetworkSocket&& other) noexcept = default;

	// 복사 생성자 및 복사 할당 연산자 삭제
	NetworkSocket(const NetworkSocket&) = delete;
	NetworkSocket& operator=(const NetworkSocket&) = delete;



	virtual ~NetworkSocket() {}

	void SetPlatformSocket(PlatformSocket InSocket);
	void SetPlatformSocket(SOCKET InSocket);
	SOCKET GetPlatformSocket();


	virtual bool Close();
	
	virtual bool CopyBufView(const NetAddr& Addr);
	virtual bool Connect(const NetAddr& Addr);
	virtual bool Listen(int32_t MaxBacklog);
	virtual void GetAddress(NetAddr& OutAddr) { }
	virtual bool SetReuseAddr(bool bAllowReuse = true) { return true; }
	virtual bool SetNonBlock(bool bNonBlock = true) { return true; }
	
public:
	ESocketType GetSocketType() const;
	std::string GetDescription() const;
	ENetworkProtocolPlags GetProtocol() const;
	ESocketInternetProtocolFamily GetInternetProtocol() const;
	std::string GetInternetProtocolAsString() const;
};


