
#include <iomanip>
#include <atomic>
#include <type_traits>

namespace Network
{
	namespace Thread
	{
		template<typename ValueT, auto TypeIdV, typename DefaultT = void>
		struct StrongSafeType
		{
			static_assert(std::is_trivially_copyable_v<ValueT>,
				"StrongSafeType<ValueT, ...> requires a trivially copyable ValueT (it is stored in a std::atomic<ValueT>).");

		protected:
			std::atomic<ValueT> Value;

		public:
			using ValueType = ValueT;
			static constexpr auto StrongTypeId = TypeIdV;
			using DefaultType = DefaultT;

			//IsStrongType<int, 0, void>::value) == false
			template<typename InValueT, auto InTypeIdV, typename InDefaultT>
			struct IsStrongType : public std::false_type
			{
			};

			//using MyStrongTypeSafe = StrongSafeType<int, 123, void>;
			//is_strong_type<MyStrongTypeSafe, 0, void>::value) == true
			template<typename InValueT, auto InTypeIdV, typename InDefaultT>
			struct IsStrongType<StrongSafeType<InValueT, InTypeIdV, InDefaultT>, InTypeIdV, InDefaultT> : public std::true_type
			{
			};

			StrongSafeType() noexcept requires (std::is_same_v<DefaultT, void>) : Value(ValueT{}) {}
			StrongSafeType() noexcept requires (!std::is_same_v<DefaultT, void>) : Value(DefaultT::value) {} //기본 타입 설정

			explicit StrongSafeType(const ValueT& InValue) noexcept : Value(InValue) {} //ValueT 타입에서 explicit 생성

			// std::atomic은 복사도 이동도 불가능 -- 원본을 load하고 새로 만든
			// atomic에 store해서 복사/이동한다.
			StrongSafeType(const StrongSafeType& Other) noexcept : Value(Other.Value.load(std::memory_order_acquire)) {}
			StrongSafeType(StrongSafeType&& Other) noexcept : Value(Other.Value.load(std::memory_order_acquire)) {}

			StrongSafeType& operator=(const ValueT& InValue) noexcept //ValueT 타입에서 대입
			{
				Value.store(InValue, std::memory_order_release);
				return *this;
			}

			StrongSafeType& operator=(const StrongSafeType& Other) noexcept
			{
				Value.store(Other.Value.load(std::memory_order_acquire), std::memory_order_release);
				return *this;
			}

			StrongSafeType& operator=(StrongSafeType&& Other) noexcept
			{
				Value.store(Other.Value.load(std::memory_order_acquire), std::memory_order_release);
				return *this;
			}

			// 일부러 GetValue()/operator ValueT&()를 안 둠 -- 둘 다 내부 값의
			// 가변 참조를 내줘서 호출자가 atomic을 완전히 우회하게 만든다.
			// load()/operator ValueT()(둘 다 값으로)로 읽을 것.

			operator ValueT() const noexcept
			{
				return Value.load(std::memory_order_acquire);
			}

			bool operator==(const StrongSafeType& Other) const noexcept
			{
				return Value.load(std::memory_order_acquire) == Other.Value.load(std::memory_order_acquire);
			}

			bool operator==(const ValueT& Other) const noexcept
			{
				return Value.load(std::memory_order_acquire) == Other;
			}

			ValueT load(std::memory_order Order = std::memory_order_acquire) const noexcept
			{
				return Value.load(Order);
			}

			void store(const ValueT& NewValue, std::memory_order Order = std::memory_order_release) noexcept
			{
				Value.store(NewValue, Order);
			}

			bool compare_exchange_weak(ValueT& Expected, ValueT Desired,
				std::memory_order Order = std::memory_order_seq_cst) noexcept
			{
				return Value.compare_exchange_weak(Expected, Desired, Order);
			}

			bool compare_exchange_strong(ValueT& Expected, ValueT Desired,
				std::memory_order Order = std::memory_order_seq_cst) noexcept
			{
				return Value.compare_exchange_strong(Expected, Desired, Order);
			}
		};
	}
}
