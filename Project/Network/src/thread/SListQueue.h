#pragma once

#include <Windows.h>
#include <stdexcept>
#include <memory>
#include <optional>

namespace Network::Thread::Queue
{
	namespace Private
	{
		template <typename T>
		struct Item
		{
			SLIST_ENTRY _entry;
			T _data;

			Item(const T& data)
				: _data(data)
			{
			}

			Item(T&& data)
				: _data(std::move(data))
			{
			}
		};
	}

	template<typename T>
	class SListQueue
	{
	public:
		SListQueue();
		~SListQueue();

		//복사, 이동 금지
		SListQueue(const SListQueue&) = delete;
		auto operator=(const SListQueue&) -> SListQueue & = delete;

		SListQueue(SListQueue&&) = delete;
		auto operator=(SListQueue&&) -> SListQueue & = delete;

		auto PushBack(const T& data) -> void;
		auto PushBack(T&& data) -> void;

		auto TryPopFront() -> std::optional<T>;

	private:
		auto Transition() -> void;

	private:
		DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) SLIST_HEADER push_stack;
		DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) SLIST_HEADER pop_stack;

		static constexpr auto critical_section_spin_count = 4000;
		CRITICAL_SECTION critical_section;
	};

	template<typename T>
	inline SListQueue<T>::SListQueue()
	{
		if (InitializeCriticalSectionAndSpinCount(&critical_section, critical_section_spin_count) == false)
		{
			throw STATUS_NO_MEMORY;
		}

		InitializeSListHead(&push_stack);
		InitializeSListHead(&pop_stack);

	}

	template<typename T>
	inline SListQueue<T>::~SListQueue()
	{

		PSLIST_ENTRY entry = nullptr;
		while ((entry = InterlockedPopEntrySList(&push_stack)) != nullptr)
		{
			Private::Item<T>* item = CONTAINING_RECORD(entry, Private::Item<T>, _entry);
			delete item;
		}

		while ((entry = InterlockedPopEntrySList(&pop_stack)) != nullptr)
		{
			Private::Item<T>* item = CONTAINING_RECORD(entry, Private::Item<T>, _entry);
			delete item;
		}

		DeleteCriticalSection(&critical_section);
	}

	template<typename T>
	inline auto SListQueue<T>::PushBack(const T& data) -> void
	{
		Private::Item<T>* new_item = new Private::Item<T>(data);
		InterlockedPushEntrySList(&push_stack, &(new_item->_entry));
	}

	template<typename T>
	inline auto SListQueue<T>::PushBack(T&& data) -> void
	{
		Private::Item<T>* new_item = new Private::Item<T>(std::move(data));
		InterlockedPushEntrySList(&push_stack, &(new_item->_entry));
	}


	template<typename T>
	inline auto SListQueue<T>::TryPopFront() -> std::optional<T>
	{
		PSLIST_ENTRY entry = InterlockedPopEntrySList(&pop_stack);

		if (entry == nullptr)
		{
			EnterCriticalSection(&critical_section);

			//다시 한번 확인
			entry = InterlockedPopEntrySList(&pop_stack);
			if (entry == nullptr)
			{
				Transition();
				entry = InterlockedPopEntrySList(&pop_stack);
			}

			LeaveCriticalSection(&critical_section);
		}

		if (entry == nullptr)
		{
			return std::nullopt;
		}

		Private::Item<T>* item = CONTAINING_RECORD(entry, Private::Item<T>, _entry);
		std::optional<T> data = std::move(item->_data);

		delete item;

		return data;
	}

	template<typename T>
	inline auto SListQueue<T>::Transition() -> void
	{
		PSLIST_ENTRY list_head = InterlockedFlushSList(&push_stack);

		while (list_head != nullptr)
		{
			PSLIST_ENTRY next = list_head->Next;
			InterlockedPushEntrySList(&pop_stack, list_head);
			list_head = next;
		}
	}
}
