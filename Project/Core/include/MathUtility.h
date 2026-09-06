#pragma once
#include <cstdint>

template <class T>
static constexpr T DivideAndRoundUp(T Dividend, T Divisor)
{
	return (Dividend + Divisor - 1) / Divisor;
}

static  uint32_t CountLeadingZeros(uint32_t Value)
{
	unsigned long BitIndex;
	_BitScanReverse64(&BitIndex, uint64_t(Value) * 2 + 1);
	return 32 - BitIndex;
}

static uint32_t CeilLogTwo(uint32_t Arg)
{
	Arg = Arg ? Arg : 1;
	return 32 - CountLeadingZeros(Arg - 1);
}

namespace Math
{
	struct Vector3D;

	struct Vector3D
	{
		int16_t x = 0;
		int16_t y = 0;
		int16_t z = 0;

		Vector3D() : x(0), y(0), z(0) {}
		Vector3D(int16_t x, int16_t y, int16_t z) : x(x), y(y), z(z) {}
	};
}


