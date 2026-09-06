#pragma once
#include <string>

enum class Type
{
	None,
	Reject,
	Accept,
	Ignore
};

inline const std::string ToString(Type EnumVal)
{
	switch (EnumVal)
	{
	case Type::Reject:
	{
		return "Reject";
		break;
	}
	case Type::Accept:
	{
		return "Accept";
		break;
	}

	case Type::Ignore:
	{
		return "Ignore";
		break;
	}
	default:
	{
		return "None";
		break;
	}


	}
}

enum class EInitBindSocketsFlags : uint16_t
{
	Client = 0x0001,
	Server = 0x0002
};
enum class EOperatingPlatformFlags : uint8_t
{
	None = 0x000,
	Windows = 0x001,
	Linux = 0x002,
};

inline const std::string ToString(EOperatingPlatformFlags Flags)
{
	switch (Flags)
	{
	case EOperatingPlatformFlags::None:
	{
		return "NONE";
		break;
	}
	case EOperatingPlatformFlags::Windows:
	{
		return "WINDOWS";
		break;
	}
	case EOperatingPlatformFlags::Linux:
	{
		return "LINUX";
		break;
	}
	default:
	{
		return "NONE";
		break;
	}
	}
}

enum class ENetworkProtocolPlags : uint8_t
{
	None = 0x000,
	TCP = 0x001,
	UDP = 0x002,
};

enum class EInternetProtocolPlags : uint8_t
{
	None = 0x000,
	IPv4 = 0x001,
	IPv6 = 0x002,
};

inline const std::string ToString(ENetworkProtocolPlags Flags)
{
	switch (Flags)
	{
		case ENetworkProtocolPlags::None:
		{
			return "NONE";
			break;
		}
		case ENetworkProtocolPlags::TCP:
		{
			return "TCP";
			break;
		}
		case ENetworkProtocolPlags::UDP:
		{
			return "UDP";
			break;
		}
		default:
		{
			return "NONE";
			break;
		}
	}
}

inline const std::string ToString(EInternetProtocolPlags Flags)
{
	switch (Flags)
	{
	case EInternetProtocolPlags::None:
	{
		return "None";
		break;
	}
	case EInternetProtocolPlags::IPv4:
	{
		return "IPv4";
		break;
	}

	case EInternetProtocolPlags::IPv6:
	{
		return "IPv6";
		break;
	}
	default:
	{
		return "None";
		break;
	}
	}
}

inline const uint8_t ToUInt(EInternetProtocolPlags Flags)
{
	switch (Flags)
	{
		case EInternetProtocolPlags::None:
		{
			return 0;
			break;
		}

		case EInternetProtocolPlags::IPv4:
		{
			return 1;
			break;
		}

		case EInternetProtocolPlags::IPv6:
		{
			return 2;
			break;
		}

		default:
		{
			return 0;
			break;
		}
	}
}

inline uint8_t operator&(ENetworkProtocolPlags A, ENetworkProtocolPlags B)
{
	return static_cast<uint8_t>(A) & static_cast<uint8_t>(B);
}

