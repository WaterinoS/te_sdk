#pragma once

#include "te-inc.h"
#include "te-helper.h"
#include "te-logger.h"
#include "te-rakclient.h"

enum class HookType
{
    OutgoingRpc,
    IncomingRpc,
    OutgoingPacket,
    IncomingPacket
};

namespace te_sdk
{
    struct RpcContext
    {
        uint8_t rpcId;
        void* bitStream;
        void* rakPeer;
    };

    struct PacketContext
    {
        uint8_t packetId;
        void* bitStream;
        void* rakPeer;
    };

    using RpcCallback = std::function<bool(const RpcContext&)>;
    using PacketCallback = std::function<bool(const PacketContext&)>;
    using tHandleRpcPacket = bool(__cdecl*)(void* rp, const char* data, int length, PlayerID playerid);

    void RegisterRaknetCallback(HookType type, RpcCallback callback);
    void RegisterRaknetCallback(HookType type, PacketCallback callback);
    bool InitRakNetHooks();

    // RPC hook function
    bool __cdecl hkHandleRpcPacket(void* rp, const char* data, int length, PlayerID playerid);
    bool AttachHandleRpcPacketHook();
    bool IsSupportedSAMPVersion(helper::SAMPVersion version);

    extern TERakClient* LocalClient;
}

namespace te_sdk::forwarder
{
    bool ForwardOutgoingRpc(uint8_t rpcId, void* bitStream, void* rakPeer);
    bool ForwardIncomingRpc(uint8_t rpcId, void* bitStream, void* rakPeer);
    bool ForwardOutgoingPacket(uint8_t packetId, void* bitStream, void* rakPeer);
    bool ForwardIncomingPacket(uint8_t packetId, void* bitStream, void* rakPeer);

    struct HookForwarder
    {
        decltype(&ForwardOutgoingRpc) OutgoingRpc = ForwardOutgoingRpc;
        decltype(&ForwardIncomingRpc) IncomingRpc = ForwardIncomingRpc;
        decltype(&ForwardOutgoingPacket) OutgoingPacket = ForwardOutgoingPacket;
        decltype(&ForwardIncomingPacket) IncomingPacket = ForwardIncomingPacket;
    };

    extern HookForwarder g_forwarder;
}

#include "te-hookedrakclient.h"