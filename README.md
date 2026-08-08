# TE SDK

A lightweight C++ SDK library for hooking RakNet networking in SA-MP (San Andreas Multiplayer). Build your own mods, tools, and network analyzers with ease.

**Version:** 1.1.0

---

## Features

- **RakNet Hooking** - Intercept and modify network traffic
  - Incoming RPC callbacks
  - Outgoing RPC callbacks
  - Incoming Packet callbacks
  - Outgoing Packet callbacks
  - Connect / Disconnect notifications

- **Multi-Version Support** - Works with multiple SA-MP versions:

  | Version | Outgoing hooks | Incoming RPC hook |
  |---------|:--------------:|:-----------------:|
  | 0.3.7-R1   | yes | yes |
  | 0.3.7-R2   | yes | yes |
  | 0.3.7-R3   | yes | yes |
  | 0.3.7-R4   | yes | yes |
  | 0.3.7-R4-2 | yes | no *(no verified `handle_rpc_packet` offset)* |
  | 0.3.7-R5   | yes | yes |
  | 0.3.DL-R1  | yes | yes |

  A build that is not in this table can still be supported at runtime - see
  [Unknown SA-MP builds](#unknown-sa-mp-builds).

- **Send Custom Network Data** - Use `TERakClient` to send your own RPCs and packets
- **Session Information** - Access connection details (server IP, port, connection status)
- **Clean shutdown** - remove every hook and callback before unloading your module

---

## Requirements

- Windows, **32-bit** (x86) - SA-MP is 32-bit, there is no x64 build
- Visual Studio 2022 or newer, toolset **v143 or later** (the project targets `v145`)
- C++20
- **[MinHook](https://github.com/TsudaKageyu/minhook)** - consumed via vcpkg

The repository ships a `vcpkg.json` manifest next to the project file, so a
vcpkg-enabled Visual Studio restores MinHook automatically. From the command
line:

```
cd "#TE SDK"
vcpkg install --triplet x86-windows-static-md
msbuild "#TE SDK.sln" -p:Configuration=Release -p:Platform=x86
```

The resulting libraries land in `lib/`:

| Configuration | File | CRT |
|---|---|---|
| Release | `lib/te_sdk_rel.lib` | `/MD` (MultiThreadedDLL) |
| Debug   | `lib/te_sdk_dbg.lib` | `/MDd` (MultiThreadedDebugDLL) |

### ABI compatibility

TE SDK is a **static library** that exposes `std::function`, `std::string` and
`std::vector` across its interface. Your project must therefore match:

- the same **CRT** (`/MD` for the release lib, `/MDd` for the debug lib), and
- a compatible **MSVC toolset** (v143+).

Mismatches surface as unresolved externals or as crashes inside STL types.
Whole-program optimisation (`/GL`) is deliberately **off** so the shipped `.lib`
is not tied to one exact compiler build.

---

## Quick Start

### 1. Project Setup

1. Link `te_sdk_rel.lib` (or `te_sdk_dbg.lib`) into your project
2. Include the main header:
```cpp
#include "te-sdk.h"
```

### 2. Initialize the SDK

**Important:** `InitRakNetHooks()` cannot be called directly in DllMain. You must create a separate thread and wait until SA-MP is fully loaded:

```cpp
#include "te-sdk.h"
#include <thread>
#include <chrono>

void InitThread()
{
    // Wait until SDK initializes successfully
    // (SA-MP and RakNet must be fully loaded)
    while (!te::sdk::InitRakNetHooks())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // SDK initialized successfully - register your callbacks here
    // ...
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        std::thread(InitThread).detach();
    }
    return TRUE;
}
```

`InitRakNetHooks()` returns `true` once the SDK is ready - including on a
repeated call - so the loop above always terminates. Use `InitRakNetHooksEx()`
when you want to know *why* an attempt did not succeed:

```cpp
switch (te::sdk::InitRakNetHooksEx())
{
case te::sdk::InitResult::Ok:                 break;  // ready
case te::sdk::InitResult::AlreadyInitialized: break;  // also ready
case te::sdk::InitResult::SampNotLoaded:      // keep retrying
case te::sdk::InitResult::RakNetUnavailable:  // keep retrying
    break;
case te::sdk::InitResult::UnsupportedVersion: // retrying will not help
case te::sdk::InitResult::HookFailed:
    break;
}
```

### 3. Register Callbacks

```cpp
#include "te-sdk.h"

void SetupCallbacks()
{
    // Intercept incoming RPCs (from server)
    te::sdk::RegisterRaknetCallback(HookType::IncomingRpc,
        [](const te::sdk::RpcContext& ctx) -> bool {
            // ctx.rpcId     - RPC identifier
            // ctx.bitStream - Data (RakNet::BitStream*)
            // ctx.rakPeer   - RakPeer instance

            // Return true to allow the RPC, false to block it
            return true;
        });

    // Intercept outgoing RPCs (to server)
    te::sdk::RegisterRaknetCallback(HookType::OutgoingRpc,
        [](const te::sdk::RpcContext& ctx) -> bool { return true; });

    // Intercept incoming packets (from server)
    te::sdk::RegisterRaknetCallback(HookType::IncomingPacket,
        [](const te::sdk::PacketContext& ctx) -> bool { return true; });

    // Intercept outgoing packets (to server)
    te::sdk::RegisterRaknetCallback(HookType::OutgoingPacket,
        [](const te::sdk::PacketContext& ctx) -> bool { return true; });
}
```

### 4. Shut down before unloading

```cpp
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_DETACH && lpReserved == nullptr)
    {
        // Dynamic FreeLibrary: take the hooks back out first, otherwise the
        // game keeps jumping into memory that is about to be unmapped.
        te::sdk::ShutdownRakNetHooks();
    }
    return TRUE;
}
```

---

## API Reference

### Initialization / lifecycle

```cpp
bool       te::sdk::InitRakNetHooks();       // true = ready (or already ready)
InitResult te::sdk::InitRakNetHooksEx();     // detailed status
bool       te::sdk::IsInitialized();
bool       te::sdk::IsIncomingRpcSupported();
void       te::sdk::ShutdownRakNetHooks();
const char* te::sdk::GetVersionString();
```

```cpp
enum class InitResult
{
    Ok, AlreadyInitialized, SampNotLoaded,
    UnsupportedVersion, RakNetUnavailable, HookFailed
};
```

### Callback Registration

```cpp
using CallbackId = uint32_t;
constexpr CallbackId kInvalidCallbackId = 0;

CallbackId RegisterRaknetCallback(HookType type, RpcCallback cb,    void* userData = nullptr);
CallbackId RegisterRaknetCallback(HookType type, PacketCallback cb, void* userData = nullptr);
CallbackId RegisterConnectCallback(ConnectionCallback cb,    void* userData = nullptr);
CallbackId RegisterDisconnectCallback(ConnectionCallback cb, void* userData = nullptr);

bool UnregisterRaknetCallback(CallbackId id);
void ClearRaknetCallbacks();
```

**HookType values:**
- `HookType::IncomingRpc` - RPCs received from server
- `HookType::OutgoingRpc` - RPCs sent to server
- `HookType::IncomingPacket` - Packets received from server
- `HookType::OutgoingPacket` - Packets sent to server

**Callback signatures:**
```cpp
using RpcCallback        = std::function<bool(const RpcContext&)>;
using PacketCallback     = std::function<bool(const PacketContext&)>;
using ConnectionCallback = std::function<void(const SessionInfo&)>;
```

Rules that apply to every callback:

- They run on the game's **network thread** - keep them fast.
- They run with **no SDK lock held**, so calling back into the SDK (including
  registering or unregistering callbacks) from inside one is safe.
- Returning `false` blocks the message **and stops later callbacks** for it
  from being invoked.
- An exception escaping a callback is caught, logged, and treated as "allow".
- **Always unregister** (or call `ShutdownRakNetHooks()`) before the module that
  owns the callback is unloaded.

### Context Structures

```cpp
struct RpcContext
{
    uint32_t structSize;   // sizeof(RpcContext) as the SDK built it
    uint32_t rpcId;
    void*    bitStream;    // RakNet::BitStream* with RPC data
    void*    rakPeer;
    uint32_t bitLength;    // payload size in bits
    bool     outgoing;
    bool     canModify;    // writes to bitStream are forwarded on
    void*    userData;     // the value passed at registration
};

struct PacketContext
{
    uint32_t structSize;
    uint32_t packetId;
    void*    bitStream;
    void*    rakPeer;
    uint32_t length;       // payload size in bytes
    bool     outgoing;
    bool     canModify;
    void*    userData;
};
```

`structSize` lets a callback written against a newer SDK detect fields an older
one did not set. It is always the first member.

### Session Information

```cpp
te::sdk::SessionInfo te::sdk::GetSessionInfo();   // returns a snapshot
```

```cpp
struct SessionInfo
{
    char serverIP[64];           // Server IP address (truncated if longer)
    unsigned short serverPort;
    unsigned short clientPort;
    bool isConnected;
    unsigned int deprecated;     // Deprecated parameter from Connect call
    int threadSleepTimer;
};
```

### Sending Data

```cpp
extern te::sdk::TERakClient* te::sdk::LocalClient;
```

**Send an RPC:**
```cpp
RakNet::BitStream bs;
bs.Write<uint8_t>(someData);
bs.Write<float>(someValue);

int rpcId = 25;
te::sdk::LocalClient->SendRPC(rpcId, &bs);
```

**Send a packet:**
```cpp
RakNet::BitStream bs;
bs.Write<uint8_t>(packetId);
bs.Write<uint16_t>(someData);

te::sdk::LocalClient->SendPacket(&bs);
```

**TERakClient methods:**
```cpp
bool IsValid() const;

bool SendRPC(int rpcId, BitStream* bitStream,
             PacketPriority priority = HIGH_PRIORITY,
             PacketReliability reliability = RELIABLE_ORDERED,
             char orderingChannel = 0,
             bool shiftTimestamp = false);

bool SendPacket(BitStream* bitStream,
                PacketPriority priority = HIGH_PRIORITY,
                PacketReliability reliability = RELIABLE_ORDERED,
                char orderingChannel = 0);
```

`LocalClient` is `nullptr` until `InitRakNetHooks()` succeeds and again after
`ShutdownRakNetHooks()`. Check it, or check `IsValid()`.

### Replaying a blocked RPC

```cpp
bool te::sdk::ReplayIncomingRPC(uint8_t rpcId, const uint8_t* data, int numBytes);
```

Feeds an RPC straight to SA-MP's own handler, bypassing TE hooks. Returns
`false` if no incoming RPC has been observed yet (the SDK needs a RakPeer and a
PlayerID to replay with).

---

## Complete Example

```cpp
#include "te-sdk.h"
#include <thread>

std::vector<te::sdk::CallbackId> g_callbacks;

bool OnIncomingRPC(const te::sdk::RpcContext& ctx)
{
    te::sdk::helper::logging::Log("Incoming RPC: %u", ctx.rpcId);

    if (ctx.rpcId == 93)   // example: block SetPlayerHealth
        return false;

    return true;
}

void InitializeSDK()
{
    te::sdk::helper::logging::SetModName("MyMod v1.0");

    while (!te::sdk::InitRakNetHooks())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    g_callbacks.push_back(
        te::sdk::RegisterRaknetCallback(HookType::IncomingRpc, OnIncomingRPC));

    g_callbacks.push_back(te::sdk::RegisterConnectCallback(
        [](const te::sdk::SessionInfo& s) {
            te::sdk::helper::logging::Log("Connected to %s:%hu", s.serverIP, s.serverPort);
        }));

    te::sdk::helper::logging::Log("TE SDK %s initialized", te::sdk::GetVersionString());
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        std::thread(InitializeSDK).detach();
    }
    else if (reason == DLL_PROCESS_DETACH && lpReserved == nullptr)
    {
        te::sdk::ShutdownRakNetHooks();
    }
    return TRUE;
}
```

---

## Working with BitStream

The `bitStream` in context structures is a `RakNet::BitStream*`. Cast it and use RakNet's BitStream API:

```cpp
bool OnIncomingRPC(const te::sdk::RpcContext& ctx)
{
    auto* bs = static_cast<RakNet::BitStream*>(ctx.bitStream);

    uint8_t value;
    bs->Read(value);

    bs->ResetReadPointer();

    // Modify data (write back)
    bs->ResetWritePointer();
    bs->Write<uint8_t>(newValue);

    return true;
}
```

**Common BitStream methods:**
- `Read<T>(var)` / `Write<T>(var)`
- `ReadCompressed<T>(var)` / `WriteCompressed<T>(var)`
- `ResetReadPointer()` / `ResetWritePointer()`
- `GetNumberOfBytesUsed()` / `GetData()`

### Modification rules

`ctx.canModify` tells you whether your edits are forwarded on. All four hooks
now behave the same way - the SDK hands you a stream, and if you changed it,
the changed version is what goes out:

| Hook | Behaviour |
|---|---|
| Outgoing RPC / packet | Edits are forwarded. If you leave the stream untouched the original buffer is passed through **byte for byte**, preserving payloads whose bit length is not a whole number of bytes. |
| Incoming RPC | Same. Note that a *modified* payload is re-encoded on a byte boundary, so its bit length is rounded up to the next multiple of 8. |
| Incoming packet | Edits are copied back over RakNet's buffer. The payload may be **shortened but not grown** - RakNet owns the allocation. Attempts to grow it are logged and ignored. |

---

## Logging

```cpp
namespace log = te::sdk::helper::logging;

// Set your mod name (also selects the log sub-directory)
log::SetModName("MyMod v1.0");

// Log messages (printf-style formatting)
log::Log("Message: %s, Value: %d", "test", 123);   // Level::Info

// Explicit severities
log::LogTrace(...); log::LogDebug(...); log::LogWarn(...); log::LogError(...);
log::LogAt(log::Level::Warn, "...");

// Drop everything below a level (default: Info)
log::SetMinLevel(log::Level::Debug);
log::SetMinLevel(log::Level::Off);      // silence without closing the file

// Turn file logging off entirely - no folder or file is ever created
log::SetLoggingEnabled(false);
bool on = log::IsLoggingEnabled();

// Where the logs go (default: a "te_sdk" folder next to YOUR module)
log::SetLogDirectory("D:\\logs\\mymod");

// Rotate to a new file past this size (default 16 MiB, 0 = never)
log::SetMaxFileSize(4ull * 1024 * 1024);

log::Flush();       // push buffered output to disk
log::ResetSession();
log::Shutdown();    // close the file
```

Log lines carry a timestamp, a level and the originating thread:

```
=== SESSION START (MyMod v1.0 | gta_sa, pid 1234): 2025-01-07 15:30:45 ===
[15:30:45.812] [INFO ] [t:4820] [te::sdk] TE SDK 1.1.0 starting
[15:30:45.813] [WARN ] [t:4820] [te::sdk] Incoming RPC hook unavailable for 0.3.7-R4-2
```

Notes:

- The log directory defaults to the folder containing **your module**, not the
  process working directory.
- `SetModName()` may be called at any time; if a file is already open it is
  closed and the next line starts a fresh file under the new sub-directory.
- The file is opened once and kept open with a 64 KiB buffer - `Log()` no
  longer reopens it per call.
- Files older than 72 hours are pruned on startup.

---

## Helper Functions

```cpp
namespace h = te::sdk::helper;

h::SAMPVersion version = h::GetSAMPVersion();
std::string    name    = h::TranslateSAMPVersion(version);
uint32_t       stamp   = h::GetSAMPVersionSignature();   // raw build stamp

void*     sampInfo = h::GetSAMPInfo();
void*     rakNet   = h::GetRakNetInterface();
uintptr_t base     = h::GetSAMPBase();

// Range-checked memory access (the whole range must be committed & readable)
bool ok  = h::IsReadable(ptr, size);
bool okw = h::IsWritable(ptr, size);
uint32_t val = h::ReadMemory<uint32_t>(address);
bool wrote   = h::WriteMemory<uint32_t>(address, newValue);

// IDA-style signature scan over a module image ('?' / '??' = wildcard)
uintptr_t addr = h::FindPattern("samp.dll", "55 8B EC 83 EC ?? 53 56 57");

// Extract RPC data from raw packet bytes
h::ExtractedRPC rpc;
h::ExtractRPCData(data, length, rpc);
```

**SAMPVersion enum:**
```cpp
enum class SAMPVersion
{
    Unknown = 0,
    R1,      // 0.3.7-R1
    R2,      // 0.3.7-R2
    DL,      // 0.3.DL-R1
    R3,      // 0.3.7-R3
    R4,      // 0.3.7-R4
    R4v2,    // 0.3.7-R4-2
    R5,      // 0.3.7-R5
    Count    // sentinel, not a version
};
```

### Unknown SA-MP builds

Every samp.dll address the SDK uses lives in a single `VersionProfile` struct.
If you run into a build that is not in the table, you can supply the offsets at
runtime instead of waiting for a new SDK release:

```cpp
te::sdk::helper::VersionProfile p{};
p.sampInfo          = 0x26EB94;   // samp.dll relative
p.chatInfo          = 0x26EB80;
p.inputInfo         = 0x26EB84;
p.fnAddToChatWnd    = 0x67BE0;
p.fnSendCommand     = 0x69900;
p.fnHandleRpcPacket = 0x3ADE0;    // 0 = no incoming RPC hook
p.netGameRakClient  = 0;          // CNetGame relative
p.netGameHostname   = 0x131;
p.netGameGameState  = 0x3CD;
p.netGamePools      = 0x3DE;
p.poolsPlayerPool     = 0x4;
p.playerPoolLocalId   = 0x2F1C;
p.playerPoolLocalName = 0x2F20;
p.playerPoolLocalPlayer = 0x2F3A;
p.localNameIsStdString = false;   // true only for R1/R2

te::sdk::helper::RegisterVersionProfile(te::sdk::helper::SAMPVersion::R5, p);
te::sdk::helper::SetSAMPVersionOverride(te::sdk::helper::SAMPVersion::R5);

// only now:
te::sdk::InitRakNetHooks();
```

`GetSAMPVersionSignature()` returns the build stamp the SDK read, which is what
you want to quote when reporting an unrecognised build.

---

## SA-MP Helper Functions

All functions are in the `te::sdk::helper::samp` namespace.

### Chat

```cpp
te::sdk::helper::samp::AddChatMessage("Hello!", 0xFF00FF00);
te::sdk::helper::samp::SendCommand("/kill");
```

### Player / server info

```cpp
const char* name     = te::sdk::helper::samp::GetPlayerName();
const char* hostname = te::sdk::helper::samp::GetServerName();
uint16_t    id       = te::sdk::helper::samp::GetPlayerId();  // 0xFFFF on failure
```

`GetPlayerName()` and `GetServerName()` return a snapshot held in a
**thread-local** buffer. The pointer stays valid until the next call to the same
function on the same thread; copy it if you need to keep it longer.

`te::sdk::helper::samp::kInvalidPlayerId` is the `0xFFFF` sentinel.

### Game State

```cpp
bool loaded  = te::sdk::helper::samp::IsGameLoaded();    // game state >= 5
bool spawned = te::sdk::helper::samp::IsPlayerSpawned(); // state == 14 + CLocalPlayer valid
```

### Custom Chat Commands

```cpp
// Register a command (without the '/' prefix)
te::sdk::helper::samp::RegisterChatCommand("hello", [](const char* params) {
    te::sdk::helper::samp::AddChatMessage("Hello world!", 0xFFFFFFFF);
});

te::sdk::helper::samp::RegisterChatCommand("tp", [](const char* params) {
    // params contains everything after "/tp "
    te::sdk::helper::logging::Log("Teleport params: %s", params);
});

bool had = te::sdk::helper::samp::UnregisterChatCommand("tp");
bool has = te::sdk::helper::samp::IsChatCommandRegistered("hello");
te::sdk::helper::samp::ClearChatCommands();   // also uninstalls the hook
```

Commands are case-insensitive. If a registered command is typed, it is **not**
forwarded to the server. Handlers are invoked with no SDK lock held, so calling
`RegisterChatCommand` / `UnregisterChatCommand` from inside a handler is safe.

Unregister your commands (or call `ShutdownRakNetHooks()`, which clears them)
before unloading your module.

---

## Important Notes

- Initialize the SDK **after** SA-MP has fully loaded
- Callback functions are called from the game's network thread - keep them fast
- Returning `false` from a callback blocks the RPC/packet and short-circuits the
  remaining callbacks for that message
- The SDK automatically detects the SA-MP version
- Call `ShutdownRakNetHooks()` before your module is unloaded

---

## Migrating from 1.0

| 1.0 | 1.1 | Why |
|---|---|---|
| `SessionInfo& GetSessionInfo()` | `SessionInfo GetSessionInfo()` | The struct is written from the network thread; handing out a reference was a data race. Use `const auto& s = GetSessionInfo();` or take it by value. |
| `void RegisterRaknetCallback(...)` | `CallbackId RegisterRaknetCallback(..., void* userData = nullptr)` | Source-compatible - keep the id if you want to unregister later. |
| `int GetPlayerId()` returning `-1` | `uint16_t GetPlayerId()` returning `0xFFFF` | The field really is a `uint16`; reading it as `int` pulled in the neighbouring struct member. |
| `const char* GetPlayerName()` pointing into game memory | thread-local snapshot | The old pointer could be invalidated underneath you. |
| `SessionInfo::depreciated` (docs) | `SessionInfo::deprecated` | The docs never matched the header; the header spelling wins. |
| `HookedRakClientInterface` | removed | It was never part of the build and duplicated the hook logic. |
| `te::sdk::forwarder::*` | removed | Internal plumbing for the class above. |
| Contexts were exactly 12 bytes | now carry `structSize`, `length`/`bitLength`, `outgoing`, `canModify`, `userData` | Field access is unchanged; only aggregate initialisation of a context breaks. |

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `InitRakNetHooks()` returns false | Call `InitRakNetHooksEx()` - it distinguishes "SA-MP not up yet" from "unsupported build". Check the log. |
| Version not detected | Quote `GetSAMPVersionSignature()` when reporting it, or register a profile yourself (see [Unknown SA-MP builds](#unknown-sa-mp-builds)). |
| Callbacks not firing | Verify `IsInitialized()`; for incoming RPCs also check `IsIncomingRpcSupported()`. |
| Game crashes on module unload | Call `ShutdownRakNetHooks()` before unloading. |
| Edits to an incoming packet are ignored | The payload may be shortened but not grown - see [Modification rules](#modification-rules). |
| Unresolved externals when linking | CRT/toolset mismatch - see [ABI compatibility](#abi-compatibility). |
| No log file appears | Logging may be disabled, the level too high, or the directory not writable. Check `SetLogDirectory()`. |

---

## License

See [LICENSE](LICENSE).

This project is released as **freeware**.
You are allowed to **use it in binary form only** (linking `.lib` into your project).
**Modifying, redistributing, or publishing the source code is not permitted.**
