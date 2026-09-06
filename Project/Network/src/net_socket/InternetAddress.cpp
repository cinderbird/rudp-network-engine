#include "InternetAddress.h"

bool NetAddr::CompareEndpoints(const NetAddr& InAddr) const
{
    return *this == InAddr;
}

bool NetAddr::CompareEndpoints(std::string StringAddr) const
{
    return this->ToString(true) == StringAddr;
}

bool NetAddr::IsPortValid(int32_t InPort) const
{
    return true;
}

void NetAddr::GetPort(int32_t& OutPort) const
{
    OutPort = GetPort();
}

int32_t NetAddr::GetPlatformPort() const
{
    return GetPort();
}

bool NetAddr::operator==(const NetAddr& Other) const
{
    std::vector<uint8_t> ThisIP = GetRawIp();
    std::vector<uint8_t> OtherIP = Other.GetRawIp();
    
    return ThisIP == OtherIP && GetPort() == Other.GetPort();
}

EInternetProtocolPlags NetAddr::GetProtocolType() const
{
    return EInternetProtocolPlags::None;
}

uint32_t NetAddr::GetTypeHash(const NetAddr& InAddr)
{
    return InAddr.GetTypeHash();
}

