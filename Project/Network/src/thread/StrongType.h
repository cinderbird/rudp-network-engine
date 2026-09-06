#pragma once

#include <limits>
#include <utility>

namespace Network::HAL
{
	namespace Private
	{
		template<size_t N>
		struct reader
		{
			friend auto counted_flag(reader<N>);
		};

		template<size_t N>
		struct setter
		{
			friend auto counted_flag(reader<N>) {}
			static constexpr size_t n = N;
		};

		template< auto Tag, size_t NextVal = 0 >
		[[nodiscard]] consteval auto counter_impl()
		{
			constexpr bool counted_past_m_value = requires(reader<NextVal> r) { counted_flag(r); };

			if constexpr (counted_past_m_value)
			{
				return counter_impl<Tag, NextVal + 1>();
			}
			else
			{
				setter<NextVal> s;
				return s.n;
			}
		}

		template<typename T>
		concept Hashable = requires(T a)
		{
			{ std::hash<T>{}(a) } -> std::convertible_to<std::size_t>;
		};
	}

	template<typename T, auto P, typename D = void >
	struct StrongType
	{
		StrongType() noexcept requires (std::is_same_v<D, void>) = default;
		StrongType() noexcept requires (!std::is_same_v<D, void>)
		{
			m_value = D::value;
		}
		explicit StrongType(const T& v) noexcept
		{
			m_value = v;
		}
		explicit StrongType(T&& v) noexcept
		{
			m_value = v;
		}

		StrongType(StrongType<T, P, D> const&) noexcept = default;	//copy constructible
		StrongType(StrongType<T, P, D>&&) noexcept = default;		//move constructible

		StrongType<T, P, D>& operator=(T const& v) noexcept
		{
			m_value = v; return *this;
		}

		//T 타입에서 복사 대입 가능
		StrongType<T, P, D>& operator=(T&& v) noexcept
		{
			m_value = v; return *this;
		}

		StrongType<T, P, D>& operator=(StrongType<T, P, D> const&) noexcept = default;	//move assignable
		StrongType<T, P, D>& operator=(StrongType<T, P, D>&&) noexcept = default;		//move assignable

		T& value() noexcept
		{
			return m_value;
		}

		operator const T& () const noexcept
		{
			return m_value;
		}

		operator T& () noexcept
		{
			return m_value;
		}

		auto operator<=>(const StrongType<T, P, D>& v) const = default;

		struct equal_to
		{
			constexpr bool operator()(const T& lhs, const T& rhs) const noexcept requires std::equality_comparable<std::decay_t<T>>
			{
				return lhs == rhs;
			}
		};

		struct hash
		{
			std::size_t operator()(const StrongType<T, P, D>& tag) const noexcept requires Private::Hashable<std::decay_t<T>>
			{
				return std::hash<T>()(tag.m_value);
			}
		};

		bool has_value() const noexcept requires (!std::is_same_v<D, void>)
		{
			return m_value != D::value;
		}

	protected:
		T m_value;
	};


	template< auto Tag = [] {}, auto Val = Private::counter_impl<Tag>() >
	constexpr auto counter = Val;
}

//type counter는 https://mc-deltat.github.io/articles/stateful-metaprogramming-cpp20 에서 가져옴
