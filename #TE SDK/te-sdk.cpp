#include "te-sdk.h"
#include "FullRakNet/PacketEnumerations.h"

#include <mutex>
#include <vector>
#include <memory>
#include <map>

#include <MinHook.h>

namespace te::sdk::forwarder
{
    std::recursive_mutex g_mutex;

    std::vector<te::sdk::RpcCallback> g_outgoingRpcCallbacks;
    std::vector<te::sdk::RpcCallback> g_incomingRpcCallbacks;
    std::vector<te::sdk::PacketCallback> g_outgoingPacketCallbacks;
    std::vector<te::sdk::PacketCallback> g_incomingPacketCallbacks;

    bool OnOutgoingRpc(uint8_t rpcId, void* bitStream, void* rakPeer)
    {
        te::sdk::RpcContext ctx{ rpcId, bitStream, rakPeer };
        std::lock_guard<std::recursive_mutex> lock(g_mutex);

        if (g_outgoingRpcCallbacks.empty()) {
            return true;
        }

        for (auto& cb : g_outgoingRpcCallbacks)
            if (!cb(ctx))
                return false;

        return true;
    }

    bool OnIncomingRpc(uint8_t rpcId, void* bitStream, void* rakPeer)
    {
        te::sdk::RpcContext ctx{ rpcId, bitStream, rakPeer };
        std::lock_guard<std::recursive_mutex> lock(g_mutex);

        if (g_incomingRpcCallbacks.empty()) {
            return true;
        }

        for (auto& cb : g_incomingRpcCallbacks)
            if (!cb(ctx))
                return false;

        return true;
    }

    bool OnOutgoingPacket(uint8_t packetId, void* bitStream, void* rakPeer)
    {
        te::sdk::PacketContext ctx{ packetId, bitStream, rakPeer };
        std::lock_guard<std::recursive_mutex> lock(g_mutex);

        if (g_outgoingPacketCallbacks.empty()) {
            return true;
        }

        for (auto& cb : g_outgoingPacketCallbacks)
            if (!cb(ctx))
                return false;

        return true;
    }

    bool OnIncomingPacket(uint8_t packetId, void* bitStream, void* rakPeer)
    {
        te::sdk::PacketContext ctx{ packetId, bitStream, rakPeer };

        std::lock_guard<std::recursive_mutex> lock(g_mutex);

        if (g_incomingPacketCallbacks.empty()) {
            return true;
        }

        for (auto& cb : g_incomingPacketCallbacks)
            if (!cb(ctx))
                return false;

        return true;
    }

    HookForwarder g_forwarder;
}

namespace te::sdk
{
    using namespace te::sdk::forwarder;
    using namespace te::sdk::helper::logging;

    TERakClient* LocalClient = nullptr;
    tHandleRpcPacket oHandleRpcPacket = nullptr;

    void* g_rakPeer = nullptr;
    PlayerID g_playerId = { 0, 0 };

    static SessionInfo sessionInfo = {
       "",      // serverIP
       0,       // serverPort
       0,       // clientPort
       false,   // isConnected
       0,       // deprecated
       0        // threadSleepTimer
    };

    SessionInfo& GetSessionInfo() {
        return sessionInfo;
    }

    void RegisterRaknetCallback(HookType type, RpcCallback callback)
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);

        switch (type)
        {
        case HookType::OutgoingRpc:    g_outgoingRpcCallbacks.push_back(callback); break;
        case HookType::IncomingRpc:    g_incomingRpcCallbacks.push_back(callback); break;
        default: break;
        }
    }

    void RegisterRaknetCallback(HookType type, PacketCallback callback)
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);

        switch (type)
        {
        case HookType::OutgoingPacket: g_outgoingPacketCallbacks.push_back(callback); break;
        case HookType::IncomingPacket: g_incomingPacketCallbacks.push_back(callback); break;
        default: break;
        }
    }

    // ============ SEND HOOKS (char* version) ============
    typedef bool(__thiscall* pSend_CharPtr)(void*, const char*, int, PacketPriority, PacketReliability, char);
    pSend_CharPtr oSend_CharPtr = nullptr;

    bool  __fastcall hkSend_CharPtr(void* thisPtr, void* edx, const char* data, int length,
        PacketPriority priority, PacketReliability reliability, char orderingChannel)
    {
        if (data && length > 0)
        {
            RakNet::BitStream bs(reinterpret_cast<unsigned char*>(const_cast<char*>(data)), length, false);
            uint8_t packetId = 0;
            bs.Read(packetId);
            bs.ResetReadPointer();

            if (!g_forwarder.OutgoingPacket(packetId, &bs, thisPtr))
                return false;
        }

        return oSend_CharPtr(thisPtr, data, length, priority, reliability, orderingChannel);
    }

    // ============ SEND HOOKS (BitStream* version) ============
    typedef bool(__thiscall* pSend_BitStream)(void*, RakNet::BitStream*, PacketPriority,
        PacketReliability, char);

    pSend_BitStream oSend_BitStream = nullptr;

    bool __fastcall hkSend_BitStream(void* thisPtr, void*, RakNet::BitStream* bitStream,
        PacketPriority priority, PacketReliability reliability, char orderingChannel)
    {
        if (bitStream)
        {
            uint8_t packetId = 0;
            bitStream->Read(packetId);
            bitStream->ResetReadPointer();

            if (!g_forwarder.OutgoingPacket(packetId, bitStream, thisPtr))
                return false;

            bitStream->ResetReadPointer();
        }

        return oSend_BitStream(thisPtr, bitStream, priority, reliability, orderingChannel);
    }

    // ============ RECEIVE HOOK ============
    typedef Packet* (__thiscall* pReceive)(void*);
    pReceive oReceive = nullptr;

    Packet* __fastcall hkReceive(void* thisPtr, void* edx)
    {
        Packet* p = oReceive(thisPtr);

        while (p != nullptr)
        {
            // Guard against malformed packets with null or empty data
            if (!p->data || p->length == 0)
                break;

            uint8_t packetId = p->data[0];

            RakNet::BitStream bs(p->data, p->length, true);

            if (g_forwarder.IncomingPacket(packetId, &bs, thisPtr))
                break;

			LocalClient->GetInterface()->DeallocatePacket(p);
            p = oReceive(thisPtr);
        }

        return p;
    }

    // ============ RPC HOOKS (char* version) ============
    typedef bool(__thiscall* pRPC_CharPtr)(void*, int*, const char*, unsigned int,
        PacketPriority, PacketReliability, char, bool);
    pRPC_CharPtr oRPC_CharPtr = nullptr;

    bool __fastcall hkRPC_CharPtr(void* thisPtr, void* edx, int* uniqueID, const char* data,
        unsigned int bitLength, PacketPriority priority, PacketReliability reliability,
        char orderingChannel, bool shiftTimestamp)
    {
        if (uniqueID && data && bitLength > 0)
        {
            RakNet::BitStream bs(reinterpret_cast<unsigned char*>(const_cast<char*>(data)),
                BITS_TO_BYTES(bitLength), true);

            if (!g_forwarder.OutgoingRpc(static_cast<uint8_t>(*uniqueID), &bs, thisPtr))
                return false;

            bs.ResetReadPointer();
        }

        return oRPC_CharPtr(thisPtr, uniqueID, data, bitLength, priority, reliability,
            orderingChannel, shiftTimestamp);
    }

    // ============ RPC HOOKS (BitStream* version) ============
    typedef bool(__thiscall* pRPC_BitStream)(void*, int*, RakNet::BitStream*,
        PacketPriority, PacketReliability, char, bool);
    pRPC_BitStream oRPC_BitStream = nullptr;

    bool __fastcall hkRPC_BitStream(void* thisPtr, void* edx, int* uniqueID,
        RakNet::BitStream* bitStream, PacketPriority priority, PacketReliability reliability,
        char orderingChannel, bool shiftTimestamp)
    {
        if (uniqueID && bitStream)
        {
            if (!g_forwarder.OutgoingRpc(static_cast<uint8_t>(*uniqueID), bitStream, thisPtr))
                return false;

            bitStream->ResetReadPointer();
        }

        return oRPC_BitStream(thisPtr, uniqueID, bitStream, priority, reliability,
            orderingChannel, shiftTimestamp);
    }

    // ============ RPC_ HOOK ============
    typedef bool(__thiscall* pRPC_Extended)(void*, int*, RakNet::BitStream*,
        PacketPriority, PacketReliability, char, bool, NetworkID);
    pRPC_Extended oRPC_Extended = nullptr;

    bool __fastcall hkRPC_Extended(void* thisPtr, void* edx, int* uniqueID,
        RakNet::BitStream* bitStream, PacketPriority priority, PacketReliability reliability,
        char orderingChannel, bool shiftTimestamp, NetworkID networkID)
    {
        if (uniqueID && bitStream)
        {
            if (!g_forwarder.OutgoingRpc(static_cast<uint8_t>(*uniqueID), bitStream, thisPtr))
                return false;

            bitStream->ResetReadPointer();
        }

        return oRPC_Extended(thisPtr, uniqueID, bitStream, priority, reliability,
            orderingChannel, shiftTimestamp, networkID);
    }

    // ============ CONNECT HOOK ============
    typedef bool(__thiscall* pConnect)(void*, const char*, unsigned short, unsigned short, unsigned int, int);
    pConnect oConnect = nullptr;

    bool __fastcall hkConnect(void* thisPtr, void* edx, const char* host, unsigned short serverPort,
        unsigned short clientPort, unsigned int depreciated, int threadSleepTimer)
    {
        Log("[te::sdk] Connecting to %s:%hu", host, serverPort);

        auto& session = GetSessionInfo();
        strcpy_s(session.serverIP, sizeof(session.serverIP), host);
        session.serverPort = serverPort;
        session.clientPort = clientPort;
        session.deprecated = depreciated;
        session.threadSleepTimer = threadSleepTimer;
        session.isConnected = false;

        bool result = oConnect(thisPtr, host, serverPort, clientPort, depreciated, threadSleepTimer);

        if (result) {
            session.isConnected = true;
            Log("[te::sdk] Connected successfully");
        }
        else {
            Log("[te::sdk] Connection failed");
        }

        return result;
    }

    // ============ DISCONNECT HOOK ============
    typedef void(__thiscall* pDisconnect)(void*, unsigned int, unsigned char);
    pDisconnect oDisconnect = nullptr;

    void __fastcall hkDisconnect(void* thisPtr, void* edx, unsigned int blockDuration, unsigned char orderingChannel)
    {
        Log("[te::sdk] Disconnecting from server");

        auto& session = GetSessionInfo();
        session.isConnected = false;

        oDisconnect(thisPtr, blockDuration, orderingChannel);
    }

    bool __fastcall hkHandleRpcPacket(void* rp, void*, const char* data, int length, PlayerID playerid) {
        // Validate parameters before caching them to avoid storing invalid state
        if (!rp || !data || length <= 0 || !oHandleRpcPacket) {
            if (oHandleRpcPacket) {
                return oHandleRpcPacket(rp, data, length, playerid);
            }
            return false;
        }

        g_rakPeer = rp;
        g_playerId = playerid;

        try {
            RakNet::BitStream incoming(std::bit_cast<unsigned char*>(const_cast<char*>(data)),
                static_cast<unsigned int>(length), true);

            unsigned char id = 0;
            unsigned char* input = nullptr;
            unsigned int bits_data = 0;
            std::shared_ptr<RakNet::BitStream> callback_bs = std::make_shared<RakNet::BitStream>();

            incoming.IgnoreBits(8);

            if (data[0] == ID_TIMESTAMP) {
                incoming.IgnoreBits(8 * (sizeof(RakNetTime) + sizeof(unsigned char)));
            }

            int offset = incoming.GetReadOffset();

            if (!incoming.Read(id)) {
                Log("[te::sdk] Failed to read RPC ID");
                return oHandleRpcPacket(rp, data, length, playerid);
            }

            if (!incoming.ReadCompressed(bits_data)) {
                Log("[te::sdk] Failed to read compressed data size");
                return oHandleRpcPacket(rp, data, length, playerid);
            }

            if (bits_data > 0) {
                unsigned int bytes_needed = BITS_TO_BYTES(incoming.GetNumberOfUnreadBits());
                if (bytes_needed >= MAX_ALLOCA_STACK_ALLOCATION) {
                    input = new unsigned char[bytes_needed];
                }
                else {
                    input = std::bit_cast<unsigned char*>(alloca(bytes_needed));
                }

                if (!incoming.ReadBits(input, bits_data, false)) {
                    Log("[te::sdk] Failed to read RPC data bits");
                    if (bytes_needed >= MAX_ALLOCA_STACK_ALLOCATION) {
                        delete[] input;
                    }
                    return false;
                }

                callback_bs = std::make_shared<RakNet::BitStream>(input, BITS_TO_BYTES(bits_data), true);

                if (bytes_needed >= MAX_ALLOCA_STACK_ALLOCATION) {
                    delete[] input;
                }
            }

            bool allow_rpc = true;
            try {
                allow_rpc = g_forwarder.IncomingRpc(id, callback_bs.get(), rp);
            }
            catch (...) {
                Log("[te::sdk] Exception in RPC callback for RPC ID %d", id);
                allow_rpc = true;
            }

            if (!allow_rpc) {
                Log("[te::sdk] RPC ID %d blocked by callback", id);
                return false;
            }

            incoming.SetWriteOffset(offset);
            incoming.Write(id);

            bits_data = BYTES_TO_BITS(callback_bs->GetNumberOfBytesUsed());
            incoming.WriteCompressed(bits_data);
            if (bits_data > 0) {
                incoming.WriteBits(callback_bs->GetData(), bits_data, false);
            }

            return oHandleRpcPacket(rp, std::bit_cast<char*>(incoming.GetData()),
                incoming.GetNumberOfBytesUsed(), playerid);

        }
        catch (const std::exception& e) {
            Log("[te::sdk] Exception in hkHandleRpcPacket: %s", e.what());
            return oHandleRpcPacket(rp, data, length, playerid);
        }
        catch (...) {
            Log("[te::sdk] Unknown exception in hkHandleRpcPacket");
            return oHandleRpcPacket(rp, data, length, playerid);
        }
    }

    void ReplayIncomingRPC(uint8_t rpcId, const uint8_t* data, int numBytes)
    {
        if (!oHandleRpcPacket || !g_rakPeer)
        {
            Log("[te::sdk] Cannot replay RPC %d: hook not initialized", rpcId);
            return;
        }

        // Construct packet in the same format as hkHandleRpcPacket passes to oHandleRpcPacket:
        // [1 byte header (not timestamp)] [1 byte rpcId] [compressed bit count] [data bits]
        RakNet::BitStream replay;
        replay.Write<uint8_t>(0);  // header byte (not ID_TIMESTAMP)
        replay.Write<uint8_t>(rpcId);
        unsigned int dataBits = static_cast<unsigned int>(numBytes * 8);
        replay.WriteCompressed(dataBits);
        if (dataBits > 0)
        {
            replay.WriteBits(data, dataBits, false);
        }

        // Call original handler directly, bypassing TE hooks
        oHandleRpcPacket(g_rakPeer,
            std::bit_cast<const char*>(replay.GetData()),
            replay.GetNumberOfBytesUsed(),
            g_playerId);

        Log("[te::sdk] Replayed blocked RPC %d (%d bytes)", rpcId, numBytes);
    }

    bool AttachHandleRpcPacketHook() {
        std::uintptr_t handleRpcPacketAddr = helper::GetHandleRpcPacketAddress();
        if (!handleRpcPacketAddr) {
            Log("[te::sdk] Failed to get handle_rpc_packet address for current SAMP version");
            return false;
        }

        if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
            Log("[te::sdk] Failed to initialize MinHook");
            return false;
        }

        MH_STATUS createStatus = MH_CreateHook(
            reinterpret_cast<void*>(handleRpcPacketAddr),
            reinterpret_cast<void*>(&hkHandleRpcPacket),
            reinterpret_cast<void**>(&oHandleRpcPacket)
        );

        if (createStatus != MH_OK) {
            Log("[te::sdk] MH_CreateHook failed with error: %d", createStatus);
            return false;
        }

        MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<void*>(handleRpcPacketAddr));
        if (enableStatus != MH_OK) {
            Log("[te::sdk] MH_EnableHook failed with error: %d", enableStatus);
            return false;
        }

        Log("[te::sdk] handle_rpc_packet hook attached successfully at 0x%p", reinterpret_cast<void*>(handleRpcPacketAddr));
        return true;
    }

    bool IsSupportedSAMPVersion(helper::SAMPVersion version) {
        return version == helper::SAMPVersion::R1 ||
            version == helper::SAMPVersion::R3 ||
            version == helper::SAMPVersion::R4 ||
            version == helper::SAMPVersion::R5 ||
            version == helper::SAMPVersion::DL;
    }

    bool AttachRakNetHooks(void* originalInterface)
    {
        if (!originalInterface) {
            Log("[te::sdk] originalInterface is null");
            return false;
        }

        void** originalVtable = *reinterpret_cast<void***>(originalInterface);
        if (!originalVtable) {
            Log("[te::sdk] originalVtable is null");
            return false;
        }

        // Initialize MinHook
        if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
            Log("[te::sdk] Failed to initialize MinHook");
            return false;
        }

        // Hook Connect - vtable[1]
        void* fnConnect = originalVtable[1];
        if (MH_CreateHook(fnConnect, &hkConnect, reinterpret_cast<void**>(&oConnect)) != MH_OK) {
            Log("[te::sdk] Failed to hook Connect");
            return false;
        }
        MH_EnableHook(fnConnect);

        // Hook Disconnect - vtable[2]
        void* fnDisconnect = originalVtable[2];
        if (MH_CreateHook(fnDisconnect, &hkDisconnect, reinterpret_cast<void**>(&oDisconnect)) != MH_OK) {
            Log("[te::sdk] Failed to hook Disconnect");
            return false;
        }
        MH_EnableHook(fnDisconnect);

        // Hook Send (char* version) - vtable[7]
        void* fnSend_CharPtr = originalVtable[7];
        if (MH_CreateHook(fnSend_CharPtr, &hkSend_CharPtr, reinterpret_cast<void**>(&oSend_CharPtr)) != MH_OK) {
            Log("[te::sdk] Failed to hook Send(char*)");
            return false;
        }
        MH_EnableHook(fnSend_CharPtr);

        // Hook Send (BitStream* version) - vtable[6]
        void* fnSend_BitStream = originalVtable[6];
        if (MH_CreateHook(fnSend_BitStream, &hkSend_BitStream, reinterpret_cast<void**>(&oSend_BitStream)) != MH_OK) {
            Log("[te::sdk] Failed to hook Send(BitStream*)");
            return false;
        }
        MH_EnableHook(fnSend_BitStream);

        // Hook Receive - vtable[8]
        void* fnReceive = originalVtable[8];
        if (MH_CreateHook(fnReceive, &hkReceive, reinterpret_cast<void**>(&oReceive)) != MH_OK) {
            Log("[te::sdk] Failed to hook Receive");
            return false;
        }
        MH_EnableHook(fnReceive);

        // Hook RPC (char* version) - vtable[26]
        void* fnRPC_CharPtr = originalVtable[26];
        if (MH_CreateHook(fnRPC_CharPtr, &hkRPC_CharPtr, reinterpret_cast<void**>(&oRPC_CharPtr)) != MH_OK) {
            Log("[te::sdk] Failed to hook RPC(char*)");
            return false;
        }
        MH_EnableHook(fnRPC_CharPtr);

        // Hook RPC (BitStream* version) - vtable[25]
        void* fnRPC_BitStream = originalVtable[25];
        if (MH_CreateHook(fnRPC_BitStream, &hkRPC_BitStream, reinterpret_cast<void**>(&oRPC_BitStream)) != MH_OK) {
            Log("[te::sdk] Failed to hook RPC(BitStream*)");
            return false;
        }
        MH_EnableHook(fnRPC_BitStream);

        // Hook RPC_ - vtable[27]
        void* fnRPC_Extended = originalVtable[27];
        if (MH_CreateHook(fnRPC_Extended, &hkRPC_Extended, reinterpret_cast<void**>(&oRPC_Extended)) != MH_OK) {
            Log("[te::sdk] Failed to hook RPC_");
            return false;
        }
        MH_EnableHook(fnRPC_Extended);

        Log("[te::sdk] All RakNet function hooks attached successfully");
        return true;
    }

    bool InitRakNetHooks()
    {
        using namespace te::sdk::helper;

        te::sdk::helper::logging::ResetSession();

        if (LocalClient)
        {
            Log("[te::sdk] RakNet hooks already initialized.");
            return false;
        }

        SAMPVersion version = GetSAMPVersion();
        if (version == SAMPVersion::Unknown)
        {
            Log("[te::sdk] Unsupported SAMP version. Hooking aborted.");
            return false;
        }

        if (!IsSupportedSAMPVersion(version)) {
            Log("[te::sdk] SAMP version %s does not support incoming RPC hooks. Only outgoing hooks will be available.",
                TranslateSAMPVersion(version).c_str());
        }

        auto sampInfo = GetSAMPInfo();
        if (!sampInfo)
        {
            Log("[te::sdk] Failed to get SAMP info");
            return false;
        }

        Log("[te::sdk] Detected SAMP version: %s", TranslateSAMPVersion(version).c_str());
        Log("[te::sdk] SAMP info found at %p", sampInfo);

        Log("[te::sdk] Initializing RakNet hooks...");
        void* rakSlot = GetRakNetInterface();
        if (!rakSlot)
        {
            Log("[te::sdk] RakNet interface is not available.");
            return false;
        }

        void* rakClient = *static_cast<void**>(rakSlot);
        if (!rakClient)
        {
            Log("[te::sdk] rakClient is null");
            return false;
        }

        void** vtable = *reinterpret_cast<void***>(rakClient);
        if (!vtable)
        {
            Log("[te::sdk] originalVtable is null");
            return false;
        }

        LocalClient = new TERakClient(rakClient, vtable);

        if (!AttachRakNetHooks(rakClient))
        {
            Log("[te::sdk] Failed to attach RakNet hooks.");
            // Clean up the allocated client to avoid a memory leak
            delete LocalClient;
            LocalClient = nullptr;
            return false;
        }

        bool incomingRpcSupported = false;
        if (IsSupportedSAMPVersion(version)) {
            if (AttachHandleRpcPacketHook()) {
                incomingRpcSupported = true;
                Log("[te::sdk] Incoming RPC hook initialized successfully.");
            }
            else {
                Log("[te::sdk] Failed to initialize incoming RPC hook.");
            }
        }

        Log("[te::sdk] RakNet hooks initialized successfully. Incoming RPC support: %s",
            incomingRpcSupported ? "YES" : "NO");
        return true;
    }
}