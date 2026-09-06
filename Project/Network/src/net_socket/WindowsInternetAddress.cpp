#include <numeric>
#include "WindowsInternetAddress.h"
#include <EngineBaseTypes.h>

void MapIPv4ToIPv6(const uint32_t& InAddress, in6_addr& OutStructure)
{
	::memset(&OutStructure, 0, sizeof(OutStructure));
	OutStructure.s6_addr[10] = 0xff;
	OutStructure.s6_addr[11] = 0xff;
	OutStructure.s6_addr[12] = (static_cast<uint32_t>(InAddress) & 0xFF);
	OutStructure.s6_addr[13] = ((static_cast<uint32_t>(InAddress) >> 8) & 0xFF);
	OutStructure.s6_addr[14] = ((static_cast<uint32_t>(InAddress) >> 16) & 0xFF);
	OutStructure.s6_addr[15] = ((static_cast<uint32_t>(InAddress) >> 24) & 0xFF);
}

void WindowsAddr::Clear()
{
	::memset(&Addr, 0, sizeof(Addr));
	Addr.ss_family = AF_UNSPEC;
}

int32_t WindowsAddr::GetStorageSize() const
{
	if (Addr.ss_family == AF_INET)
		return sizeof(sockaddr_in);
	else if(Addr.ss_family == AF_INET6)
		return sizeof(sockaddr_in6);
	else
		return sizeof(sockaddr_in);
}

EInternetProtocolPlags WindowsAddr::GetProtocolType() const
{
	switch (Addr.ss_family)
	{
		case AF_INET:
		{
			return EInternetProtocolPlags::IPv4;
			break;
		}
		case AF_INET6:
		{
			return EInternetProtocolPlags::IPv6;
			break;
		}
		default:
		{
			return EInternetProtocolPlags::None;
			break;
		}
	}
}

WindowsAddr::WindowsAddr(EInternetProtocolPlags ReqeustedInternetProtocol)
{
	Clear();
	switch (ReqeustedInternetProtocol)
	{
		case EInternetProtocolPlags::None:
		{
			Addr.ss_family = AF_INET;
			break;
		}
		case EInternetProtocolPlags::IPv4:
		{
			Addr.ss_family = AF_INET;
			break;
		}
		case EInternetProtocolPlags::IPv6:
		{
			Addr.ss_family = AF_INET6;
			break;
		}
		default:
		{
			Addr.ss_family = AF_UNSPEC;
			break;
		}
	}
}

bool WindowsAddr::CompareEndpoints(const NetAddr& InAddr) const
{
	const WindowsAddr& OtherBSD = static_cast<const WindowsAddr&>(InAddr);
	if (GetPort() != OtherBSD.GetPort())
	{
		return false;
	}

	if (Addr.ss_family == OtherBSD.Addr.ss_family)
	{
		return *this == InAddr;
	}
	else if (Addr.ss_family == AF_INET || OtherBSD.Addr.ss_family == AF_INET)
	{
		return false;
	}
	return false;
}

std::vector<uint8_t> WindowsAddr::GetRawIp() const
{
	std::vector<uint8_t> RawAddressVector;
	if (Addr.ss_family == AF_INET)
	{
		const sockaddr_in* IPv4Addr = ((const sockaddr_in*)&Addr);
		uint32_t IntAddr = IPv4Addr->sin_addr.s_addr;
		RawAddressVector.push_back((IntAddr >> 0) & 0xFF);
		RawAddressVector.push_back((IntAddr >> 8) & 0xFF);
		RawAddressVector.push_back((IntAddr >> 16) & 0xFF);
		RawAddressVector.push_back((IntAddr >> 24) & 0xFF);
	}
	else if (Addr.ss_family == AF_INET6)
	{
		const sockaddr_in6* IPv6Addr = ((const sockaddr_in6*)&Addr);
		for (int i = 0; i < 16; ++i)
		{
			RawAddressVector.push_back(IPv6Addr->sin6_addr.s6_addr[i]);
		}
	}
	return RawAddressVector;
}

void WindowsAddr::SetIp(const std::wstring& InAddr, bool& bIsValid)
{
	bIsValid = false;
	std::wstring AddrString = InAddr;
	std::wstring PortString;
	
	const size_t FirstColonIndex = AddrString.find_first_of(L":");
	const size_t LastColonIndex = AddrString.find_last_of(L":");
	
	if ((LastColonIndex != std::wstring::npos) &&
	        (AddrString.find(L"]:") != std::wstring::npos || FirstColonIndex == LastColonIndex))
	{
	        PortString = AddrString.substr(LastColonIndex + 1);
	        AddrString = AddrString.substr(0, LastColonIndex);
	}
	
	AddrString.erase(std::remove_if(AddrString.begin(), AddrString.end(),
	        [](wchar_t ch) { return ch == L'[' || ch == L']'; }),
	        AddrString.end());
	
	std::string Converted = ConvertToString(AddrString);
	in_addr IPv4Addr;
	in6_addr IPv6Addr;
	if (inet_pton(AF_INET, Converted.c_str(), &IPv4Addr) == 1)
	{
	        bIsValid = true;
	        SetIp(IPv4Addr);
	}
	else if (inet_pton(AF_INET6, Converted.c_str(), &IPv6Addr) == 1)
	{
	        bIsValid = true;
	        SetIp(IPv6Addr);
	}
	
	if (bIsValid && !PortString.empty())
	{
	        int32_t Port = std::stoi(PortString);
	        if (IsPortValid(Port))
	                SetAddrPort(Port);
	}
}

void WindowsAddr::SetIp(const std::string& InAddr, bool& bIsValid)
{
	std::wstring Converted = ConvertToWString(InAddr);
	SetIp(Converted, bIsValid);
}

void WindowsAddr::Set(const sockaddr_storage& AddrData, int32_t AddrSize)
{
	Clear();
	memcpy(&Addr, &AddrData, (size_t)AddrSize);
}

void WindowsAddr::SetIp(const in_addr& IPv4Addr)
{
	((sockaddr_in*)&Addr)->sin_addr = IPv4Addr;
	Addr.ss_family = AF_INET;
}

void WindowsAddr::SetIp(const in6_addr& IPv6Addr)
{
	((sockaddr_in6*)&Addr)->sin6_addr = IPv6Addr;
	Addr.ss_family = AF_INET6;
}

void WindowsAddr::GetIp(sockaddr_storage& OutAddr) const
{
	OutAddr = Addr;
}

void WindowsAddr::SetAddrPort(int32_t InPort)
{
	if (GetProtocolType() == EInternetProtocolPlags::IPv6)
	{
		((sockaddr_in6*)&Addr)->sin6_port = htons(static_cast<uint16_t>(InPort));
		return;
	}

	((sockaddr_in*)&Addr)->sin_port = htons(static_cast<uint16_t>(InPort));

}

bool WindowsAddr::IsPortValid(int32_t InPort) const
{
	return 0 <= InPort && InPort <= std::numeric_limits<uint16_t>::max();
}

int32_t WindowsAddr::GetPort() const
{
	if (GetProtocolType() == EInternetProtocolPlags::IPv6)
	{
		return ntohs(((sockaddr_in6*)&Addr)->sin6_port);
	}

	return ntohs(((sockaddr_in*)&Addr)->sin_port);
}

void WindowsAddr::SetAnyIPv4Address()
{
	Clear();
	((sockaddr_in*)&Addr)->sin_addr.s_addr = htonl(INADDR_ANY);
	Addr.ss_family = AF_INET;
}

std::string WindowsAddr::ToString(bool bAppendPort) const
{
	std::string Result;
	char IPStr[NI_MAXHOST];
	if (getnameinfo((const sockaddr*)&Addr, GetStorageSize(), IPStr, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST) == 0)
	{
		Result = IPStr;

		if (bAppendPort)
		{
			if (GetProtocolType() == EInternetProtocolPlags::IPv6)
				Result = "[" + Result + "]:" + std::to_string(GetPort());
			else
				Result = Result + ":" + std::to_string(GetPort());
		}
	}
	return Result;
}

uint32_t WindowsAddr::GetScopeId() const
{
	if (Addr.ss_family == AF_INET6)
	{
		return ntohl(((sockaddr_in6*)&Addr)->sin6_scope_id);
	}
	return 0;
}

void WindowsAddr::SetScopeId(uint32_t NewScopeId)
{
	if (Addr.ss_family == AF_INET6)
	{
		((sockaddr_in6*)&Addr)->sin6_scope_id = htonl(NewScopeId);
	}
}

std::string WindowsAddr::GetInternetProtocolTypeAsString()
{
	switch (Addr.ss_family)
	{
	case AF_INET:
	{
		return ::ToString(EInternetProtocolPlags::IPv4);
		break;
	}
	case AF_INET6:
	{
		return ::ToString(EInternetProtocolPlags::IPv6);
		break;
	}
	default:
	{
		return ::ToString(EInternetProtocolPlags::None);
		break;
	}
	}
}

bool WindowsAddr::operator==(const NetAddr& Other) const
{
	const WindowsAddr& OtherBSD = static_cast<const WindowsAddr&>(Other);
	EInternetProtocolPlags CurrentFamily = GetProtocolType();

	// 주소 체계(family)가 같은지 확인
	if (OtherBSD.GetProtocolType() != CurrentFamily)
	{
		return false;
	}

	// 포트가 다르면 그 자리에서 바로 실패.
	if (GetPort() != OtherBSD.GetPort())
	{
		return false;
	}

	if (CurrentFamily == EInternetProtocolPlags::IPv6)
	{
		const sockaddr_in6* OtherBSDAddr = (sockaddr_in6*)&(OtherBSD.Addr);
		const sockaddr_in6* ThisBSDAddr = ((sockaddr_in6*)&Addr);
		return memcmp(&(ThisBSDAddr->sin6_addr), &(OtherBSDAddr->sin6_addr), sizeof(in6_addr)) == 0;
	}

	if (CurrentFamily == EInternetProtocolPlags::IPv4)
	{
		const sockaddr_in* OtherBSDAddr = (sockaddr_in*)&(OtherBSD.Addr);
		const sockaddr_in* ThisBSDAddr = ((sockaddr_in*)&Addr);
		return ThisBSDAddr->sin_addr.s_addr == OtherBSDAddr->sin_addr.s_addr;
	}

	return false;
}

uint32_t WindowsAddr::GetTypeHash() const
{
	//IPv4만
	if (GetProtocolType() == EInternetProtocolPlags::IPv4)
	{
		return ntohl(((sockaddr_in*)&Addr)->sin_addr.s_addr) + (GetPort() * 23);
	}
	else
	{
		return -1; //Error;
	}
}

bool WindowsAddr::IsValid() const
{
	if (Addr.ss_family == AF_INET)
	{
		return ((sockaddr_in*)&Addr)->sin_addr.s_addr != 0;
	}
	return false;
}

std::shared_ptr<NetAddr> WindowsAddr::Clone() const
{
	std::shared_ptr<WindowsAddr> NewAddress = std::make_shared<WindowsAddr>(GetProtocolType());
	NewAddress->Addr = Addr;
	return NewAddress;
}