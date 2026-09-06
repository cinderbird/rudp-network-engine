#pragma once
#include "pch.h"
#include "InternetAddress.h"

class WindowsAddr : public NetAddr
{
	friend class WindowsSocket;
private:
	sockaddr_storage Addr;

protected:
	void Clear();
	void SetIp(const in_addr& IPv4Addr); //Sets the ip address using a network byte order ipv4 address
	void SetIp(const in6_addr& IPv6Addr);//Sets the ip address using a network byte order ipv6 address

public:
	WindowsAddr(EInternetProtocolPlags ReqeustedInternetProtocol = EInternetProtocolPlags::None);

	WindowsAddr(const WindowsAddr&) noexcept = default;
	WindowsAddr(WindowsAddr&&) noexcept = default;
	WindowsAddr& operator=(const WindowsAddr&) noexcept = default;
	WindowsAddr& operator=(WindowsAddr&&) noexcept = default;

	virtual bool CompareEndpoints(const NetAddr& InAddr) const override; //Compare Addr together, comparing the logical net addresses (endpoints) of the data stored, rather than doing a memory comparison like the equality operator does.

	virtual std::vector<uint8_t> GetRawIp() const override; //Gets the ip address in a raw array stored in network byte order.
	virtual void SetIp(const std::wstring& InAddr, bool& bIsValid) override;
	virtual void SetIp(const std::string& InAddr, bool& bIsValid) override;
	virtual void Set(const sockaddr_storage& AddrData, int32_t AddrSize);

	//IPv4
	virtual void GetIp(sockaddr_storage& OutAddr) const override;

	virtual void SetAddrPort(int32_t InPort) override;
	virtual bool IsPortValid(int32_t InPort) const override;
	virtual int32_t GetPort() const override;

	void SetAnyIPv4Address();

	virtual std::string ToString(bool bAppendPort) const override;
	virtual bool operator==(const NetAddr& Other) const override;
	virtual bool IsValid() const override;
	virtual std::shared_ptr<NetAddr> Clone() const override;

	virtual EInternetProtocolPlags GetProtocolType() const override;
	virtual std::string GetInternetProtocolTypeAsString() override;

	int32_t GetStorageSize() const;
	void SetScopeId(uint32_t NewScopeId);
	uint32_t GetScopeId() const;
	virtual uint32_t GetTypeHash() const override;
};


