#include "Sockets.h"

ESocketType NetworkSocket::GetSocketType() const
{
    return SocketType;
}

std::string NetworkSocket::GetDescription() const
{
    return SocketDescription;
}

ENetworkProtocolPlags NetworkSocket::GetProtocol() const
{
    return SocketProtocol;
}

ESocketInternetProtocolFamily NetworkSocket::GetInternetProtocol() const
{
    return SocketInternetProtocolFamily;
}

std::string NetworkSocket::GetInternetProtocolAsString() const
{
    return ToString(SocketInternetProtocolFamily);
}

NetworkSocket::NetworkSocket(SOCKET InSocket)
{
    Socket.WSocket = InSocket;
    SocketType = ESocketType::None;
    SocketInternetProtocolFamily = ESocketInternetProtocolFamily::None;
    SocketProtocol = ENetworkProtocolPlags::None;
}

NetworkSocket::NetworkSocket()
    : SocketType(ESocketType::None)
    , SocketInternetProtocolFamily(ESocketInternetProtocolFamily::None)
{
    SocketProtocol = ENetworkProtocolPlags::None;
    Socket.WSocket = INVALID_SOCKET;
}

NetworkSocket::NetworkSocket(ENetworkProtocolPlags InSocketType, EInternetProtocolPlags InSocketProtocolFamily)
    : SocketType(InSocketType)
    , SocketInternetProtocolFamily(InSocketProtocolFamily)
{
    SocketProtocol = ENetworkProtocolPlags::None;
    Socket.WSocket = INVALID_SOCKET;
}

NetworkSocket::NetworkSocket(ENetworkProtocolPlags InSocketType, EInternetProtocolPlags InSocketProtocolFamily, std::string& InDescription)
    : SocketType(InSocketType)
    , SocketInternetProtocolFamily(InSocketProtocolFamily)
    , SocketDescription(InDescription)
{
    SocketProtocol = ENetworkProtocolPlags::None;
    Socket.WSocket = INVALID_SOCKET;
}

NetworkSocket::NetworkSocket(NetworkSocket* InSocket)
{
    Socket = InSocket->Socket;
    SocketType = InSocket->SocketType;
    SocketInternetProtocolFamily = InSocket->SocketInternetProtocolFamily;
    SocketProtocol = InSocket->SocketProtocol;
}

NetworkSocket::NetworkSocket(std::unique_ptr<NetworkSocket> BaseSocket)
{
    SocketType = BaseSocket.get()->SocketType;
    SocketInternetProtocolFamily = BaseSocket.get()->SocketInternetProtocolFamily;
    SocketDescription = BaseSocket.get()->SocketDescription;
    SocketProtocol = BaseSocket.get()->SocketProtocol;
    Socket.WSocket = BaseSocket.get()->Socket.WSocket;
}

void NetworkSocket::SetPlatformSocket(PlatformSocket InSocket)
{
    Socket.WSocket = InSocket.WSocket;
}

void NetworkSocket::SetPlatformSocket(SOCKET InSocket)
{
    Socket.WSocket = InSocket;
}

SOCKET NetworkSocket::GetPlatformSocket()
{
    return Socket.WSocket;
}

bool NetworkSocket::Close()
{
    return true;
}

bool NetworkSocket::CopyBufView(const NetAddr& Addr)
{
    return true;
}

bool NetworkSocket::Connect(const NetAddr& Addr)
{
    return true;
}

bool NetworkSocket::Listen(int32_t MaxBacklog)
{
    return true;
}