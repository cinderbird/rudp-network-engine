#pragma once
#include "pch.h"


#include "WindowsInternetAddress.h"
#include "WindowsSocket.h"
#include "WindowsSocketParams.h"
#include "SocketTypes.h"

enum class ESocketBuildTemplate
{
    ListenSocket,
    ClientSocket,
};

template <ESocketBuildTemplate BT>
struct TSocketFactory;

// Listen Socket (서버 리스닝 소켓)
template <>
struct TSocketFactory<ESocketBuildTemplate::ListenSocket>
{
    static std::unique_ptr<WindowsSocket> Build(ENetworkProtocolPlags PPlag, EInternetProtocolPlags IPPlag, WindowsAddr* Addr, const std::string& SocketDescription = "")
    {
        WindowsSocketParams Params;

        Params.af = (IPPlag == EInternetProtocolPlags::IPv6) ? AF_INET6 : AF_INET;
        if (PPlag == ENetworkProtocolPlags::TCP)
        {
            Params.type = SOCK_STREAM;
            Params.protocol = IPPROTO_TCP;
        }
        else
        {
            Params.type = SOCK_DGRAM;
            Params.protocol = IPPROTO_UDP;
        }
        Params.dwFlags = WSA_FLAG_OVERLAPPED;

        SOCKET RawSocket = WSASocketW(Params.af, Params.type, Params.protocol, Params.lpProtocolInfo, Params.g, Params.dwFlags);
        if (RawSocket == INVALID_SOCKET)
        {
            //std::cerr << "WSASocketA failed: " << WSAGetLastError() << std::endl;
            return nullptr;
        }

        std::unique_ptr<WindowsSocket> NewSocket = std::make_unique<WindowsSocket>(RawSocket, static_cast<ESocketType>(PPlag), static_cast<ESocketInternetProtocolFamily>(IPPlag));
        if (!SocketDescription.empty())
        {
            NewSocket->SetSocketDecription(SocketDescription);
        }

        if (!NewSocket->SetNonBlock(true))
        {
            //std::cerr << "SetNonBlock failed: " << WSAGetLastError() << std::endl;
        }

        if (!NewSocket->SetReuseAddr(true))
        {
            //std::cerr << "SetReuseAddr failed: " << WSAGetLastError() << std::endl;
        }

        if (Addr)
        {
            if (!NewSocket->CopyBufView(*Addr))
            {
                //std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
            }
        }

        return NewSocket;
    }
};

// 클라이언트 소켓 (서버-클라이언트 연결)
template <>
struct TSocketFactory<ESocketBuildTemplate::ClientSocket>
{
    static std::unique_ptr<WindowsSocket> Build(ENetworkProtocolPlags PPlag, EInternetProtocolPlags IPPlag, WindowsAddr* Addr, const std::string& SocketDescription = "")
    {
        WindowsSocketParams Params;

        Params.af = (IPPlag == EInternetProtocolPlags::IPv6) ? AF_INET6 : AF_INET;
        if (PPlag == ENetworkProtocolPlags::TCP)
        {
            Params.type = SOCK_STREAM;
            Params.protocol = IPPROTO_TCP;
        }
        else
        {
            Params.type = SOCK_DGRAM;
            Params.protocol = IPPROTO_UDP;
        }
        Params.dwFlags = WSA_FLAG_OVERLAPPED;

        SOCKET RawSocket = WSASocketW(Params.af, Params.type, Params.protocol, Params.lpProtocolInfo, Params.g, Params.dwFlags);
        if (RawSocket == INVALID_SOCKET)
        {
            //std::cerr << "WSASocketA failed: " << WSAGetLastError() << std::endl;
            return nullptr;
        }

        std::unique_ptr<WindowsSocket> NewSocket = std::make_unique<WindowsSocket>(RawSocket, static_cast<ESocketType>(PPlag), static_cast<ESocketInternetProtocolFamily>(IPPlag));
        if (!SocketDescription.empty())
        {
            NewSocket->SetSocketDecription(SocketDescription);
        }

        if (!NewSocket->SetNonBlock(true))
        {
            //std::cerr << "SetNonBlock failed: " << WSAGetLastError() << std::endl;
        }

        if (!NewSocket->SetReuseAddr(true))
        {
            //std::cerr << "SetReuseAddr failed: " << WSAGetLastError() << std::endl;
        }

        if (Addr)
        {
            if (!NewSocket->CopyBufView(*Addr))
            {
                //std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
            }
        }

        return NewSocket;
    }
};


class   SocketBuilder
{
public:
    virtual ~SocketBuilder() {}

    std::unique_ptr<WindowsSocket> Build(ENetworkProtocolPlags PPlag, EInternetProtocolPlags IPPlag, NetAddr* Addr, const std::string& SocketDescription = "")
    {
        return nullptr;
    }

    template <ESocketBuildTemplate BT>
    static std::unique_ptr<WindowsSocket> BuildTemplate(ENetworkProtocolPlags PPlag, EInternetProtocolPlags IPPlag, NetAddr* Addr, const std::string& SocketDescription = "")
    {
        return TSocketFactory<BT>::Build(PPlag, IPPlag, static_cast<WindowsAddr*>(Addr), SocketDescription);
    }
};
