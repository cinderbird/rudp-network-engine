#pragma once
#include "ActorProtocol.pb.h"
#include "ControlProtocol.pb.h"


template<typename T>
struct ProtocolTraits;

//Actor
template<>
struct ProtocolTraits<GameProtocol::S_Spawn>
{
	static constexpr uint8_t Category = 1;
	static constexpr uint16_t Id = 1;
};

template<>
struct ProtocolTraits<GameProtocol::S_Move>
{
	static constexpr uint8_t Category = 1;
	static constexpr uint16_t Id = 2;
};

template<>
struct ProtocolTraits<GameProtocol::C_Move>
{
	static constexpr uint8_t Category = 1;
	static constexpr uint16_t Id = 3;
};

template<>
struct ProtocolTraits<GameProtocol::C_Spawn>
{
	static constexpr uint8_t Category = 1;
	static constexpr uint16_t Id = 4;
};

//Control
template<>
struct ProtocolTraits<GameProtocol::NMT_Hello>
{
	static constexpr uint8_t Category = 0;
	static constexpr uint16_t Id = 1;
};

template<>
struct ProtocolTraits<GameProtocol::NMT_Challenge>
{
	static constexpr uint8_t Category = 0;
	static constexpr uint16_t Id = 2;
};

template<>
struct ProtocolTraits<GameProtocol::NMT_Login>
{
	static constexpr uint8_t Category = 0;
	static constexpr uint16_t Id = 3;
};

template<>
struct ProtocolTraits<GameProtocol::NMT_Welcome>
{
	static constexpr uint8_t Category = 0;
	static constexpr uint16_t Id = 4;
};

template<>
struct ProtocolTraits<GameProtocol::NMT_Join>
{
	static constexpr uint8_t Category = 0;
	static constexpr uint16_t Id = 5;
};

template<>
struct ProtocolTraits<GameProtocol::NMT_NetSpeed>
{
	static constexpr uint8_t Category = 0;
	static constexpr uint16_t Id = 6;
};

template<>
struct ProtocolTraits<GameProtocol::NMT_Failure>
{
	static constexpr uint8_t Category = 0;
	static constexpr uint16_t Id = 7;
};

