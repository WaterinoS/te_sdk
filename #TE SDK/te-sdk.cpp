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
    using namespace te_sdk::helper::logging;

    TERakClient* LocalClient = nullptr;
    tWSARecvFrom oWSARecvFrom;

    std::unordered_map<uint16_t, PacketFragment> g_incompletePackets;
    std::mutex g_packetMutex;

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
        int result = oWSARecvFrom(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen, lpOverlapped, lpCompletionRoutine);

        if (result == 0 && lpNumberOfBytesRecvd && *lpNumberOfBytesRecvd > 0 && lpBuffers && lpBuffers->len > 0)
        {
            helper::ExtractedRPC rpc;
            if (LocalClient && LocalClient->GetInterface() &&
                helper::ExtractRPCData(reinterpret_cast<const char*>(lpBuffers->buf), *lpNumberOfBytesRecvd, rpc))
            {
                std::lock_guard<std::mutex> lock(g_packetMutex);
                bool shouldBlock = false;

                if (rpc.isSplitPacket) {
                    // Process split packet fragments
                    uint16_t splitId = rpc.splitPacketId;
                    auto& fragment = g_incompletePackets[splitId];

                    // Initialize on first fragment
                    if (fragment.expectedFragments == 0) {
                        fragment.expectedFragments = rpc.splitPacketCount;
                        fragment.timestamp = std::chrono::steady_clock::now();

                        Log("[te_sdk] Started reassembly of split packet ID %d, expected fragments: %d",
                            splitId, fragment.expectedFragments);
                    }

                    // Validate fragment data
                    if (rpc.splitPacketIndex >= fragment.expectedFragments) {
                        Log("[te_sdk] Invalid split packet index %d for packet %d (expected < %d)",
                            rpc.splitPacketIndex, splitId, fragment.expectedFragments);
                        g_incompletePackets.erase(splitId);
                    }
                    else if (fragment.fragments.find(rpc.splitPacketIndex) != fragment.fragments.end()) {
                        Log("[te_sdk] Duplicate fragment %d for packet %d", rpc.splitPacketIndex, splitId);
                    }
                    else {
                        // Store fragment data
                        fragment.fragments[rpc.splitPacketIndex] = rpc.payload;

                        /*Log("[te_sdk] Received fragment %d/%d for packet %d (size: %d)",
                            rpc.splitPacketIndex + 1, fragment.expectedFragments, splitId, rpc.payload.size());*/

                        // Check if packet is complete
                        if (fragment.fragments.size() == fragment.expectedFragments) {
                            Log("[te_sdk] Split packet %d is complete, reassembling %d fragments (estimated size: %d KB)...",
                                splitId, fragment.expectedFragments,
                                (fragment.expectedFragments * 1400) / 1024); // Estimate based on typical fragment size

                            // Reassemble with progress for large packets
                            std::vector<uint8_t> completeData;
                            size_t estimatedSize = fragment.expectedFragments * 1400; // Rough estimate
                            completeData.reserve(estimatedSize); // Pre-allocate memory

                            for (uint32_t i = 0; i < fragment.expectedFragments; ++i) {
                                auto it = fragment.fragments.find(i);
                                if (it != fragment.fragments.end()) {
                                    completeData.insert(completeData.end(), it->second.begin(), it->second.end());

                                    // Log progress for very large packets
                                    if (fragment.expectedFragments > 100 && (i + 1) % 50 == 0) {
                                        Log("[te_sdk] Reassembly progress: %d/%d fragments", i + 1, fragment.expectedFragments);
                                    }
                                }
                                else {
                                    Log("[te_sdk] Missing fragment %d for packet %d", i, splitId);
                                    break;
                                }
                            }

                            if (completeData.size() > 0) {
                                Log("[te_sdk] Reassembled packet %d, total size: %d KB", splitId, completeData.size() / 1024);
                                if (!ProcessCompletePacket(completeData)) {
                                    shouldBlock = true;
                                }
                            }

                            g_incompletePackets.erase(splitId);
                        }
                    }
                }
                else {
                    // Process complete (non-split) packet
                    if (!ProcessCompleteRPC(rpc)) {
                        shouldBlock = true;
                    }
                }

                // Cleanup old fragments (30 second timeout)
                auto now = std::chrono::steady_clock::now();
                for (auto it = g_incompletePackets.begin(); it != g_incompletePackets.end();) {
                    if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count() > 30) {
                        Log("[te_sdk] Timeout for split packet %d", it->first);
                        it = g_incompletePackets.erase(it);
                    }
                    else {
                        ++it;
                    }
                }

                // Block packet if callback returned false
                if (shouldBlock) {
                    if (lpNumberOfBytesRecvd) {
                        *lpNumberOfBytesRecvd = 0;
                    }

                    if (lpFlags) {
                        *lpFlags |= MSG_DONTROUTE; // Indicate that this packet should not be routed
					}
                }
            }
        }

        return result;
    }

    bool ProcessCompletePacket(const std::vector<uint8_t>& data) {
        Log("[te_sdk] ProcessCompletePacket called with %d bytes", data.size());

        // The reassembled data is raw packet data, not a full RakNet packet
        // We need to parse it directly as packet content
        if (data.size() < 2) {
            Log("[te_sdk] Reassembled packet too small (%d bytes)", data.size());
            return true;
        }

        try {
            BitStream dataBitStream(const_cast<unsigned char*>(data.data()), data.size(), false);

            BYTE packetId = 0;
            if (!dataBitStream.Read(packetId)) {
                Log("[te_sdk] Failed to read packetId from reassembled data");
                return true;
            }

            if (packetId != 20) {
                //Log("[te_sdk] Reassembled packet is not RPC (packetId: %d), allowing", packetId);
                return true; // Allow non-RPC packets
            }

            BYTE rpcId = 0;
            if (!dataBitStream.Read(rpcId)) {
                Log("[te_sdk] Failed to read rpcId from reassembled data");
                return true;
            }

            // Extract remaining payload
            int unreadBits = dataBitStream.GetNumberOfUnreadBits();
            int unreadBytes = unreadBits / 8;
            std::vector<BYTE> payload;

            if (unreadBytes > 0) {
                payload.resize(unreadBytes);
                if (!dataBitStream.Read(reinterpret_cast<char*>(payload.data()), unreadBytes)) {
                    Log("[te_sdk] Failed to read payload from reassembled data");
                    return true;
                }
            }

            Log("[te_sdk] Processing reassembled RPC %d (payload size: %d)", rpcId, payload.size());

            // Create BitStream for callback
            BitStream rpcData(payload.data(), payload.size(), false);
            bool allowed = g_forwarder.IncomingRpc(rpcId, &rpcData, LocalClient->GetInterface());

            //Log("[te_sdk] RPC %d callback result: %s", rpcId, allowed ? "ALLOWED" : "BLOCKED");

            if (!allowed) {
                return false; // Block packet
            }
            else {
                return true; // Allow packet
            }
        }
        catch (const std::exception& e) {
            Log("[te_sdk] Exception while processing reassembled packet: %s", e.what());
            return true; // Allow packet on exception
        }
        catch (...) {
            Log("[te_sdk] Unknown exception while processing reassembled packet");
            return true; // Allow packet on exception
        }
    }

    bool ProcessCompleteRPC(const helper::ExtractedRPC& rpc) {
        if (rpc.packetId == 20) {
            BitStream rpcData(const_cast<unsigned char*>(rpc.payload.data()), rpc.payload.size(), false);
            bool allowed = g_forwarder.IncomingRpc(rpc.rpcId, &rpcData, LocalClient->GetInterface());

            //Log("[te_sdk] Complete RPC %d callback result: %s", rpc.rpcId, allowed ? "ALLOWED" : "BLOCKED");

            if (!allowed) {
                return false; // Block packet
            }
            else {
                return true; // Allow packet
            }
        }
        return true; // Allow non-RPC packets
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