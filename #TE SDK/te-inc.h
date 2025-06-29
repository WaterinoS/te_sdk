#ifndef __MODMAIN_H
#define __MODMAIN_H

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#pragma warning( disable : 4409 )
#pragma warning( disable : 4250 )
#pragma warning( disable : 4100 )
#pragma warning( disable : 4733 )
#pragma warning( disable : 4244 )
#pragma warning( disable : 4828 )

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>

#include <memory>
#include <random>
#include <map>
#include <list>
#include <queue>
#include <set>
#include <array>
#include <future>
#include <chrono>

#include <mutex>
#include <shared_mutex>

#include <filesystem>
namespace fs = std::filesystem;

#include <iphlpapi.h>
#include <iomanip>
#include <Lmcons.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <shlobj.h>
#include <UserEnv.h>
#include <string>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <sys/stat.h>
#include <time.h>
#include <wininet.h>
#include <assert.h>

#pragma comment(lib, "Ws2_32.lib")

#endif