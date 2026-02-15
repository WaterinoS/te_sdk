# TE SDK

A lightweight C++ SDK library for hooking RakNet networking in SA-MP (San Andreas Multiplayer). Build your own mods, tools, and network analyzers with ease.

---

## Features

- **RakNet Hooking** - Intercept and modify network traffic
  - Incoming RPC callbacks
  - Outgoing RPC callbacks
  - Incoming Packet callbacks
  - Outgoing Packet callbacks

- **Multi-Version Support** - Works with multiple SA-MP versions:
  - 0.3.7-R1
  - 0.3.7-R3
  - 0.3.7-R4
  - 0.3.7-R5
  - 0.3.DL-R1

- **Send Custom Network Data** - Use `TERakClient` to send your own RPCs and packets

- **Session Information** - Access connection details (server IP, port, connection status)

---

## Quick Start

### 1. Project Setup

1. Link `te_sdk.lib` to your project
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

### 3. Register Callbacks

Register your callback functions to intercept network traffic:

```cpp
#include "te-sdk.h"

void SetupCallbacks()
{
    // Intercept incoming RPCs (from server)
    te::sdk::RegisterRaknetCallback(HookType::IncomingRpc,
        [](const te::sdk::RpcContext& ctx) -> bool {
            // ctx.rpcId    - RPC identifier
            // ctx.bitStream - Data (RakNet::BitStream*)
            // ctx.rakPeer  - RakPeer instance

            // Return true to allow the RPC, false to block it
            return true;
        });

    // Intercept outgoing RPCs (to server)
    te::sdk::RegisterRaknetCallback(HookType::OutgoingRpc,
        [](const te::sdk::RpcContext& ctx) -> bool {
            return true;
        });

    // Intercept incoming packets (from server)
    te::sdk::RegisterRaknetCallback(HookType::IncomingPacket,
        [](const te::sdk::PacketContext& ctx) -> bool {
            // ctx.packetId  - Packet identifier
            // ctx.bitStream - Data (RakNet::BitStream*)
            // ctx.rakPeer   - RakPeer instance

            return true;
        });

    // Intercept outgoing packets (to server)
    te::sdk::RegisterRaknetCallback(HookType::OutgoingPacket,
        [](const te::sdk::PacketContext& ctx) -> bool {
            return true;
        });
}
```

---

## API Reference

### Initialization

```cpp
bool te::sdk::InitRakNetHooks();
```
Initializes the SDK and hooks RakNet. Returns `true` on success.

### Callback Registration

```cpp
void te::sdk::RegisterRaknetCallback(HookType type, RpcCallback callback);
void te::sdk::RegisterRaknetCallback(HookType type, PacketCallback callback);
```

**HookType values:**
- `HookType::IncomingRpc` - RPCs received from server
- `HookType::OutgoingRpc` - RPCs sent to server
- `HookType::IncomingPacket` - Packets received from server
- `HookType::OutgoingPacket` - Packets sent to server

**Callback signatures:**
```cpp
using RpcCallback = std::function<bool(const RpcContext&)>;
using PacketCallback = std::function<bool(const PacketContext&)>;
```

### Context Structures

```cpp
struct RpcContext
{
    uint32_t rpcId;     // RPC identifier
    void* bitStream;    // RakNet::BitStream* with RPC data
    void* rakPeer;      // RakPeer instance
};

struct PacketContext
{
    uint32_t packetId;  // Packet identifier
    void* bitStream;    // RakNet::BitStream* with packet data
    void* rakPeer;      // RakPeer instance
};
```

### Session Information

```cpp
te::sdk::SessionInfo& te::sdk::GetSessionInfo();
```

```cpp
struct SessionInfo
{
    char serverIP[64];           // Server IP address
    unsigned short serverPort;   // Server port
    unsigned short clientPort;   // Client port
    bool isConnected;            // Connection status
    unsigned int depreciated;    // Deprecated parameter
    int threadSleepTimer;        // Thread sleep timer
};
```

### Sending Data

Access the `TERakClient` to send custom RPCs and packets:

```cpp
extern te::sdk::TERakClient* te::sdk::LocalClient;
```

**Send an RPC:**
```cpp
RakNet::BitStream bs;
bs.Write<uint8_t>(someData);
bs.Write<float>(someValue);

int rpcId = 25;  // Your RPC ID
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
bool SendRPC(int rpcId, BitStream* bitStream,
             PacketPriority priority = HIGH_PRIORITY,
             PacketReliability reliability = RELIABLE_ORDERED,
             char orderingChannel = 0,
             bool shiftTimestamp = false);

bool SendPacket(BitStream* bitStream,
                PacketPriority priority = HIGH_PRIORITY,
                PacketReliability reliability = UNRELIABLE_SEQUENCED,
                char orderingChannel = 0);
```

---

## Complete Example

```cpp
#include "te-sdk.h"
#include <thread>

// Log RPC callback
bool OnIncomingRPC(const te::sdk::RpcContext& ctx)
{
    // Example: Log all incoming RPCs
    te::sdk::helper::logging::Log("Incoming RPC: %d", ctx.rpcId);

    // Example: Block a specific RPC
    if (ctx.rpcId == 93)  // Example: block SetPlayerHealth
    {
        return false;  // Block this RPC
    }

    return true;  // Allow other RPCs
}

bool OnOutgoingRPC(const te::sdk::RpcContext& ctx)
{
    te::sdk::helper::logging::Log("Outgoing RPC: %d", ctx.rpcId);
    return true;
}

bool OnIncomingPacket(const te::sdk::PacketContext& ctx)
{
    te::sdk::helper::logging::Log("Incoming Packet: %d", ctx.packetId);
    return true;
}

bool OnOutgoingPacket(const te::sdk::PacketContext& ctx)
{
    te::sdk::helper::logging::Log("Outgoing Packet: %d", ctx.packetId);
    return true;
}

void InitializeSDK()
{
    // Wait for SA-MP to fully load
    while (!te::sdk::helper::GetSAMPInfo())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Initialize hooks
    if (!te::sdk::InitRakNetHooks())
    {
        te::sdk::helper::logging::Log("Failed to initialize TE SDK!");
        return;
    }

    // Register callbacks
    te::sdk::RegisterRaknetCallback(HookType::IncomingRpc, OnIncomingRPC);
    te::sdk::RegisterRaknetCallback(HookType::OutgoingRpc, OnOutgoingRPC);
    te::sdk::RegisterRaknetCallback(HookType::IncomingPacket, OnIncomingPacket);
    te::sdk::RegisterRaknetCallback(HookType::OutgoingPacket, OnOutgoingPacket);

    te::sdk::helper::logging::Log("TE SDK initialized successfully!");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        std::thread(InitializeSDK).detach();
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

    // Read data from the stream
    uint8_t value;
    bs->Read(value);

    // Reset read position if needed
    bs->ResetReadPointer();

    // Modify data (write back)
    bs->ResetWritePointer();
    bs->Write<uint8_t>(newValue);

    return true;
}
```

**Common BitStream methods:**
- `Read<T>(var)` - Read a value
- `Write<T>(var)` - Write a value
- `ReadCompressed<T>(var)` - Read compressed value
- `WriteCompressed<T>(var)` - Write compressed value
- `ResetReadPointer()` - Reset read position to start
- `ResetWritePointer()` - Reset write position to start
- `GetNumberOfBytesUsed()` - Get data size in bytes
- `GetData()` - Get raw data pointer

---

## Logging

Use the built-in logging system:

```cpp
// Set your mod name (shown in session headers)
te::sdk::helper::logging::SetModName("MyMod v1.0");

// Log messages (printf-style formatting)
te::sdk::helper::logging::Log("Message: %s, Value: %d", "test", 123);

// Get the current mod name
const char* name = te::sdk::helper::logging::GetModName();
```

Logs are written to `te_sdk/` folder with session headers:
```
=== SESSION START (MyMod v1.0 | game.exe, pid 1234): 2025-01-07 15:30:45 ===
```

If `SetModName()` is not called, the session header uses the executable name only.

---

## Helper Functions

```cpp
// Get detected SA-MP version
te::sdk::helper::SAMPVersion version = te::sdk::helper::GetSAMPVersion();

// Get version as string
std::string versionStr = te::sdk::helper::TranslateSAMPVersion(version);

// Get SAMP_INFO pointer
void* sampInfo = te::sdk::helper::GetSAMPInfo();

// Get RakNet interface
void* rakNet = te::sdk::helper::GetRakNetInterface();

// Get samp.dll base address
uintptr_t base = te::sdk::helper::GetSAMPBase();

// Safe memory read/write templates
uint32_t val = te::sdk::helper::ReadMemory<uint32_t>(address);
bool ok = te::sdk::helper::WriteMemory<uint32_t>(address, newValue);

// Extract RPC data from raw packet bytes
te::sdk::helper::ExtractedRPC rpc;
te::sdk::helper::ExtractRPCData(data, length, rpc);
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
    R5       // 0.3.7-R5
};
```

---

## SA-MP Helper Functions

All functions are in the `te::sdk::helper::samp` namespace and support all SA-MP versions (R1, R2, R3, R4, R4v2, R5, DL).

### Chat

```cpp
// Add a message to the SA-MP chat window
te::sdk::helper::samp::AddChatMessage("Hello!", 0xFF00FF00);

// Send a command to the server
te::sdk::helper::samp::SendCommand("/kill");
```

### Player Info

```cpp
// Get local player name
const char* name = te::sdk::helper::samp::GetPlayerName();

// Get local player ID (-1 on failure)
int id = te::sdk::helper::samp::GetPlayerId();
```

### Server Info

```cpp
// Get the server hostname
const char* hostname = te::sdk::helper::samp::GetServerName();
```

### Game State

```cpp
// Check if connected to a server (game state >= 5)
bool loaded = te::sdk::helper::samp::IsGameLoaded();

// Check if player is fully spawned (game state == 14 + CLocalPlayer valid)
bool spawned = te::sdk::helper::samp::IsPlayerSpawned();
```

### Custom Chat Commands

Register client-side command handlers that intercept commands before they're sent to the server:

```cpp
// Register a command (without the '/' prefix)
te::sdk::helper::samp::RegisterChatCommand("hello", [](const char* params) {
    te::sdk::helper::samp::AddChatMessage("Hello world!", 0xFFFFFFFF);
});

// Command with parameters
te::sdk::helper::samp::RegisterChatCommand("tp", [](const char* params) {
    // params contains everything after "/tp "
    te::sdk::helper::logging::Log("Teleport params: %s", params);
});
```

Commands are case-insensitive. If a registered command is typed, it is **not** forwarded to the server.

---

## Important Notes

- Initialize the SDK **after** SA-MP has fully loaded
- Callback functions are called from the game's network thread - keep them fast
- Returning `false` from a callback blocks the RPC/packet
- The SDK automatically detects the SA-MP version
- Incoming RPC hooks are supported on R1, R3, R4, R5, and DL versions

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `InitRakNetHooks()` returns false | Ensure SA-MP is fully loaded before calling |
| Callbacks not firing | Verify hooks initialized successfully |
| Game crashes | Check callback return values and BitStream operations |
| Version not detected | Ensure you're using a supported SA-MP version |

---

## License

This project is released as **freeware**.
You are allowed to **use it in binary form only** (linking `.lib` into your project).
**Modifying, redistributing, or publishing the source code is not permitted.**
