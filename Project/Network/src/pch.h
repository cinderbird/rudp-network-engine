#pragma once
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "CorePch.h"

#include <cstdint>
#include <memory>
#include <vector>
#include <array>
#include <queue>
#include <deque>
#include <map>
#include <unordered_map>
#include <variant>
#include <limits>
#include <type_traits>
#include <algorithm>
#include <functional>
#include <span>
#include <atomic>
#include <thread>
#include <mutex>
#include <filesystem>
#include <random>
#include <shared_mutex>

#include "NetMacro.h"
#include "AyncyLog.h"
#include "PacketEnum.h"

#include "ActorProtocol.pb.h"
#include "ControlProtocol.pb.h"
#include "Struct.pb.h"