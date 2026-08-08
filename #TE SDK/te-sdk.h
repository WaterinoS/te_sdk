#pragma once

#include "te-inc.h"
#include "te-helper.h"
#include "te-samp.h"
#include "te-logger.h"
#include "te-rakclient.h"

// ---- SDK version ----
#define TE_SDK_VERSION_MAJOR 1
#define TE_SDK_VERSION_MINOR 1
#define TE_SDK_VERSION_PATCH 0
#define TE_SDK_VERSION_STRING "1.1.0"

enum class HookType
{
    OutgoingRpc,
    IncomingRpc,
    OutgoingPacket,
    IncomingPacket
};

namespace te::sdk
{
    // Identifies a registered callback. Keep it so the callback can be removed
    // again - see UnregisterRaknetCallback(). kInvalidCallbackId is returned
    // when registration fails.
    using CallbackId = uint32_t;
    constexpr CallbackId kInvalidCallbackId = 0;

    // `structSize` is set by the SDK to sizeof(the struct it was built with).
    // Check it before reading fields added in a later SDK version.
    struct RpcContext
    {
        uint32_t structSize;
        uint32_t rpcId;
        void* bitStream;      // RakNet::BitStream*
        void* rakPeer;

        uint32_t bitLength;   // payload size in bits
        bool outgoing;        // true for OutgoingRpc, false for IncomingRpc

        // True when writes to `bitStream` are forwarded on. When false the
        // stream is a read-only snapshot and edits are discarded.
        bool canModify;

        // The value passed to RegisterRaknetCallback() for this callback
        void* userData;
    };

    struct PacketContext
    {
        uint32_t structSize;
        uint32_t packetId;
        void* bitStream;      // RakNet::BitStream*
        void* rakPeer;

        uint32_t length;      // payload size in bytes
        bool outgoing;        // true for OutgoingPacket, false for IncomingPacket

        // Incoming packets may be edited in place or shortened, but NOT grown
        // past their original length - RakNet owns the buffer.
        bool canModify;

        void* userData;
    };

    struct SessionInfo
    {
        char serverIP[64];           // Server IP address
        unsigned short serverPort;   // Server port
        unsigned short clientPort;   // Client port used for connection
        bool isConnected;            // Connection status
        unsigned int deprecated;     // Deprecated parameter from Connect call
        int threadSleepTimer;        // Thread sleep timer from Connect call
    };

    enum class InitResult
    {
        Ok = 0,
        AlreadyInitialized,
        SampNotLoaded,
        UnsupportedVersion,
        RakNetUnavailable,
        HookFailed
    };

    using RpcCallback = std::function<bool(const RpcContext&)>;
    using PacketCallback = std::function<bool(const PacketContext&)>;
    using ConnectionCallback = std::function<void(const SessionInfo&)>;

    // ---- callback registration ----
    //
    // Callbacks run on the game's network thread with NO SDK lock held, so they
    // may freely call back into the SDK. Returning false blocks the RPC/packet
    // AND stops later callbacks for that message from being invoked.

    CallbackId RegisterRaknetCallback(HookType type, RpcCallback callback, void* userData = nullptr);
    CallbackId RegisterRaknetCallback(HookType type, PacketCallback callback, void* userData = nullptr);

    // Connection lifecycle notifications (fired from Connect/Disconnect hooks)
    CallbackId RegisterConnectCallback(ConnectionCallback callback, void* userData = nullptr);
    CallbackId RegisterDisconnectCallback(ConnectionCallback callback, void* userData = nullptr);

    // Remove a callback by id. Returns false if the id was not registered.
    // ALWAYS unregister before the module owning the callback is unloaded.
    bool UnregisterRaknetCallback(CallbackId id);

    // Remove every registered callback of every kind.
    void ClearRaknetCallbacks();

    // ---- lifecycle ----

    // Initialise the SDK and hook RakNet.
    // Returns true when the SDK is ready - including when it was already
    // initialised by an earlier call, so a retry loop terminates.
    bool InitRakNetHooks();

    // Same, but reports exactly what happened.
    InitResult InitRakNetHooksEx();

    // True once InitRakNetHooks() has succeeded.
    bool IsInitialized();

    // True when the incoming-RPC hook is active (needs a known
    // handle_rpc_packet offset for the detected SA-MP version).
    bool IsIncomingRpcSupported();

    // Remove every hook, drop every callback and release the SDK's resources.
    // Call this before unloading your module - leaving hooks installed while
    // the code they jump to is unmapped crashes the game.
    // Quiesce your own threads first; hooks already executing are not waited on.
    void ShutdownRakNetHooks();

    // ---- info ----

    // Returns a snapshot of the current session. This is a copy on purpose:
    // the fields are written from the network thread.
    SessionInfo GetSessionInfo();

    bool IsSupportedSAMPVersion(helper::SAMPVersion version);

    const char* GetVersionString();

    extern TERakClient* LocalClient;

    // Replay a previously blocked RPC through the original handler, bypassing
    // TE hooks. Only valid after at least one incoming RPC has been observed.
    bool ReplayIncomingRPC(uint8_t rpcId, const uint8_t* data, int numBytes);

    static_assert(offsetof(RpcContext, structSize) == 0,
                  "structSize must stay the first member of RpcContext");
    static_assert(offsetof(PacketContext, structSize) == 0,
                  "structSize must stay the first member of PacketContext");
}
