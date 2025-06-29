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

    struct PacketFragment {
        std::map<uint32_t, std::vector<uint8_t>> fragments;
        uint32_t expectedFragments = 0;
        std::chrono::steady_clock::time_point timestamp;
    };

    using RpcCallback = std::function<bool(const RpcContext&)>;
    using PacketCallback = std::function<bool(const PacketContext&)>;

    using tWSARecvFrom = int (WINAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, struct sockaddr*, LPINT, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);

    void RegisterRaknetCallback(HookType type, RpcCallback callback);
    void RegisterRaknetCallback(HookType type, PacketCallback callback);
    bool InitRakNetHooks();

    int WINAPI hkWSARecvFrom(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd, LPDWORD lpFlags, struct sockaddr* lpFrom, LPINT lpFromlen, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);

    bool ProcessCompletePacket(const std::vector<uint8_t>& data);
    bool ProcessCompleteRPC(const helper::ExtractedRPC& rpc);

    extern TERakClient* LocalClient;
    extern tWSARecvFrom oWSARecvFrom;
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