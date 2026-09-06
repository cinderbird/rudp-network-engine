#include "WindowsSocket.h"
#include "WindowsInternetAddress.h"

WindowsSocket::WindowsSocket(SOCKET Socket, ESocketType InSocketType, ESocketInternetProtocolFamily InSocketProtocolFamily)
	: NetworkSocket(InSocketType, InSocketProtocolFamily)
{
	SetPlatformSocket(Socket);
}

WindowsSocket::WindowsSocket(SOCKET Socket, ESocketType InSocketType, ESocketInternetProtocolFamily InSocketProtocolFamily, std::string& InDescription)
	: NetworkSocket(InSocketType, InSocketProtocolFamily, InDescription)
{
	SetPlatformSocket(Socket);
}

WindowsSocket::WindowsSocket(WindowsSocket* InSocket)
{
	Socket = InSocket->Socket;
	SocketType = InSocket->SocketType;
	SocketInternetProtocolFamily = InSocket->SocketInternetProtocolFamily;
	SocketProtocol = InSocket->SocketProtocol;
}

WindowsSocket::WindowsSocket(std::unique_ptr<WindowsSocket> BaseSocket)
{
	SocketType = BaseSocket.get()->SocketType;
	SocketInternetProtocolFamily = BaseSocket.get()->SocketInternetProtocolFamily;
	SocketDescription = BaseSocket.get()->SocketDescription;
	SocketProtocol = BaseSocket.get()->SocketProtocol;
	Socket.WSocket = BaseSocket.get()->Socket.WSocket;
}

WindowsSocket::WindowsSocket(std::unique_ptr<NetworkSocket> BaseSocket)
	: NetworkSocket(std::move(BaseSocket)) {
}

WindowsSocket::~WindowsSocket()
{
	Close();
}

bool WindowsSocket::Close()
{
	if (GetPlatformSocket() != INVALID_SOCKET)
	{
		int32_t error = closesocket(GetPlatformSocket());
		//GetPlatformSocketPtr() = INVALID_SOCKET;
		return error == 0;
	}
	return false;
}

bool WindowsSocket::CopyBufView(const NetAddr& Addr)
{
	if (Addr.GetProtocolType() != GetInternetProtocol())
	{
		return false;
	}

	const WindowsAddr& BSDAddr = static_cast<const WindowsAddr&>(Addr);
	return bind(GetPlatformSocket(), (const sockaddr*)&(BSDAddr.Addr), BSDAddr.GetStorageSize()) == 0;
}

bool WindowsSocket::Listen(int32_t MaxBacklog = SOMAXCONN)
{
	::listen(GetPlatformSocket(), SOMAXCONN);
	return true;
}

void WindowsSocket::SetSocketDecription(std::string Decription)
{
	SocketDescription = Decription;
}

void WindowsSocket::GetAddress(NetAddr& OutAddr)
{
	WindowsAddr& Addr = static_cast<WindowsAddr&>(OutAddr);
	int32_t Size = sizeof(sockaddr_storage);

	bool bOk = getsockname(GetPlatformSocket(), (sockaddr*)&Addr.Addr, &Size) == 0;
}

bool WindowsSocket::SetReuseAddr(bool bAllowReuse)
{
	int reuse = 1;
	if (setsockopt(GetPlatformSocket(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)) == SOCKET_ERROR)
	{
		return false;
	}
	else
	{
		return true;
	}

}

bool WindowsSocket::SetTcpNoDelay(bool flag)
{
	int opt = flag ? 1 : 0;
	return ::setsockopt(GetPlatformSocket(), SOL_SOCKET, TCP_NODELAY, reinterpret_cast<char*>(&opt), sizeof(opt)) == 0;
}

bool WindowsSocket::SetNonBlock(bool bNonBlock)
{
	if (bNonBlock)
	{
		u_long NonBlocking = 1;
		int Result = ioctlsocket(GetPlatformSocket(), FIONBIO, &NonBlocking);
		if (Result != NO_ERROR)
		{
			::closesocket(GetPlatformSocket());
			return false;
		}
	}
	return true;
}