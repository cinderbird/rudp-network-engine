#include "NetConnectionResolution.h"
#include <WindowsInternetAddress.h>
#include <PacketEnum.h>
#include "WindowsSocket.h"

bool GameNetConnectionAddressResolution::InitLocalConnection(std::string remoteAddr)
{
    bool bValidInit = true;
    RemoteAddr = std::make_shared<WindowsAddr>(EInternetProtocolPlags::IPv4);

    RemoteAddr->SetIp(remoteAddr, bValidInit);

    return bValidInit;
}

std::shared_ptr<NetAddr> GameNetConnectionAddressResolution::GetRemoteAddr() const
{
    return RemoteAddr;
}

bool GameNetConnectionAddressResolution::IsAddressResolutionEnabled() const
{
    return ResolutionState != EAddressResolutionState::Disabled;
}

ECheckAddressResolutionResult GameNetConnectionAddressResolution::CheckAddressResolution()
{
    ECheckAddressResolutionResult Result = ECheckAddressResolutionResult::None;

    if (ResolutionState == EAddressResolutionState::TryNextAddress)
    {
        RemoteAddr = ResolverResults[CurrentAddressIndex];


        ResolutionUdpSocket.reset();

        for (const std::shared_ptr<NetworkSocket>& BindSocket : BindSockets)
        {
            if (BindSocket->GetInternetProtocolAsString() == RemoteAddr->GetInternetProtocolTypeAsString() && BindSocket->GetSocketType() == ENetworkProtocolPlags::UDP)
            {
                ResolutionUdpSocket = BindSocket;
                break;
            }
        }

        if (ResolutionUdpSocket != nullptr)
        {
            ResolutionState = EAddressResolutionState::Connecting;

            if (CurrentAddressIndex == 0)
            {
                Result = ECheckAddressResolutionResult::TryFirstAddress;
            }
            else
            {
                Result = ECheckAddressResolutionResult::TryNextAddress;
            }

            ++CurrentAddressIndex;
        }
        else
        {
            ResolutionState = EAddressResolutionState::Error;
            Result = ECheckAddressResolutionResult::FindSocketError;
        }
    }
    else if (ResolutionState == EAddressResolutionState::Connected)
    {
        ResolutionState = EAddressResolutionState::Done;
        Result = ECheckAddressResolutionResult::Connected;

        CleanupResolutionSockets();
    }
    else if (ResolutionState == EAddressResolutionState::Error)
    {
        ResolutionState = EAddressResolutionState::Done;
        Result = ECheckAddressResolutionResult::Error;
    }

    return Result;
}

std::shared_ptr<NetworkSocket> GameNetConnectionAddressResolution::GetResolutionUdpSocket()
{
    return ResolutionUdpSocket;
}

bool GameNetConnectionAddressResolution::IsAddressResolutionComplete() const
{
    return ResolutionState == EAddressResolutionState::Done;
}

void GameNetConnectionAddressResolution::NotifyAddressResolutionConnected()
{
    if (ResolutionState == EAddressResolutionState::Connecting)
    {
        ResolutionState = EAddressResolutionState::Connected;
    }
}

void GameNetConnectionAddressResolution::CleanupResolutionSockets()
{
    if (IsAddressResolutionEnabled())
    {
        ResolutionUdpSocket.reset();
        BindSockets.fill(nullptr);
        ResolverResults.clear();
    }
}

void GameNetConnectionAddressResolution::DisableAddressResolution()
{
    ResolutionState = EAddressResolutionState::Disabled;
}

