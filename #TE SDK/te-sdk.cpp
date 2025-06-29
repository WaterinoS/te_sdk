#include "te-sdk.h"
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
    tWSARecvFrom oWSARecvFrom;

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

    int WINAPI hkWSARecvFrom(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd, LPDWORD lpFlags, struct sockaddr* lpFrom, LPINT lpFromlen, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
    {
        //helper::Log("[te_sdk] hkWSARecvFrom called");

        helper::ExtractedRPC rpc;
        if (LocalClient && LocalClient->GetInterface() && lpBuffers && lpBuffers->len > 0 && helper::ExtractRPCData(reinterpret_cast<const char*>(lpBuffers->buf), lpBuffers->len, rpc))
        {
            BitStream rpcData(reinterpret_cast<unsigned char*>(rpc.payload.data()), rpc.payload.size(), false);
            if (g_forwarder.IncomingRpc(rpc.rpcId, &rpcData, LocalClient->GetInterface()))
            {
                return oWSARecvFrom(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen, lpOverlapped, lpCompletionRoutine);
            }
            else
            {
                return 0;
            }
		}

        return oWSARecvFrom(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen, lpOverlapped, lpCompletionRoutine);
    }


    bool InitRakNetHooks()
    {
        using namespace te_sdk::helper;

        if (LocalClient)
        {
            Log("[te_sdk] RakNet hooks already initialized.");
            return false;
		}

        SAMPVersion version = GetSAMPVersion();
        if (version == SAMPVersion::Unknown)
        {
            Log("[te_sdk] Unsupported SAMP version. Hooking aborted.");
            return false;
        }

		auto sampInfo = GetSAMPInfo();
        if (!sampInfo)
        {
            return false;
		}

        Log("[te_sdk] Detected SAMP version: %s", TranslateSAMPVersion(version).c_str());
        Log("[te_sdk] SAMP info found at %p", sampInfo);

        Log("[te_sdk] Initializing RakNet hooks...");
        void* rak = GetRakNetInterface();
        if (!rak)
        {
			Log("[te_sdk] RakNet interface is not available.");
            return false;
        }

        if (AttachWSAHooks())
        {
            LocalClient = new TERakClient(*reinterpret_cast<void**>(rak));
            auto* hooked = new HookedRakClientInterface();
            hooked->SetForwarder(&g_forwarder);

            DWORD oldProtect;
            VirtualProtect((void*)rak, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
            *reinterpret_cast<void**>(rak) = hooked;
            VirtualProtect((void*)rak, sizeof(void*), oldProtect, &oldProtect);

            Log("[te_sdk] RakNet hooks initialized successfully.");
            return true;
        }
        return false;
    }
}