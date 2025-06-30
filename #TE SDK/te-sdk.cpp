#include "te-sdk.h"
#include "FullRakNet/PacketEnumerations.h"

#include <mutex>
#include <vector>
#include <memory>

#include "Detours/detours_x86.h"
#pragma comment(lib, "Detours/detours_x86.lib")

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
    tHandleRpcPacket oHandleRpcPacket = nullptr;

    void* g_rakPeer = nullptr;
    PlayerID g_playerId = { 0, 0 };

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

    bool __cdecl hkHandleRpcPacket(void* rp, const char* data, int length, PlayerID playerid) {
        g_rakPeer = rp;
        g_playerId = playerid;

        // Basic validation
        if (!rp || !data || length <= 0 || !oHandleRpcPacket) {
            if (oHandleRpcPacket) {
                return oHandleRpcPacket(rp, data, length, playerid);
            }
            return false;
        }

        const size_t _MAX_ALLOCA_STACK_ALLOCATION = 1024;

        try {
            // Create BitStream from the incoming data
            RakNet::BitStream incoming(const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data)),
                static_cast<unsigned int>(length), false); // CHANGED: set copyData to false

            unsigned char id = 0;
            unsigned char* input = nullptr;
            unsigned int bits_data = 0;

            // Skip packet header (first byte is packet ID)
            incoming.IgnoreBits(8);

            // Handle timestamp if present
            if (length > 0 && data[0] == ID_TIMESTAMP) {
                incoming.IgnoreBits(8 * (sizeof(RakNetTime) + sizeof(unsigned char)));
            }

            // Read RPC ID
            if (!incoming.Read(id)) {
                Log("[te_sdk] Failed to read RPC ID");
                return oHandleRpcPacket(rp, data, length, playerid);
            }

            // Read compressed data size
            if (!incoming.ReadCompressed(bits_data)) {
                Log("[te_sdk] Failed to read compressed data size");
                return oHandleRpcPacket(rp, data, length, playerid);
            }

            // Create BitStream for the callback
            std::unique_ptr<RakNet::BitStream> callback_bs;

            if (bits_data > 0) {
                // Calculate required bytes
                unsigned int bytes_needed = BITS_TO_BYTES(bits_data);
                unsigned int available_bits = incoming.GetNumberOfUnreadBits();

                if (bits_data > available_bits) {
                    Log("[te_sdk] Invalid data size: requested %u bits, available %u bits", bits_data, available_bits);
                    return oHandleRpcPacket(rp, data, length, playerid);
                }

                // Allocate buffer for RPC data
                bool used_alloca = false;

                if (bytes_needed < _MAX_ALLOCA_STACK_ALLOCATION) {
                    input = static_cast<unsigned char*>(alloca(bytes_needed));
                    used_alloca = true;
                }
                else {
                    input = new unsigned char[bytes_needed];
                }

                // Read the RPC data
                if (!incoming.ReadBits(input, bits_data, false)) {
                    Log("[te_sdk] Failed to read RPC data bits");
                    if (!used_alloca) {
                        delete[] input;
                    }
                    return oHandleRpcPacket(rp, data, length, playerid);
                }

                // Create BitStream with the extracted data
                callback_bs = std::make_unique<RakNet::BitStream>(input, bytes_needed, false); // CHANGED: set copyData to false

                // Clean up allocated memory only if we're not using the data
                if (!used_alloca) {
                    // DON'T delete[] input here - the BitStream is using it!
                    // We'll clean it up after the original function call
                }
            }
            else {
                // Create empty BitStream for RPCs with no data
                callback_bs = std::make_unique<RakNet::BitStream>();
            }

            // Call our incoming RPC callbacks
            bool allow_rpc = true;
            try {
                allow_rpc = g_forwarder.IncomingRpc(id, callback_bs.get(), rp);
            }
            catch (...) {
                Log("[te_sdk] Exception in RPC callback for RPC ID %d", id);
                allow_rpc = true; // Allow RPC on callback exception
            }

            if (!allow_rpc) {
                Log("[te_sdk] RPC ID %d blocked by callback", id);
                // Clean up before returning
                if (input && bits_data > 0 && BITS_TO_BYTES(bits_data) >= _MAX_ALLOCA_STACK_ALLOCATION) {
                    delete[] input;
                }
                return false; // Block the RPC
            }

            // CRITICAL FIX: Don't reconstruct data if callbacks didn't modify it
            // Just call the original function with original data
            bool result = oHandleRpcPacket(rp, data, length, playerid);

            // Clean up allocated memory
            if (input && bits_data > 0 && BITS_TO_BYTES(bits_data) >= _MAX_ALLOCA_STACK_ALLOCATION) {
                delete[] input;
            }

            return result;

        }
        catch (const std::exception& e) {
            Log("[te_sdk] Exception in hkHandleRpcPacket: %s", e.what());
            return oHandleRpcPacket(rp, data, length, playerid);
        }
        catch (...) {
            Log("[te_sdk] Unknown exception in hkHandleRpcPacket");
            return oHandleRpcPacket(rp, data, length, playerid);
        }
    }

    bool AttachHandleRpcPacketHook() {
        std::uintptr_t handleRpcPacketAddr = helper::GetHandleRpcPacketAddress();
        if (!handleRpcPacketAddr) {
            Log("[te_sdk] Failed to get handle_rpc_packet address for current SAMP version");
            return false;
        }

        oHandleRpcPacket = reinterpret_cast<tHandleRpcPacket>(handleRpcPacketAddr);

        LONG error = DetourTransactionBegin();
        if (error != NO_ERROR) {
            Log("[te_sdk] DetourTransactionBegin failed with error: %ld", error);
            return false;
        }

        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&reinterpret_cast<PVOID&>(oHandleRpcPacket), &hkHandleRpcPacket);

        error = DetourTransactionCommit();
        if (error != NO_ERROR) {
            Log("[te_sdk] DetourTransactionCommit failed with error: %ld", error);
            DetourTransactionAbort();
            return false;
        }

        Log("[te_sdk] handle_rpc_packet hook attached successfully at 0x%p", reinterpret_cast<void*>(handleRpcPacketAddr));
        return true;
    }

    bool IsSupportedSAMPVersion(helper::SAMPVersion version) {
        return version == helper::SAMPVersion::R1 ||      // 0.3.7-R1
            version == helper::SAMPVersion::R3 ||      // 0.3.7-R3-1 (assuming R3 maps to R3-1)
            version == helper::SAMPVersion::R4 ||      // 0.3.7-R4
            version == helper::SAMPVersion::DL;        // 0.3DL-R1
    }

    bool InitRakNetHooks()
    {
        using namespace te_sdk::helper;

        te_sdk::helper::logging::ResetSession();

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

        // Check if this version supports incoming RPC hooks
        if (!IsSupportedSAMPVersion(version)) {
            Log("[te_sdk] SAMP version %s does not support incoming RPC hooks. Only outgoing hooks will be available.",
                TranslateSAMPVersion(version).c_str());
        }

        auto sampInfo = GetSAMPInfo();
        if (!sampInfo)
        {
            Log("[te_sdk] Failed to get SAMP info");
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

        LocalClient = new TERakClient(*reinterpret_cast<void**>(rak));
        auto* hooked = new HookedRakClientInterface();
        hooked->SetForwarder(&g_forwarder);

        DWORD oldProtect;
        VirtualProtect((void*)rak, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
        *reinterpret_cast<void**>(rak) = hooked;
        VirtualProtect((void*)rak, sizeof(void*), oldProtect, &oldProtect);

        // Only attach incoming RPC hook for supported versions
        bool incomingRpcSupported = false;
        if (IsSupportedSAMPVersion(version)) {
            if (AttachHandleRpcPacketHook()) {
                incomingRpcSupported = true;
                Log("[te_sdk] Incoming RPC hook initialized successfully.");
            }
            else {
                Log("[te_sdk] Failed to initialize incoming RPC hook.");
            }
        }

        Log("[te_sdk] RakNet hooks initialized successfully. Incoming RPC support: %s",
            incomingRpcSupported ? "YES" : "NO");
        return true;
    }
}