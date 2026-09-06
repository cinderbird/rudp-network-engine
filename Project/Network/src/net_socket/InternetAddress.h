#pragma once
#include "pch.h"
#include "SocketTypes.h"


class NetAddr
{
public:
	NetAddr() {};
	virtual ~NetAddr() {}

	virtual bool CompareEndpoints(const NetAddr& InAddr) const;
	virtual bool CompareEndpoints(std::string StringAddr) const;
	virtual void SetIp(const in_addr& IPv4Addr) = 0;
	virtual void SetIp(const in6_addr& IPv6Addr) = 0;
	virtual void SetIp(const std::wstring& InAddr, bool& bIsValid) = 0;
	virtual void SetIp(const std::string& InAddr, bool& bIsValid) = 0;

	virtual void GetIp(sockaddr_storage& OutAddr) const = 0;

	virtual void SetAddrPort(int32_t InPort) = 0;
	virtual bool IsPortValid(int32_t InPort) const;
	virtual void GetPort(int32_t& OutPort) const;
	virtual int32_t GetPort() const = 0;

	virtual int32_t GetPlatformPort() const;

	virtual std::vector<uint8_t> GetRawIp() const = 0;

	virtual std::string ToString(bool bAppendPort) const = 0;
	virtual bool operator==(const NetAddr& Other) const;
	
	virtual uint32_t GetTypeHash() const = 0;
	virtual bool IsValid() const = 0;
	virtual std::shared_ptr<NetAddr> Clone() const = 0;
	
	virtual std::string GetInternetProtocolTypeAsString() = 0;

	virtual EInternetProtocolPlags GetProtocolType() const;

	uint32_t GetTypeHash(const NetAddr& InAddr);
};


