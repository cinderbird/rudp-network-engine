#pragma once
#include <iostream>
#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/daily_file_sink.h"


namespace ServerLog
{
	void Init()
	{
        try
        {
            auto daily_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>("logfile", 23, 59);
            spdlog::init_thread_pool(10000, 1); // queue with 10K items and 1 backing thread.
            auto async_file = spdlog::basic_logger_mt<spdlog::async_factory>("async_file_logger", "logs/async_log.txt");
            spdlog::drop_all();
        }
        catch (const spdlog::spdlog_ex& ex)
        {
            std::cout << "Log initialization failed: " << ex.what() << std::endl;
        }
	}
}