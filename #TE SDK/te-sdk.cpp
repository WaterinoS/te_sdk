#include "te-sdk.h"
#include <mutex>

#include <mutex>
#include <vector>

namespace te_sdk::forwarder
{
    std::mutex g_mutex;

    std::vector<te_sdk::RpcCallback> g_outgoingRpcCallbacks;
    std::vector<te_sdk::RpcCallback> g_incomingRpcCallbacks;
    std::vector<te_sdk::PacketCallback> g_outgoingPacketCallbacks;
    std::vector<te_sdk::PacketCallback> g_incomingPacketCallbacks;

    bool OnOutgoingRpc(uint8_t rpcId, void* bitStream, void* rakPeer)
    {
        te_sdk::RpcContext ctx{ rpcId, bitStream, rakPeer };
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& cb : g_outgoingRpcCallbacks)
            if (!cb(ctx))
                return false;

        return true;
    }

    bool OnIncomingRpc(uint8_t rpcId, void* bitStream, void* rakPeer)
    {
        te_sdk::RpcContext ctx{ rpcId, bitStream, rakPeer };
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& cb : g_incomingRpcCallbacks)
            if (!cb(ctx))
                return false;

        return true;
    }

    bool OnOutgoingPacket(uint8_t packetId, void* bitStream, void* rakPeer)
    {
        te_sdk::PacketContext ctx{ packetId, bitStream, rakPeer };
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& cb : g_outgoingPacketCallbacks)
            if (!cb(ctx))
                return false;

        return true;
    }

    bool OnIncomingPacket(uint8_t packetId, void* bitStream, void* rakPeer)
    {
        te_sdk::PacketContext ctx{ packetId, bitStream, rakPeer };
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& cb : g_incomingPacketCallbacks)
            if (!cb(ctx))
                return false;

        return true;
    }

    bool ForwardOutgoingRpc(uint8_t rpcId, void* bitStream, void* rakPeer) { return OnOutgoingRpc(rpcId, bitStream, rakPeer); }
    bool ForwardIncomingRpc(uint8_t rpcId, void* bitStream, void* rakPeer) { return OnIncomingRpc(rpcId, bitStream, rakPeer); }
    bool ForwardOutgoingPacket(uint8_t packetId, void* bitStream, void* rakPeer) { return OnOutgoingPacket(packetId, bitStream, rakPeer); }
    bool ForwardIncomingPacket(uint8_t packetId, void* bitStream, void* rakPeer) { return OnIncomingPacket(packetId, bitStream, rakPeer); }

    HookForwarder g_forwarder;
}

namespace te_sdk
{
	using namespace te_sdk::forwarder;

    TERakClient* LocalClient = nullptr;

    void RegisterRaknetCallback(HookType type, RpcCallback callback)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        switch (type)
        {
        case HookType::OutgoingRpc:    g_outgoingRpcCallbacks.push_back(callback); break;
        case HookType::IncomingRpc:    g_incomingRpcCallbacks.push_back(callback); break;
        default: break;
        }
    }

    void RegisterRaknetCallback(HookType type, PacketCallback callback)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        switch (type)
        {
        case HookType::OutgoingPacket: g_outgoingPacketCallbacks.push_back(callback); break;
        case HookType::IncomingPacket: g_incomingPacketCallbacks.push_back(callback); break;
        default: break;
        }
    }

    void InitRakNetHooks()
    {
        using namespace te_sdk_helper;

        void* rak = GetRakNetInterface();
        if (!rak)
        {
            Log("[te_sdk] RakNet interface not found. Hooking aborted.");
            return;
        }

        Log("[te_sdk] RakNet interface found at %p", rak);

        LocalClient = new TERakClient(rak);
        auto* hooked = new HookedRakClientInterface();
        hooked->SetForwarder(&g_forwarder);

        *reinterpret_cast<void**>(rak) = hooked;

        Log("[te_sdk] HookedRakClientInterface installed successfully.");
    }
}