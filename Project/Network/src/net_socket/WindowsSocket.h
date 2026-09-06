#pragma once
#include "pch.h"
#include "Sockets.h"

class WindowsSocket : public NetworkSocket
{
public:
	WindowsSocket(SOCKET Socket, ESocketType InSocketType, ESocketInternetProtocolFamily InSocketProtocolFamily);
	WindowsSocket(SOCKET Socket, ESocketType InSocketType, ESocketInternetProtocolFamily InSocketProtocolFamily, std::string& InDescription);
	WindowsSocket(WindowsSocket* InSocket);
	WindowsSocket(std::unique_ptr<WindowsSocket> BaseSocket);
	WindowsSocket(std::unique_ptr<NetworkSocket> BaseSocket);

	WindowsSocket(WindowsSocket&& other) noexcept = default;
	WindowsSocket& operator=(WindowsSocket&& other) noexcept = default;
	
	WindowsSocket(const WindowsSocket&) = delete;
	WindowsSocket& operator=(const WindowsSocket&) = delete;

	virtual ~WindowsSocket() override;

	virtual bool Close() override;
	virtual bool CopyBufView(const NetAddr& Addr) override;
	virtual bool Listen(int32_t MaxBacklog) override;
	void SetSocketDecription(std::string Decription);
	virtual void GetAddress(NetAddr& OutAddr) override;
	virtual bool SetReuseAddr(bool bAllowReuse = true) override;
	bool SetTcpNoDelay(bool flag);
	virtual bool SetNonBlock(bool bNonBlock = true) override;

protected:
	int SendFlags = 0;
};