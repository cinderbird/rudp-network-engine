#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <mutex> // Init()을 지키는 std::call_once
#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "DriverSetting.h"


namespace ServerLog
{
	inline spdlog::level::level_enum ParseLevel(const std::string& Name)
	{
		if (Name == "trace")    return spdlog::level::trace;
		if (Name == "debug")    return spdlog::level::debug;
		if (Name == "info")     return spdlog::level::info;
		if (Name == "warn")     return spdlog::level::warn;
		if (Name == "err")      return spdlog::level::err;
		if (Name == "critical") return spdlog::level::critical;
		if (Name == "off")      return spdlog::level::off;
		return spdlog::level::trace;
	}

	inline std::once_flag InitOnceFlag;

	inline void Init(std::shared_ptr<DriverSetting> Setting)
	{
		std::call_once(InitOnceFlag, [&Setting]()
		{
		try
		{
			spdlog::init_thread_pool(10000, 1);

			std::vector<spdlog::sink_ptr> sinks;
			std::filesystem::path exePath = std::filesystem::current_path();
			std::filesystem::path logDirPath = exePath.parent_path().parent_path() / "Logs";

			if (!std::filesystem::exists(logDirPath))
			{
				std::filesystem::create_directories(logDirPath);
			}

			std::filesystem::path logFilePath = logDirPath / ((Setting->IsServer()) ? "Server" : "Remote");
			logFilePath /= ("network_log_pid" + std::to_string(::GetCurrentProcessId()) + ".txt");

			sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>(logFilePath.string(), 23, 59));

			auto async_logger = std::make_shared<spdlog::async_logger>(
				"network_trace_log",
				sinks.begin(),
				sinks.end(),
				spdlog::thread_pool(),
				spdlog::async_overflow_policy::overrun_oldest);

			spdlog::set_default_logger(async_logger);
			spdlog::set_level(ParseLevel(Setting->GetLogLevel()));
		}
		catch (const spdlog::spdlog_ex& ex)
		{
			std::cout << "Log initialization failed: " << ex.what() << std::endl;
		}
		});
	}
}

#define NETWORK_LOG_TRACE(Arg, ...) do {spdlog::trace(Arg, __VA_ARGS__);} while(0)
#define NETWORK_LOG_DEBUG(Arg, ...) do {spdlog::debug(Arg, __VA_ARGS__);} while(0)
#define NETWORK_LOG_INFO(Arg, ...) do {spdlog::info(Arg, __VA_ARGS__);} while(0)
#define NETWORK_LOG_WARN(Arg, ...) do {spdlog::warn(Arg, __VA_ARGS__);} while(0)
#define NETWORK_LOG_ERROR(Arg, ...) do {spdlog::error(Arg, __VA_ARGS__);} while(0)
#define NETWORK_LOG_CRITICAL(Arg, ...) do {spdlog::critical(Arg, __VA_ARGS__);} while(0)
