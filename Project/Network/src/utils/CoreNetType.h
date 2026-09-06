#pragma once
#include "pch.h"

enum class EChannelCloseReason : uint8_t
{
	Destroyed,
	Dormancy,
	LevelUnloaded,
	Relevancy,
	TearOff,
	MAX = 15	
};

inline std::string ToString(EChannelCloseReason Reason)
{
	switch (Reason)
	{
	case EChannelCloseReason::Destroyed:
	{
		return "Destroyed";
		break;
	}
	case EChannelCloseReason::Dormancy:
	{
		return "Dormancy";
		break;
	}
	case EChannelCloseReason::LevelUnloaded:
	{
		return "LevelUnloaded";
		break;
	}
	case EChannelCloseReason::Relevancy:
	{
		return "Relevancy";
		break;
	}
	case EChannelCloseReason::TearOff:
	{
		return "TearOff";
		break;
	}
	case EChannelCloseReason::MAX:
	{
		return "MAX";
		break;
	}
	default:
	{
		return "None";
		break;
	}
	}
}


