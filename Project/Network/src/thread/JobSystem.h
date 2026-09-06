#pragma once

#include <vector>
#include <barrier>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <coroutine>
#include <atomic>
#include "StrongType.h"
#include "SListQueue.h"

namespace Network::Thread::Job
{
	using thread_count_type = Network::HAL::StrongType<int64_t, Network::HAL::counter<>, std::integral_constant<int64_t, -1>>;
	using thread_index_type = Network::HAL::StrongType<int64_t, Network::HAL::counter<>, std::integral_constant<int64_t, -1>>;
	using thread_id_type = Network::HAL::StrongType<int64_t, Network::HAL::counter<>, std::integral_constant<int64_t, -1>>;
	using thread_type_type = Network::HAL::StrongType<int64_t, Network::HAL::counter<>, std::integral_constant<int64_t, -1>>;

	class Task;
	class FuncTask;

	template<typename T>
	class CoroutineTaskBase;

	template<typename T>
	class CoroutineTask;

	template<typename T>
	class CoroutineObject;

	template<typename T, typename... Args>
	struct AwaitableTuple;

	template<typename T>
	struct AwaitableResumeOn;

	template<typename T>
	struct AwaitableTag;

	template<typename T>
	struct FinalAwaiter;

	template<typename>
	struct is_vector_s : std::false_type {};

	template<typename T>
	struct is_vector_s<std::vector<T>> : std::true_type {};

	template<typename T>
	concept is_vector = is_vector_s<std::decay_t<T>>::value;

	template<typename T>
	concept is_parent = std::is_same_v< std::decay_t<T>, Task>;

	template<typename T>
	concept is_function_job = std::is_same_v< std::decay_t<T>, FuncTask>;

	template<typename T>
	concept is_coroutine_job = std::is_base_of_v<Task, std::decay_t<T>> && !is_function_job<T>;

	template<typename T>
	concept is_any_job = is_parent<T> || is_function_job<T> || is_coroutine_job<T>;

	template<typename >
	struct is_coro_return_s : std::false_type {};

	template<typename T>
	struct is_coro_return_s<CoroutineObject<T>> : std::true_type {};

	template<typename T>
	concept is_coro_return = is_coro_return_s<std::decay_t<T>>::value;

	template<typename T>
	concept is_function = std::is_convertible_v< std::decay_t<T>, std::function<void()> > && (!is_coro_return<T>);

	template<typename T>
	concept is_func_base_job = is_function<T> || is_function_job<T>;


	template<typename... Arg>
	inline decltype(auto) ParallelTask(Arg&&... args)
	{
		return std::tuple<Arg&&...>(std::forward<Arg>(args)...);
	}

	class JobSystem
	{
	public:
		static void Init(uint32_t thread_count = std::thread::hardware_concurrency());
		static auto GetInstance() -> JobSystem&;

		static auto GetThreadCount() -> uint32_t
		{
			return GetInstance().system_thread_count.load(std::memory_order_relaxed);
		}

		struct WorkerStat
		{
			uint64_t jobs_run = 0;
			uint32_t os_thread_id = 0;
			uint64_t cpu_time_us = 0;
		};
		static auto SnapshotWorkerStats() -> std::vector<WorkerStat>;

		JobSystem(thread_count_type thread_count = thread_count_type(std::thread::hardware_concurrency()));
		~JobSystem();

		JobSystem(const JobSystem&) = delete;
		auto operator=(const JobSystem&) -> JobSystem & = delete;

		JobSystem(JobSystem&&) = delete;
		auto operator=(JobSystem&&) -> JobSystem & = delete;

		template<typename T> requires std::is_base_of_v<Thread::Job::Task, T> && (!is_coro_return<T>)
		static auto AddAsyncJob(T* task, Thread::Job::Task* current_task = nullptr, int32_t task_count = -1) -> uint32_t
		{
			auto& job_system = Thread::Job::JobSystem::GetInstance();
			Thread::Job::Task* parent_task = nullptr;

			if (current_task)
			{
				parent_task = current_task;
			}
			else
			{
				parent_task = job_system.GetThreadCurrentJob();
			}

			auto ScheduledCount = job_system.Internal_AddJob(task, parent_task, task_count);
			return ScheduledCount;
		}
		static auto AddAsyncJob(Thread::Job::is_coro_return auto&& coroutine_job, Thread::Job::Task* current_task = nullptr, int32_t task_count = -1) -> uint32_t
		{
			if (!coroutine_job.GetHandle() || coroutine_job.GetHandle().done())
			{
				return 0;
			}

			const auto& job_system = Thread::Job::JobSystem::GetInstance();
			Thread::Job::Task* thread_current_job = nullptr;

			if (current_task)
			{
				thread_current_job = current_task;
			}
			else
			{
				thread_current_job = job_system.GetThreadCurrentJob();
			}

			auto ScheduledCount = Network::Thread::Job::JobSystem::GetInstance().Internal_AddJob(&coroutine_job.GetPromise(), thread_current_job, task_count);
			return ScheduledCount;
		}
		static auto AddAsyncJob(Thread::Job::is_function auto&& _function, Thread::Job::Task* current_task = nullptr, int32_t task_count = -1) -> uint32_t
		{
			const auto& job_system = Thread::Job::JobSystem::GetInstance();
			Thread::Job::Task* thread_current_job = nullptr;
			if (current_task)
			{
				thread_current_job = current_task;
			}
			else
			{
				thread_current_job = job_system.GetThreadCurrentJob();
			}

			auto job = new Thread::Job::FuncTask(std::forward<decltype(_function)>(_function));

			auto ScheduledCount = Network::Thread::Job::JobSystem::GetInstance().Internal_AddJob(job, thread_current_job, task_count);
			return ScheduledCount;
		}

		static auto AddAsyncJob(Thread::Job::is_function auto&& _function, thread_index_type pinned_thread_index, Thread::Job::Task* current_task = nullptr, int32_t task_count = -1) -> uint32_t
		{
			const auto& job_system = Thread::Job::JobSystem::GetInstance();
			Thread::Job::Task* thread_current_job = nullptr;
			if (current_task)
			{
				thread_current_job = current_task;
			}
			else
			{
				thread_current_job = job_system.GetThreadCurrentJob();
			}

			auto job = new Thread::Job::FuncTask(std::forward<decltype(_function)>(_function), pinned_thread_index);

			auto ScheduledCount = Network::Thread::Job::JobSystem::GetInstance().Internal_AddJob(job, thread_current_job, task_count);
			return ScheduledCount;
		}
		static auto AddAsyncJob(Thread::Job::is_vector auto&& vector_contained_job, Thread::Job::Task* current_task = nullptr, int32_t job_count = -1) -> uint32_t
		{
			const auto& job_system = Thread::Job::JobSystem::GetInstance();

			Thread::Job::Task* thread_current_job = nullptr;
			if (current_task)
			{
				thread_current_job = current_task;
			}
			else
			{
				thread_current_job = job_system.GetThreadCurrentJob();
			}

			uint32_t sum = 0;
			std::ranges::for_each(vector_contained_job, [&](auto&& contained_job) {
				sum += AddAsyncJob(std::forward<decltype(contained_job)>(contained_job), thread_current_job, job_count);
				job_count = 0; //for remain root_task counter(prevent inf loop)
				});

			return sum;
		}

		static auto GetThreadCurrentJob() -> Task*;
	private:
		template<typename T> requires is_any_job<T>
		auto Internal_AddJob(T* job, Task* root_task, int32_t children_count) -> uint32_t;

		void RunPendingTsak(thread_count_type thread_index, thread_count_type thread_count);

		static bool ShouldUseCVWait();

		//소비자
		auto RunJobFromLocalQueue(thread_count_type thread_index) -> bool;
		auto RunCoroutineFromLocalQueue(thread_count_type thread_index) -> bool;
		auto RunJobFromGlobalQueue(thread_count_type thread_index) -> bool;
		auto RunCoroutineFromGlobalQueue(thread_count_type thread_index) -> bool;

		auto NextThreadIndex() -> thread_index_type;

		void NotifyRootTask(Task* job);

	private:
		static std::once_flag init_flag;
		static std::unique_ptr<JobSystem> instance;

	private:
		std::atomic<uint32_t> system_thread_count;

		std::atomic_bool is_system_work;
		std::barrier<> init_barrier;


		struct WorkerSignal
		{
			std::mutex mutex;
			std::condition_variable cv;
			std::atomic<uint64_t> pending_task_count{ 0 };
		};
		std::vector<std::unique_ptr<WorkerSignal>> worker_signals;

		struct WorkerTelemetry
		{
			std::atomic<uint64_t> jobs_run{ 0 };
			std::atomic<uint32_t> os_thread_id{ 0 };
		};
		std::vector<std::unique_ptr<WorkerTelemetry>> worker_telemetry;

		std::vector<std::thread> job_system_threads;

		std::vector<std::unique_ptr<Network::Thread::Queue::SListQueue<FuncTask*>>> global_func_task_queues;
		std::vector<std::unique_ptr<Network::Thread::Queue::SListQueue<Task*>>> global_coroutine_task_queues;

		std::vector<std::unique_ptr<Network::Thread::Queue::SListQueue<FuncTask*>>> local_func_task_queues;
		std::vector<std::unique_ptr<Network::Thread::Queue::SListQueue<Task*>>> local_coroutine_task_queues;

		static inline thread_local Task* thread_current_job{};
		std::atomic<uint64_t> next_thread_index;
	};

	template<typename T>
	class CoroutineObject
	{
	public:
		using promise_type = CoroutineTask<T>;

	public:
		CoroutineObject() noexcept {}
		CoroutineObject(std::coroutine_handle<promise_type> handle) noexcept : _handle(handle) {}

		CoroutineObject(CoroutineObject&& coroutine_object) noexcept : _handle(std::exchange(coroutine_object._handle, {})) {}
		void operator=(CoroutineObject<T>&& coroutine_object) noexcept { _handle = std::exchange(coroutine_object._handle, {}); }

		~CoroutineObject() noexcept
		{
			if (!_handle || !_handle.done() || !_handle.promise()._root_task)
			{
				return;
			}

			_handle.destroy();
		}

		CoroutineObject(const CoroutineObject& coroutine_object) = delete;
		auto operator=(const CoroutineObject& coroutine_object) -> CoroutineObject & = delete;

		void DoTask()
		{
			if (_handle && _handle.done() == false)
			{
				_handle.promise().resume();
			}
		}

		auto GetValue() noexcept -> T
		{
			if constexpr (std::is_void_v<std::decay<T>> == false)
			{
				if (_handle)
				{
					return _handle.promise().Get();
				}
			}
			else
			{
				return;
			}
		}

		auto GetPromise() -> CoroutineTask<T>&
		{
			return _handle.promise();
		}

		auto GetHandle() -> std::coroutine_handle<promise_type>
		{
			return _handle;
		}

	private:
		std::coroutine_handle<promise_type> _handle;
	};

	/*
	job base class
	*/
	class Task
	{
	public:
		Task() = default;

		Task(thread_index_type thread_index, thread_type_type thread_type, thread_id_type thread_id, Task* root_task) noexcept
			: _root_task(root_task)
			, _thread_index(thread_index)
			, _thread_id(thread_id)
			, _thread_type(thread_type)
		{
		}

		virtual ~Task() {}

		Task(const Task&) = delete;
		auto operator=(const Task&) -> Task & = delete;
		Task(Task&&) noexcept = delete;
		auto operator=(Task&&) noexcept -> Task & = delete;

		virtual void DoTask() {}
		virtual auto Destroy() -> bool { return false; }

	public:
		std::atomic<uint32_t>	_children_task_count;
		std::atomic_bool		is_function_task;

		Task* _root_task;
		thread_index_type		_thread_index;
		thread_id_type			_thread_id;
		thread_type_type		_thread_type;
	};

	/*
	std::function base job
	*/
	class FuncTask : public Task
	{
		std::function<void()> task{ []() {} };

	public:
		FuncTask() : Task() {}

		FuncTask(is_function auto&& _function, thread_index_type thread_index = thread_index_type{}, thread_type_type thread_type = thread_type_type{}, thread_id_type thread_id = thread_id_type{}, Task* root_task = nullptr)
			: Task(thread_index, thread_type, thread_id, root_task)
			, task(std::forward<decltype(_function)>(_function))
		{
			is_function_task.store(true);
		}

		virtual ~FuncTask() {}

		FuncTask(const FuncTask& func_task) noexcept = default;
		auto operator=(const FuncTask& func_task) noexcept -> FuncTask & = default;
		FuncTask(FuncTask&& func_task) noexcept = default;
		auto operator=(FuncTask&& func_task) noexcept -> FuncTask & = default;

		virtual void DoTask() override
		{
			task();
		}

		virtual auto Destroy() noexcept -> bool override
		{
			return true;
		}
	};

	/*
	coroutine base job
	*/
	template<typename T>
	class CoroutineTaskBase : public Task
	{
	public:
		CoroutineTaskBase(std::coroutine_handle<> handle) noexcept
			: _handle(handle)
		{
			is_function_task.store(false);
		}

		auto unhandled_exception() noexcept -> void
		{
			std::terminate();
		}

		auto initial_suspend() noexcept -> std::suspend_always
		{
			return std::suspend_always{};
		}

		auto final_suspend() noexcept -> FinalAwaiter<T>
		{
			return FinalAwaiter<T>{};
		}

		virtual void DoTask() override
		{
			if (_handle && !_handle.done())
			{
				_handle.resume();
			}
		}

		virtual auto Destroy() noexcept -> bool override
		{
			_handle.destroy();
			return false;
		}

		template<typename Arg>
		auto await_transform(Arg&& func) noexcept -> AwaitableTuple<T, Arg>
		{
			return AwaitableTuple<T, Arg>{ std::tuple<Arg&&>(std::forward<Arg>(func)) };
		}

		template<typename... Args>
		auto await_transform(std::tuple<Args...>&& job_tuple) noexcept -> AwaitableTuple<T, Args...>
		{
			return AwaitableTuple<T, Args...>(std::forward<std::tuple<Args...>>(std::move(job_tuple)));
		}

		template<typename... Args>
		auto await_transform(std::tuple<Args...>& job_tuple) noexcept -> AwaitableTuple<T, Args...>
		{
			return AwaitableTuple<T, Args...>(job_tuple);
		}

		auto await_transform(thread_index_type thread_index) noexcept -> AwaitableResumeOn<T>
		{
			return AwaitableResumeOn<T>(thread_index);
		}

	private:
		std::coroutine_handle<> _handle{};
	};

	template<typename T>
	class CoroutineTask : public CoroutineTaskBase<T>
	{
	public:
		CoroutineTask() noexcept
			: CoroutineTaskBase<T>(std::coroutine_handle<CoroutineTask<T>>::from_promise(*this))
		{
		}

		//코루틴 인터페이스
		//---------------------------------------------------------------//
		void return_value(T value)
		{
			this->task_value = value;
		}

		auto get_return_object() noexcept -> CoroutineObject<T>
		{
			return std::coroutine_handle<CoroutineTask<T>>::from_promise(*this);
		}
		//---------------------------------------------------------------//

		auto Get() -> T
		{
			return task_value;
		}

	private:
		T task_value{};
	};

	template<>
	class CoroutineTask<void> : public CoroutineTaskBase<void>
	{
	public:
		CoroutineTask() noexcept
			: CoroutineTaskBase<void>(std::coroutine_handle<CoroutineTask<void>>::from_promise(*this))
		{
		}

		//코루틴 인터페이스
		//---------------------------------------------------------------//
		void return_void() noexcept
		{
		}

		auto get_return_object() noexcept -> CoroutineObject<void>
		{
			return std::coroutine_handle<CoroutineTask<void>>::from_promise(*this);
		}
		//---------------------------------------------------------------//
	};

	template<typename T, typename... Args>
	struct AwaitableTuple : std::suspend_always
	{
		std::tuple<Args&&...>	_job_tuple;
		std::size_t				_job_count;

		AwaitableTuple(std::tuple<Args&&...> job_tuple) noexcept
			: _job_tuple(std::forward<std::tuple<Args&&...>>(job_tuple))
			, _job_count(0)
		{

		}

		auto await_ready() noexcept -> bool
		{
			auto sum_of_job_count = [&]<std::size_t... Idx>(std::index_sequence<Idx...>) {
				_job_count = (GetArgSize(std::get<Idx>(_job_tuple)) + ... + 0);
			};

			sum_of_job_count(std::make_index_sequence<sizeof...(Args)>{});

			return _job_count == 0;
		}

		auto await_suspend(std::coroutine_handle<CoroutineTask<T>> handle) noexcept -> bool
		{
			auto elem_funnel = [&]<std::size_t Idx>()
			{

				using TupleType = decltype(_job_tuple);
				using TupleElemType = decltype(std::get<Idx>(std::forward<TupleType>(_job_tuple)));
				decltype(auto) Elem = std::forward<TupleElemType>(std::get<Idx>(std::forward<TupleType>(_job_tuple)));

				//각각의 Task를 AddAsyncJob를 통해 스케쥴링.
				Thread::Job::JobSystem::AddAsyncJob(std::forward<TupleElemType>(Elem),
					static_cast<Task*>(&handle.promise()), static_cast<int32_t>(_job_count));
				//첫번째 Task(부모)는 총 자식 Task를 _job_count로 설정

				//이후 자식들은 중복되지 않도록 _jon_count를 0으로 설정
				_job_count = 0;
			};

			//tuple에 들어있는 Task를 하나씩 꺼내면서  elem_funnel 람다 함수에 전달.
			auto polled_elem_funnel = [&]<std::size_t... Idx>(std::index_sequence<Idx...>) {
				(elem_funnel.template operator() < Idx > (), ...);
			};


			polled_elem_funnel(std::make_index_sequence<sizeof...(Args)>{});

			return true;
		}

		auto await_resume() -> decltype(auto)
		{
			auto vec_to_tuple_cat = [&]<typename... Elems>(Elems&&... args) {
				return std::tuple_cat(GetResultTuple(std::forward<Elems>(args))...);
			};

			using concat_tuple_type = decltype(std::apply(vec_to_tuple_cat, _job_tuple));

			if constexpr (std::tuple_size_v < concat_tuple_type > == 0)
			{
				return;
			}
			else if constexpr (std::tuple_size_v < concat_tuple_type > == 1)
			{
				auto ret = std::get<0>(std::apply(vec_to_tuple_cat, std::move(_job_tuple)));
				return ret;
			}
			else
			{
				return std::apply(vec_to_tuple_cat, _job_tuple);
			}
		}

		template<typename Arg>
		auto GetArgSize(Arg& arg) -> size_t
		{
			if constexpr (is_vector<Arg>)
			{
				return arg.size();
			}


			return 1;
		}

		auto GetResultTuple(auto&& job) -> decltype(auto)
		{
			return std::make_tuple();
		}

		template<typename T> requires (!std::is_void_v<T>)
		auto GetResultTuple(CoroutineObject<T>& coroutine_object) -> decltype(auto)
		{
			return std::make_tuple(coroutine_object.GetValue());
		}

		template<typename T> requires (!std::is_void_v<T>)
		auto GetResultTuple(CoroutineObject<T>&& coroutine_object) -> decltype(auto)
		{
			return std::make_tuple(coroutine_object.GetValue());
		}

		template<typename Elem> requires (!std::is_void_v<Elem>)
		auto GetResultTuple(std::vector<CoroutineObject<Elem>>& coroutine_objects) -> decltype(auto)
		{
			std::vector<Elem> values;
			values.reserve(coroutine_objects.size());

			for (auto&& coroutine_job : coroutine_objects)
			{
				values.push_back(std::move(coroutine_job.GetValue()));
			}

			return std::make_tuple(std::move(values));
		}

		template<typename Elem> requires (!std::is_void_v<Elem>)
		auto GetResultTuple(std::vector<CoroutineObject<Elem>>&& coroutine_objects) -> decltype(auto)
		{
			std::vector<Elem> values;
			values.reserve(coroutine_objects.size());

			for (auto&& coroutine_job : std::move(coroutine_objects))
			{
				values.push_back(coroutine_job.GetValue());
			}

			return std::make_tuple(std::move(values));
		}

	};

	template<typename T>
	struct AwaitableResumeOn : std::suspend_always
	{
		thread_index_type _thread_index;

		AwaitableResumeOn(thread_index_type thread_index) noexcept
			: _thread_index(thread_index)
		{
		}

		auto await_ready() noexcept -> bool
		{
			auto current_task = JobSystem::GetThreadCurrentJob();
			if (current_task == nullptr)
			{
				return false;
			}
			else
			{
				return _thread_index == current_task->_thread_index;
			}
		}

		void await_suspend(std::coroutine_handle<CoroutineTask<T>> handle) noexcept
		{
			auto& promise = handle.promise();
			promise._thread_index = _thread_index;
			JobSystem::AddAsyncJob(&promise, promise._root_task, -1);
		}
	};

	template<typename T>
	struct FinalAwaiter : public std::suspend_always
	{
		auto await_suspend(std::coroutine_handle<CoroutineTask<T>> handle) noexcept -> bool
		{
			Task* root_task = handle.promise()._root_task;

			if (root_task)
			{
				uint32_t children_task_count = 0;
				children_task_count = root_task->_children_task_count.fetch_sub(1);

				//자신이 부모가 가진 마지막 Task였다면
				if (children_task_count == 1)
				{
					//부모를 Task 스케줄에 추가!
					JobSystem::AddAsyncJob(root_task, root_task->_root_task, 0);
				}
			}

			return true;
		}
	};

	template<typename T> requires is_any_job<T>
	inline auto JobSystem::Internal_AddJob(T* job, Task* root_task, int32_t children_count) -> uint32_t
	{
		job->_root_task = root_task;
		if (root_task != nullptr && children_count != 0)
		{
			if (children_count < 0)
			{
				children_count = 1;
			}

			root_task->_children_task_count.fetch_add(static_cast<unsigned int>(children_count));
		}

		auto job_thread_index = job->_thread_index;
		auto next_thread = job_thread_index < 0 ? NextThreadIndex() : job_thread_index;
		size_t next_thread_index = static_cast<size_t>(next_thread.value());

		//Concept을 사용하여 안전한 Task 구분
		if constexpr (is_function_job<std::remove_pointer_t<decltype(job)>>)
		{
			job->_children_task_count.store(1);
			job_thread_index < 0 ? global_func_task_queues[next_thread_index]->PushBack(static_cast<FuncTask*>(job)) : local_func_task_queues[next_thread_index]->PushBack(static_cast<FuncTask*>(job));
		}
		else
		{
			job->_children_task_count.store(0);
			job_thread_index < 0 ? global_coroutine_task_queues[next_thread_index]->PushBack(job) : local_coroutine_task_queues[next_thread_index]->PushBack(job);
		}

		{
			WorkerSignal& Signal = *worker_signals[next_thread_index];
			{
				std::lock_guard<std::mutex> NotifyLock(Signal.mutex);
				Signal.pending_task_count.fetch_add(1, std::memory_order_relaxed);
			}
			Signal.cv.notify_one();
		}
		return 1;
	}
}
