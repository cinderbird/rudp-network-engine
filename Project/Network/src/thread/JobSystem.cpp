#include "JobSystem.h"
#include <cstdlib> 

std::once_flag Network::Thread::Job::JobSystem::init_flag;
std::unique_ptr<Network::Thread::Job::JobSystem> Network::Thread::Job::JobSystem::instance;

Network::Thread::Job::JobSystem::JobSystem(thread_count_type thread_count)
	: system_thread_count(static_cast<uint32_t>(thread_count))
	, is_system_work(true)
	, init_barrier(static_cast<std::ptrdiff_t>(thread_count.value() + 1))
{
	for (thread_count_type thread_count_i = thread_count_type(0); thread_count_i < thread_count; ++thread_count_i)
	{
		local_func_task_queues.push_back(std::make_unique<Network::Thread::Queue::SListQueue<FuncTask*>>());
		local_coroutine_task_queues.push_back(std::make_unique<Network::Thread::Queue::SListQueue<Task*>>());

		global_func_task_queues.push_back(std::make_unique<Network::Thread::Queue::SListQueue<FuncTask*>>());
		global_coroutine_task_queues.push_back(std::make_unique<Network::Thread::Queue::SListQueue<Task*>>());

		// 워커당 신호 슬롯 하나. 아래서 워커 스레드가 시작되기 전에 전부
		// 채워져 있어야 한다 -- Internal_AddJob이 이 벡터를 인덱싱하고,
		// 워커는 시작 즉시 자식 Job을 enqueue할 수 있다.
		worker_signals.push_back(std::make_unique<WorkerSignal>());

		// 같은 순서 요구사항 -- 워커는 시작하자마자 자기 슬롯에 쓰므로,
		// 어떤 스레드가 시작하기 전에도 모든 슬롯이 존재해야 한다.
		worker_telemetry.push_back(std::make_unique<WorkerTelemetry>());
	}

	for (thread_count_type thread_count_i = thread_count_type(0); thread_count_i < thread_count; ++thread_count_i)
	{
		job_system_threads.emplace_back(std::thread(&JobSystem::RunPendingTsak, this, thread_count_i, thread_count));
	}

	init_barrier.arrive_and_wait();
}

void Network::Thread::Job::JobSystem::Init(uint32_t _thread_count)
{
	std::call_once(init_flag, [](uint32_t thread_count) {
		instance = std::make_unique<JobSystem>(thread_count_type{ thread_count });
		}, _thread_count);
}

auto Network::Thread::Job::JobSystem::GetInstance() -> JobSystem&
{
	return *instance;
}

auto Network::Thread::Job::JobSystem::GetThreadCurrentJob() -> Task*
{
	return thread_current_job;
}

Network::Thread::Job::JobSystem::~JobSystem()
{
	is_system_work = false;


	for (auto& Signal : worker_signals)
	{
		{
			std::lock_guard<std::mutex> Lock(Signal->mutex);
		}
		Signal->cv.notify_one();
	}

	for (auto& thread : job_system_threads)
	{
		if (thread.joinable())
		{
			thread.join();
		}
	}
}

void Network::Thread::Job::JobSystem::RunPendingTsak(thread_count_type thread_index, thread_count_type thread_count)
{
	init_barrier.arrive_and_wait();

	const bool bUseCVWait = Network::Thread::Job::JobSystem::ShouldUseCVWait();

	WorkerTelemetry& Telemetry = *worker_telemetry[static_cast<size_t>(thread_index.value())];
	Telemetry.os_thread_id.store(::GetCurrentThreadId(), std::memory_order_relaxed);

	while (is_system_work)
	{
		if (RunJobFromLocalQueue(thread_index) || RunCoroutineFromLocalQueue(thread_index))
		{
			Telemetry.jobs_run.fetch_add(1, std::memory_order_relaxed);
			continue;
		}
		if (RunJobFromGlobalQueue(thread_index) || RunCoroutineFromGlobalQueue(thread_index))
		{
			Telemetry.jobs_run.fetch_add(1, std::memory_order_relaxed);
			continue;
		}

		if (bUseCVWait)
		{
			WorkerSignal& Signal = *worker_signals[static_cast<size_t>(thread_index.value())];
			std::unique_lock<std::mutex> lock(Signal.mutex);
			Signal.cv.wait(lock, [this, &Signal] {
				return !is_system_work || Signal.pending_task_count.load(std::memory_order_relaxed) > 0;
				});
		}
		else
		{
			YieldProcessor();
		}
	}
}

auto Network::Thread::Job::JobSystem::SnapshotWorkerStats() -> std::vector<WorkerStat>
{
	std::vector<WorkerStat> Stats;

	if (!instance)
	{
		return Stats;
	}

	JobSystem& System = *instance;
	Stats.reserve(System.worker_telemetry.size());

	for (size_t Index = 0; Index < System.worker_telemetry.size(); ++Index)
	{
		WorkerStat Stat{};
		Stat.jobs_run = System.worker_telemetry[Index]->jobs_run.load(std::memory_order_relaxed);
		Stat.os_thread_id = System.worker_telemetry[Index]->os_thread_id.load(std::memory_order_relaxed);

		if (Index < System.job_system_threads.size() && System.job_system_threads[Index].joinable())
		{
			FILETIME Creation{}, Exit{}, Kernel{}, User{};
			HANDLE ThreadHandle = static_cast<HANDLE>(
				const_cast<std::thread&>(System.job_system_threads[Index]).native_handle());

			if (ThreadHandle && ::GetThreadTimes(ThreadHandle, &Creation, &Exit, &Kernel, &User))
			{

				const uint64_t KernelTicks = (static_cast<uint64_t>(Kernel.dwHighDateTime) << 32) | Kernel.dwLowDateTime;
				const uint64_t UserTicks = (static_cast<uint64_t>(User.dwHighDateTime) << 32) | User.dwLowDateTime;
				Stat.cpu_time_us = (KernelTicks + UserTicks) / 10ull;
			}
		}

		Stats.push_back(Stat);
	}

	return Stats;
}

bool Network::Thread::Job::JobSystem::ShouldUseCVWait()
{
#pragma warning(suppress : 4996) // 이런 좁은 범위의 테스트 전용 읽기엔 getenv로 충분
	static const bool bForceBusySpin = (std::getenv("NET_FORCE_BUSYSPIN") != nullptr);
	return !bForceBusySpin;
}

auto Network::Thread::Job::JobSystem::RunJobFromLocalQueue(thread_count_type thread_index) -> bool
{
	auto& job_queue = local_func_task_queues[static_cast<size_t>(thread_index.value())];

	if (auto opt_job = job_queue->TryPopFront())
	{
		if (auto job = opt_job.value())
		{
			// 이 워커 자신의 카운터를 감소(방금 pop한 큐가 이 워커 자신의
			// 것이므로) -- JobSystem.h의 WorkerSignal 참고.
			worker_signals[static_cast<size_t>(thread_index.value())]->pending_task_count.fetch_sub(1, std::memory_order_relaxed);
			thread_current_job = static_cast<Task*>(job);

			if (thread_current_job)
			{
				job->DoTask();
				NotifyRootTask(thread_current_job);

				return true;
			}
		}
	}

	return false;
}

auto Network::Thread::Job::JobSystem::RunCoroutineFromLocalQueue(thread_count_type thread_index) -> bool
{
	auto& job_queue = local_coroutine_task_queues[static_cast<size_t>(thread_index.value())];

	if (auto opt_job = job_queue->TryPopFront())
	{
		if (auto job = opt_job.value())
		{
			// 이 워커 자신의 카운터를 감소(방금 pop한 큐가 이 워커 자신의
			// 것이므로) -- JobSystem.h의 WorkerSignal 참고.
			worker_signals[static_cast<size_t>(thread_index.value())]->pending_task_count.fetch_sub(1, std::memory_order_relaxed);
			thread_current_job = static_cast<Task*>(job);

			if (thread_current_job)
			{
				thread_current_job->DoTask();
				return true;
			}
		}
	}

	return false;
}

auto Network::Thread::Job::JobSystem::RunJobFromGlobalQueue(thread_count_type thread_index) -> bool
{
	auto& job_queue = global_func_task_queues[static_cast<size_t>(thread_index.value())];

	if (auto opt_job = job_queue->TryPopFront())
	{
		if (auto job = opt_job.value())
		{
			// 이 워커 자신의 카운터를 감소(방금 pop한 큐가 이 워커 자신의
			// 것이므로) -- JobSystem.h의 WorkerSignal 참고.
			worker_signals[static_cast<size_t>(thread_index.value())]->pending_task_count.fetch_sub(1, std::memory_order_relaxed);
			thread_current_job = static_cast<Task*>(job);

			if (thread_current_job)
			{
				job->DoTask();
				NotifyRootTask(thread_current_job);

				return true;
			}
		}
	}

	return false;
}

auto Network::Thread::Job::JobSystem::RunCoroutineFromGlobalQueue(thread_count_type thread_index) -> bool
{
	auto& job_queue = global_coroutine_task_queues[static_cast<size_t>(thread_index.value())];

	if (auto opt_job = job_queue->TryPopFront())
	{
		if (auto job = opt_job.value())
		{
			// 이 워커 자신의 카운터를 감소(방금 pop한 큐가 이 워커 자신의
			// 것이므로) -- JobSystem.h의 WorkerSignal 참고.
			worker_signals[static_cast<size_t>(thread_index.value())]->pending_task_count.fetch_sub(1, std::memory_order_relaxed);
			thread_current_job = static_cast<Task*>(job);

			if (thread_current_job)
			{
				thread_current_job->DoTask();
				return true;
			}
		}
	}

	return false;
}

auto Network::Thread::Job::JobSystem::NextThreadIndex() -> thread_index_type
{
	uint32_t thread_count = system_thread_count.load(std::memory_order_relaxed);

	if (thread_count == 0)
	{
		return thread_index_type{ 0 };
	}

	uint64_t next_index = next_thread_index.fetch_add(1, std::memory_order_relaxed);

	return thread_index_type{ static_cast<int64_t>(next_index % thread_count) };
}

void Network::Thread::Job::JobSystem::NotifyRootTask(Task* job)
{
	Task* root_task = job->_root_task;
	delete static_cast<FuncTask*>(job);

	if (root_task)
	{
		uint32_t remaining_children = root_task->_children_task_count.fetch_sub(1);

		if (remaining_children == 1)
		{
			if (!root_task->is_function_task.load())
			{
				thread_current_job = nullptr;
				Internal_AddJob(root_task, root_task->_root_task, 0);
			}
		}
	}
}
