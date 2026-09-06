#pragma once
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>


#include <filesystem>
#include <thread>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <chrono>


#define NET_IOCP_THREAD_COUNT 2
#define NET_STATUS_CHECK_TIME_SEC 1

//Lib header
#include "WindowsPlatformTime.h"
#include "export_struct.h"


#include "ActorProtocol.pb.h"
#include "ControlProtocol.pb.h"
#include "Struct.pb.h"

#include "IDAllocator.h"
#include "CoreStruct.h"
