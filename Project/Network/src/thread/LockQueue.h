#pragma once
#include "pch.h"

#include <future>

//범용
template<typename T>
class ThreadsafeQueue
{
private:
	mutable std::mutex Mutex;
	std::queue<std::shared_ptr<T>> DataQueue;
	std::condition_variable DataCond;

public:
	ThreadsafeQueue() {}

	void push(T new_value)
	{
		std::shared_ptr<T> Data(std::make_shared<T>(std::move(new_value)));
		std::lock_guard<std::mutex> Lock(Mutex);

		DataQueue.push(Data);
		DataCond.notify_one();
	}

	void wait_and_pop(T& value)
	{
		std::unique_lock<std::mutex> Lock(Mutex);
		DataCond.wait(Lock, [this] {return !DataQueue.empty(); });
		value = std::move(*DataQueue.front());
		DataQueue.pop();
	}

	std::shared_ptr<T> wait_and_pop()
	{
		std::unique_lock<std::mutex> Lock(Mutex);
		DataCond.wait(Lock, [this] {return !DataQueue.empty(); });
		std::shared_ptr<T> Res = DataQueue.front();
		DataQueue.pop();
		return Res;
	}

	bool try_pop(T& value)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		if (DataQueue.empty())
		{
			return false;
		}

		value = std::move(*DataQueue.front());
		DataQueue.pop();
		return true;
	}

	std::shared_ptr<T> try_pop()
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		if (DataQueue.empty())
		{
			return std::shared_ptr<T>();
		}
		std::shared_ptr<T> Res = DataQueue.front();
		DataQueue.pop();
		return Res;
	}

	bool empty() const
	{
		std::lock_guard<std::mutex> lk(Mutex);
		return DataQueue.empty();
	}
};

