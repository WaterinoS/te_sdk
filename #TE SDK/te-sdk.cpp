#include "te-sdk.h"
#include "FullRakNet/PacketEnumerations.h"

#include <MinHook.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace te::sdk
{
    using namespace te::sdk::helper;
    using namespace te::sdk::helper::logging;

    TERakClient* LocalClient = nullptr;

    // ==================================================================
    // Callback registry
    //
    // Callbacks are stored in an immutable vector behind an atomic
    // shared_ptr. Dispatch takes a snapshot and runs the callbacks with no
    // lock held, so a callback is free to call back into the SDK and can
    // never deadlock against another thread registering one. Mutation is
    // copy-on-write under g_registryMutex.
    // ==================================================================

    namespace
    {
        std::mutex g_registryMutex;
        std::atomic<CallbackId> g_nextCallbackId{ 1 };

        template<typename Callback>
        class CallbackList
        {
        public:
            struct Entry
            {
                CallbackId id = kInvalidCallbackId;
                Callback fn;
                void* userData = nullptr;
            };

            using List = std::vector<Entry>;
            using ListPtr = std::shared_ptr<const List>;

            ListPtr Snapshot() const
            {
                return m_list.load(std::memory_order_acquire);
            }

            // Caller must hold g_registryMutex.
            void Add(CallbackId id, Callback fn, void* userData)
            {
                ListPtr current = m_list.load(std::memory_order_acquire);
                auto next = current ? std::make_shared<List>(*current)
                                    : std::make_shared<List>();
                next->push_back(Entry{ id, std::move(fn), userData });
                m_list.store(ListPtr(std::move(next)), std::memory_order_release);
            }

            // Caller must hold g_registryMutex. Moves the removed callback into
            // `out` so it is destroyed by the caller, outside the lock.
            bool Remove(CallbackId id, Callback& out)
            {
                ListPtr current = m_list.load(std::memory_order_acquire);
                if (!current)
                    return false;

                auto next = std::make_shared<List>();
                next->reserve(current->size());

                bool found = false;
                for (const auto& entry : *current)
                {
                    if (!found && entry.id == id)
                    {
                        out = entry.fn;
                        found = true;
                        continue;
                    }
                    next->push_back(entry);
                }

                if (found)
                    m_list.store(ListPtr(std::move(next)), std::memory_order_release);

                return found;
            }

            // Caller must hold g_registryMutex.
            void Clear(List& out)
            {
                ListPtr current = m_list.exchange(nullptr, std::memory_order_acq_rel);
                if (current)
                    out.insert(out.end(), current->begin(), current->end());
            }

        private:
            std::atomic<ListPtr> m_list{ nullptr };
        };

        CallbackList<RpcCallback> g_outgoingRpc;
        CallbackList<RpcCallback> g_incomingRpc;
        CallbackList<PacketCallback> g_outgoingPacket;
        CallbackList<PacketCallback> g_incomingPacket;
        CallbackList<ConnectionCallback> g_onConnect;
        CallbackList<ConnectionCallback> g_onDisconnect;

        // Dispatch helpers. Returns false as soon as a callback vetoes the
        // message; later callbacks are then skipped.

        bool Dispatch(const CallbackList<RpcCallback>::ListPtr& list, RpcContext& ctx)
        {
            if (!list || list->empty())
                return true;

            for (const auto& entry : *list)
            {
                ctx.userData = entry.userData;

                bool allow = true;
                try
                {
                    allow = entry.fn(ctx);
                }
                catch (...)
                {
                    LogError("[te::sdk] Exception in RPC callback %u (rpc id %u) - allowing",
                             entry.id, ctx.rpcId);
                    allow = true;
                }

                if (!allow)
                    return false;
            }

            return true;
        }

        bool Dispatch(const CallbackList<PacketCallback>::ListPtr& list, PacketContext& ctx)
        {
            if (!list || list->empty())
                return true;

            for (const auto& entry : *list)
            {
                ctx.userData = entry.userData;

                bool allow = true;
                try
                {
                    allow = entry.fn(ctx);
                }
                catch (...)
                {
                    LogError("[te::sdk] Exception in packet callback %u (packet id %u) - allowing",
                             entry.id, ctx.packetId);
                    allow = true;
                }

                if (!allow)
                    return false;
            }

            return true;
        }

        void DispatchConnection(const CallbackList<ConnectionCallback>::ListPtr& list,
                                const SessionInfo& info)
        {
            if (!list || list->empty())
                return;

            for (const auto& entry : *list)
            {
                try
                {
                    entry.fn(info);
                }
                catch (...)
                {
                    LogError("[te::sdk] Exception in connection callback %u", entry.id);
                }
            }
        }

        CallbackId NextId()
        {
            return g_nextCallbackId.fetch_add(1, std::memory_order_relaxed);
        }
    }

    CallbackId RegisterRaknetCallback(HookType type, RpcCallback callback, void* userData)
    {
        if (!callback)
            return kInvalidCallbackId;

        const CallbackId id = NextId();

        std::lock_guard<std::mutex> lock(g_registryMutex);
        switch (type)
        {
        case HookType::OutgoingRpc: g_outgoingRpc.Add(id, std::move(callback), userData); return id;
        case HookType::IncomingRpc: g_incomingRpc.Add(id, std::move(callback), userData); return id;
        default:
            LogWarn("[te::sdk] RegisterRaknetCallback: RPC callback given a packet hook type");
            return kInvalidCallbackId;
        }
    }

    CallbackId RegisterRaknetCallback(HookType type, PacketCallback callback, void* userData)
    {
        if (!callback)
            return kInvalidCallbackId;

        const CallbackId id = NextId();

        std::lock_guard<std::mutex> lock(g_registryMutex);
        switch (type)
        {
        case HookType::OutgoingPacket: g_outgoingPacket.Add(id, std::move(callback), userData); return id;
        case HookType::IncomingPacket: g_incomingPacket.Add(id, std::move(callback), userData); return id;
        default:
            LogWarn("[te::sdk] RegisterRaknetCallback: packet callback given an RPC hook type");
            return kInvalidCallbackId;
        }
    }

    CallbackId RegisterConnectCallback(ConnectionCallback callback, void* userData)
    {
        if (!callback)
            return kInvalidCallbackId;

        const CallbackId id = NextId();
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_onConnect.Add(id, std::move(callback), userData);
        return id;
    }

    CallbackId RegisterDisconnectCallback(ConnectionCallback callback, void* userData)
    {
        if (!callback)
            return kInvalidCallbackId;

        const CallbackId id = NextId();
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_onDisconnect.Add(id, std::move(callback), userData);
        return id;
    }

    bool UnregisterRaknetCallback(CallbackId id)
    {
        if (id == kInvalidCallbackId)
            return false;

        // The removed std::function may own objects whose destructor calls back
        // into the SDK, so let it die after the lock is released.
        RpcCallback removedRpc;
        PacketCallback removedPacket;
        ConnectionCallback removedConnection;
        bool found = false;

        {
            std::lock_guard<std::mutex> lock(g_registryMutex);
            found = g_outgoingRpc.Remove(id, removedRpc)
                 || g_incomingRpc.Remove(id, removedRpc)
                 || g_outgoingPacket.Remove(id, removedPacket)
                 || g_incomingPacket.Remove(id, removedPacket)
                 || g_onConnect.Remove(id, removedConnection)
                 || g_onDisconnect.Remove(id, removedConnection);
        }

        return found;
    }

    void ClearRaknetCallbacks()
    {
        CallbackList<RpcCallback>::List rpcRemoved;
        CallbackList<PacketCallback>::List packetRemoved;
        CallbackList<ConnectionCallback>::List connectionRemoved;

        {
            std::lock_guard<std::mutex> lock(g_registryMutex);
            g_outgoingRpc.Clear(rpcRemoved);
            g_incomingRpc.Clear(rpcRemoved);
            g_outgoingPacket.Clear(packetRemoved);
            g_incomingPacket.Clear(packetRemoved);
            g_onConnect.Clear(connectionRemoved);
            g_onDisconnect.Clear(connectionRemoved);
        }

        // Destructors run here, outside the registry lock
    }

    // ==================================================================
    // Session info
    // ==================================================================

    namespace
    {
        std::mutex g_sessionMutex;
        SessionInfo g_session{};
    }

    SessionInfo GetSessionInfo()
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        return g_session;
    }

    const char* GetVersionString()
    {
        return TE_SDK_VERSION_STRING;
    }

    // ==================================================================
    // Hook plumbing
    // ==================================================================

    namespace
    {
        // Payloads bigger than this go on the heap. The old code used alloca()
        // with a 1 MiB ceiling, which risked blowing the game's network-thread
        // stack on a hostile packet.
        constexpr unsigned int kStackPayloadLimit = 4096;

        std::recursive_mutex g_initMutex;
        bool g_initialized = false;
        bool g_incomingRpcHooked = false;
        bool g_minHookOwned = false;
        std::vector<void*> g_installedHooks;

        std::mutex g_rpcPeerMutex;
        void* g_rakPeer = nullptr;
        PlayerID g_playerId = { 0, 0 };

        using tHandleRpcPacket = bool(__thiscall*)(void* rp, const char* data, int length, PlayerID playerid);
        tHandleRpcPacket oHandleRpcPacket = nullptr;

        bool EnsureMinHook()
        {
            const MH_STATUS status = MH_Initialize();
            if (status == MH_OK)
            {
                g_minHookOwned = true;
                return true;
            }
            if (status == MH_ERROR_ALREADY_INITIALIZED)
                return true;

            LogError("[te::sdk] MH_Initialize failed: %d", status);
            return false;
        }

        // Create + enable a hook, recording it so ShutdownRakNetHooks() (and
        // the rollback path below) can take it back out again.
        bool InstallHook(void* target, void* detour, void** original, const char* name)
        {
            if (!target)
            {
                LogError("[te::sdk] Hook target for %s is null", name);
                return false;
            }

            const MH_STATUS createStatus = MH_CreateHook(target, detour, original);
            if (createStatus != MH_OK)
            {
                LogError("[te::sdk] MH_CreateHook(%s) failed: %d", name, createStatus);
                return false;
            }

            const MH_STATUS enableStatus = MH_EnableHook(target);
            if (enableStatus != MH_OK)
            {
                LogError("[te::sdk] MH_EnableHook(%s) failed: %d", name, enableStatus);
                MH_RemoveHook(target);
                return false;
            }

            g_installedHooks.push_back(target);
            return true;
        }

        void RemoveAllHooks()
        {
            for (auto it = g_installedHooks.rbegin(); it != g_installedHooks.rend(); ++it)
            {
                MH_DisableHook(*it);
                MH_RemoveHook(*it);
            }
            g_installedHooks.clear();
        }

        // True when the callback edited the stream we handed it.
        bool StreamModified(const RakNet::BitStream& stream,
                            const unsigned char* original, unsigned int originalBits)
        {
            if (static_cast<unsigned int>(stream.GetNumberOfBitsUsed()) != originalBits)
                return true;

            const unsigned int bytes = BITS_TO_BYTES(originalBits);
            if (bytes == 0)
                return false;

            return memcmp(stream.GetData(), original, bytes) != 0;
        }
    }

    bool IsInitialized()
    {
        std::lock_guard<std::recursive_mutex> lock(g_initMutex);
        return g_initialized;
    }

    bool IsIncomingRpcSupported()
    {
        std::lock_guard<std::recursive_mutex> lock(g_initMutex);
        return g_incomingRpcHooked;
    }

    bool IsSupportedSAMPVersion(helper::SAMPVersion version)
    {
        const VersionProfile* profile = GetVersionProfile(version);
        return profile != nullptr && profile->fnHandleRpcPacket != 0;
    }

    // ============ SEND HOOK (char* version) ============

    namespace
    {
        typedef bool(__thiscall* pSend_CharPtr)(void*, const char*, int, PacketPriority, PacketReliability, char);
        pSend_CharPtr oSend_CharPtr = nullptr;
    }

    bool __fastcall hkSend_CharPtr(void* thisPtr, void*, const char* data, int length,
        PacketPriority priority, PacketReliability reliability, char orderingChannel)
    {
        if (!oSend_CharPtr)
            return false;

        // Skip the copy entirely when nobody registered for this hook
        const auto listeners = g_outgoingPacket.Snapshot();

        if (data && length > 0 && listeners && !listeners->empty())
        {
            // Work on a copy so a callback cannot corrupt the game's buffer,
            // then forward the copy when it actually changed anything. The old
            // code aliased the caller's buffer here but copied in the RPC hook,
            // so "can I edit this?" depended on which overload the game used.
            RakNet::BitStream stream(
                reinterpret_cast<unsigned char*>(const_cast<char*>(data)),
                static_cast<unsigned int>(length), true);

            const unsigned int originalBits = BYTES_TO_BITS(static_cast<unsigned int>(length));

            uint8_t packetId = 0;
            stream.Read(packetId);
            stream.ResetReadPointer();

            PacketContext ctx{};
            ctx.structSize = sizeof(PacketContext);
            ctx.packetId = packetId;
            ctx.bitStream = &stream;
            ctx.rakPeer = thisPtr;
            ctx.length = static_cast<uint32_t>(length);
            ctx.outgoing = true;
            ctx.canModify = true;

            if (!Dispatch(listeners, ctx))
                return false;

            if (StreamModified(stream, reinterpret_cast<const unsigned char*>(data), originalBits))
            {
                return oSend_CharPtr(thisPtr,
                    reinterpret_cast<const char*>(stream.GetData()),
                    stream.GetNumberOfBytesUsed(),
                    priority, reliability, orderingChannel);
            }
        }

        return oSend_CharPtr(thisPtr, data, length, priority, reliability, orderingChannel);
    }

    // ============ SEND HOOK (BitStream* version) ============

    namespace
    {
        typedef bool(__thiscall* pSend_BitStream)(void*, RakNet::BitStream*, PacketPriority, PacketReliability, char);
        pSend_BitStream oSend_BitStream = nullptr;
    }

    bool __fastcall hkSend_BitStream(void* thisPtr, void*, RakNet::BitStream* bitStream,
        PacketPriority priority, PacketReliability reliability, char orderingChannel)
    {
        if (!oSend_BitStream)
            return false;

        if (bitStream)
        {
            uint8_t packetId = 0;
            bitStream->Read(packetId);
            bitStream->ResetReadPointer();

            PacketContext ctx{};
            ctx.structSize = sizeof(PacketContext);
            ctx.packetId = packetId;
            ctx.bitStream = bitStream;
            ctx.rakPeer = thisPtr;
            ctx.length = static_cast<uint32_t>(bitStream->GetNumberOfBytesUsed());
            ctx.outgoing = true;
            ctx.canModify = true;

            if (!Dispatch(g_outgoingPacket.Snapshot(), ctx))
                return false;

            bitStream->ResetReadPointer();
        }

        return oSend_BitStream(thisPtr, bitStream, priority, reliability, orderingChannel);
    }

    // ============ RECEIVE HOOK ============

    namespace
    {
        typedef Packet* (__thiscall* pReceive)(void*);
        pReceive oReceive = nullptr;

        // Copy a modified packet back over RakNet's buffer. The buffer belongs
        // to RakNet, so the payload may shrink but never grow.
        void ApplyPacketEdits(Packet* packet, const RakNet::BitStream& stream)
        {
            const unsigned int newLength = static_cast<unsigned int>(stream.GetNumberOfBytesUsed());

            if (newLength == packet->length &&
                memcmp(stream.GetData(), packet->data, newLength) == 0)
            {
                return; // untouched
            }

            if (newLength > packet->length)
            {
                LogWarn("[te::sdk] Incoming packet %u grew from %u to %u bytes - "
                        "cannot resize RakNet's buffer, forwarding the original",
                        packet->data[0], packet->length, newLength);
                return;
            }

            if (!IsWritable(packet->data, newLength))
            {
                LogWarn("[te::sdk] Incoming packet %u buffer is not writable - "
                        "edits discarded", packet->data[0]);
                return;
            }

            memcpy(packet->data, stream.GetData(), newLength);
            packet->length = newLength;
            packet->bitSize = static_cast<unsigned int>(stream.GetNumberOfBitsUsed());
        }

        void DeallocatePacketSafe(Packet* packet)
        {
            if (LocalClient && LocalClient->GetInterface())
                LocalClient->GetInterface()->DeallocatePacket(packet);
        }
    }

    Packet* __fastcall hkReceive(void* thisPtr, void*)
    {
        if (!oReceive)
            return nullptr;

        Packet* p = oReceive(thisPtr);

        // No listeners: hand the packet straight back, no copy, no memcmp
        const auto listeners = g_incomingPacket.Snapshot();
        if (!listeners || listeners->empty())
            return p;

        while (p != nullptr)
        {
            // Guard against malformed packets with null or empty data
            if (!IsReadable(p, sizeof(Packet)))
                break;

            if (!p->data || p->length == 0 || !IsReadable(p->data, p->length))
                break;

            RakNet::BitStream stream(p->data, p->length, true);

            PacketContext ctx{};
            ctx.structSize = sizeof(PacketContext);
            ctx.packetId = p->data[0];
            ctx.bitStream = &stream;
            ctx.rakPeer = thisPtr;
            ctx.length = p->length;
            ctx.outgoing = false;
            ctx.canModify = true;

            if (Dispatch(listeners, ctx))
            {
                ApplyPacketEdits(p, stream);
                break;
            }

            DeallocatePacketSafe(p);
            p = oReceive(thisPtr);
        }

        return p;
    }

    // ============ RPC HOOK (char* version) ============

    namespace
    {
        typedef bool(__thiscall* pRPC_CharPtr)(void*, int*, const char*, unsigned int,
            PacketPriority, PacketReliability, char, bool);
        pRPC_CharPtr oRPC_CharPtr = nullptr;
    }

    bool __fastcall hkRPC_CharPtr(void* thisPtr, void*, int* uniqueID, const char* data,
        unsigned int bitLength, PacketPriority priority, PacketReliability reliability,
        char orderingChannel, bool shiftTimestamp)
    {
        if (!oRPC_CharPtr)
            return false;

        const auto listeners = g_outgoingRpc.Snapshot();

        if (uniqueID && data && bitLength > 0 && listeners && !listeners->empty())
        {
            const unsigned int byteLength = BITS_TO_BYTES(bitLength);

            RakNet::BitStream stream(
                reinterpret_cast<unsigned char*>(const_cast<char*>(data)),
                byteLength, true);

            const unsigned int copiedBits = BYTES_TO_BITS(byteLength);

            RpcContext ctx{};
            ctx.structSize = sizeof(RpcContext);
            ctx.rpcId = static_cast<uint8_t>(*uniqueID);
            ctx.bitStream = &stream;
            ctx.rakPeer = thisPtr;
            ctx.bitLength = bitLength;
            ctx.outgoing = true;
            ctx.canModify = true;

            if (!Dispatch(listeners, ctx))
                return false;

            if (StreamModified(stream, reinterpret_cast<const unsigned char*>(data), copiedBits))
            {
                return oRPC_CharPtr(thisPtr, uniqueID,
                    reinterpret_cast<const char*>(stream.GetData()),
                    static_cast<unsigned int>(stream.GetNumberOfBitsUsed()),
                    priority, reliability, orderingChannel, shiftTimestamp);
            }
        }

        // Unmodified: forward the caller's buffer verbatim so a payload whose
        // bit length is not a whole number of bytes keeps its exact length.
        return oRPC_CharPtr(thisPtr, uniqueID, data, bitLength, priority, reliability,
            orderingChannel, shiftTimestamp);
    }

    // ============ RPC HOOK (BitStream* version) ============

    namespace
    {
        typedef bool(__thiscall* pRPC_BitStream)(void*, int*, RakNet::BitStream*,
            PacketPriority, PacketReliability, char, bool);
        pRPC_BitStream oRPC_BitStream = nullptr;
    }

    bool __fastcall hkRPC_BitStream(void* thisPtr, void*, int* uniqueID,
        RakNet::BitStream* bitStream, PacketPriority priority, PacketReliability reliability,
        char orderingChannel, bool shiftTimestamp)
    {
        if (!oRPC_BitStream)
            return false;

        if (uniqueID && bitStream)
        {
            RpcContext ctx{};
            ctx.structSize = sizeof(RpcContext);
            ctx.rpcId = static_cast<uint8_t>(*uniqueID);
            ctx.bitStream = bitStream;
            ctx.rakPeer = thisPtr;
            ctx.bitLength = static_cast<uint32_t>(bitStream->GetNumberOfBitsUsed());
            ctx.outgoing = true;
            ctx.canModify = true;

            if (!Dispatch(g_outgoingRpc.Snapshot(), ctx))
                return false;

            bitStream->ResetReadPointer();
        }

        return oRPC_BitStream(thisPtr, uniqueID, bitStream, priority, reliability,
            orderingChannel, shiftTimestamp);
    }

    // ============ RPC_ HOOK ============

    namespace
    {
        typedef bool(__thiscall* pRPC_Extended)(void*, int*, RakNet::BitStream*,
            PacketPriority, PacketReliability, char, bool, NetworkID);
        pRPC_Extended oRPC_Extended = nullptr;
    }

    bool __fastcall hkRPC_Extended(void* thisPtr, void*, int* uniqueID,
        RakNet::BitStream* bitStream, PacketPriority priority, PacketReliability reliability,
        char orderingChannel, bool shiftTimestamp, NetworkID networkID)
    {
        if (!oRPC_Extended)
            return false;

        if (uniqueID && bitStream)
        {
            RpcContext ctx{};
            ctx.structSize = sizeof(RpcContext);
            ctx.rpcId = static_cast<uint8_t>(*uniqueID);
            ctx.bitStream = bitStream;
            ctx.rakPeer = thisPtr;
            ctx.bitLength = static_cast<uint32_t>(bitStream->GetNumberOfBitsUsed());
            ctx.outgoing = true;
            ctx.canModify = true;

            if (!Dispatch(g_outgoingRpc.Snapshot(), ctx))
                return false;

            bitStream->ResetReadPointer();
        }

        return oRPC_Extended(thisPtr, uniqueID, bitStream, priority, reliability,
            orderingChannel, shiftTimestamp, networkID);
    }

    // ============ CONNECT / DISCONNECT HOOKS ============

    namespace
    {
        typedef bool(__thiscall* pConnect)(void*, const char*, unsigned short, unsigned short, unsigned int, int);
        pConnect oConnect = nullptr;

        typedef void(__thiscall* pDisconnect)(void*, unsigned int, unsigned char);
        pDisconnect oDisconnect = nullptr;
    }

    bool __fastcall hkConnect(void* thisPtr, void*, const char* host, unsigned short serverPort,
        unsigned short clientPort, unsigned int depreciated, int threadSleepTimer)
    {
        if (!oConnect)
            return false;

        Log("[te::sdk] Connecting to %s:%hu", host ? host : "(null)", serverPort);

        {
            std::lock_guard<std::mutex> lock(g_sessionMutex);
            // strcpy_s aborts the process on overflow; a hostname longer than
            // 63 characters must truncate, not take the game down.
            if (host)
                strncpy_s(g_session.serverIP, sizeof(g_session.serverIP), host, _TRUNCATE);
            else
                g_session.serverIP[0] = '\0';

            g_session.serverPort = serverPort;
            g_session.clientPort = clientPort;
            g_session.deprecated = depreciated;
            g_session.threadSleepTimer = threadSleepTimer;
            g_session.isConnected = false;
        }

        const bool result = oConnect(thisPtr, host, serverPort, clientPort, depreciated, threadSleepTimer);

        SessionInfo snapshot;
        {
            std::lock_guard<std::mutex> lock(g_sessionMutex);
            g_session.isConnected = result;
            snapshot = g_session;
        }

        Log(result ? "[te::sdk] Connected successfully" : "[te::sdk] Connection failed");

        if (result)
            DispatchConnection(g_onConnect.Snapshot(), snapshot);

        return result;
    }

    void __fastcall hkDisconnect(void* thisPtr, void*, unsigned int blockDuration, unsigned char orderingChannel)
    {
        Log("[te::sdk] Disconnecting from server");

        SessionInfo snapshot;
        {
            std::lock_guard<std::mutex> lock(g_sessionMutex);
            g_session.isConnected = false;
            snapshot = g_session;
        }

        DispatchConnection(g_onDisconnect.Snapshot(), snapshot);

        if (oDisconnect)
            oDisconnect(thisPtr, blockDuration, orderingChannel);
    }

    // ============ INCOMING RPC HOOK ============

    bool __fastcall hkHandleRpcPacket(void* rp, void*, const char* data, int length, PlayerID playerid)
    {
        if (!oHandleRpcPacket)
            return false;

        // Validate parameters before caching them to avoid storing invalid state
        if (!rp || !data || length <= 0)
            return oHandleRpcPacket(rp, data, length, playerid);

        {
            std::lock_guard<std::mutex> lock(g_rpcPeerMutex);
            g_rakPeer = rp;
            g_playerId = playerid;
        }

        // Parsing and copying the payload is pure overhead with no listeners
        const auto listeners = g_incomingRpc.Snapshot();
        if (!listeners || listeners->empty())
            return oHandleRpcPacket(rp, data, length, playerid);

        try
        {
            RakNet::BitStream incoming(
                reinterpret_cast<unsigned char*>(const_cast<char*>(data)),
                static_cast<unsigned int>(length), true);

            incoming.IgnoreBits(8);

            if (static_cast<unsigned char>(data[0]) == ID_TIMESTAMP)
                incoming.IgnoreBits(8 * (sizeof(RakNetTime) + sizeof(unsigned char)));

            const int offset = incoming.GetReadOffset();

            unsigned char id = 0;
            if (!incoming.Read(id))
            {
                LogWarn("[te::sdk] Failed to read RPC id - forwarding untouched");
                return oHandleRpcPacket(rp, data, length, playerid);
            }

            unsigned int bitsData = 0;
            if (!incoming.ReadCompressed(bitsData))
            {
                LogWarn("[te::sdk] Failed to read RPC payload size - forwarding untouched");
                return oHandleRpcPacket(rp, data, length, playerid);
            }

            // Payload storage: small payloads stay on the stack, big ones go on
            // the heap. `payload` keeps the pristine bytes so we can tell
            // afterwards whether a callback actually changed anything.
            unsigned char stackPayload[kStackPayloadLimit];
            std::vector<unsigned char> heapPayload;
            unsigned char* payload = stackPayload;

            if (bitsData > 0)
            {
                const unsigned int payloadBytes = BITS_TO_BYTES(
                    static_cast<unsigned int>(incoming.GetNumberOfUnreadBits()));

                if (payloadBytes > kStackPayloadLimit)
                {
                    heapPayload.resize(payloadBytes);
                    payload = heapPayload.data();
                }

                if (!incoming.ReadBits(payload, static_cast<int>(bitsData), false))
                {
                    // Malformed: hand the original bytes to the game rather
                    // than silently swallowing the RPC.
                    LogWarn("[te::sdk] Failed to read RPC %u payload bits - forwarding untouched", id);
                    return oHandleRpcPacket(rp, data, length, playerid);
                }
            }

            // BitStream declares neither a copy constructor nor an assignment
            // operator, and it may point `data` at its own inline stackData
            // buffer - so it must be constructed in place, never assigned.
            std::optional<RakNet::BitStream> streamHolder;
            if (bitsData > 0)
                streamHolder.emplace(payload, BITS_TO_BYTES(bitsData), true);
            else
                streamHolder.emplace();

            RakNet::BitStream& callbackStream = *streamHolder;

            const unsigned int copiedBits = BYTES_TO_BITS(BITS_TO_BYTES(bitsData));

            RpcContext ctx{};
            ctx.structSize = sizeof(RpcContext);
            ctx.rpcId = id;
            ctx.bitStream = &callbackStream;
            ctx.rakPeer = rp;
            ctx.bitLength = bitsData;
            ctx.outgoing = false;
            ctx.canModify = true;

            if (!Dispatch(listeners, ctx))
            {
                LogDebug("[te::sdk] RPC %u blocked by callback", id);
                return false;
            }

            // Nothing changed: forward the original datagram. Re-encoding would
            // round the payload up to whole bytes and change the RPC's bit
            // length, so leaving it alone is both faster and more faithful.
            if (!StreamModified(callbackStream, payload, copiedBits))
                return oHandleRpcPacket(rp, data, length, playerid);

            incoming.SetWriteOffset(offset);
            incoming.Write(id);

            const unsigned int newBits =
                BYTES_TO_BITS(static_cast<unsigned int>(callbackStream.GetNumberOfBytesUsed()));
            incoming.WriteCompressed(newBits);
            if (newBits > 0)
                incoming.WriteBits(callbackStream.GetData(), static_cast<int>(newBits), false);

            return oHandleRpcPacket(rp,
                reinterpret_cast<char*>(incoming.GetData()),
                incoming.GetNumberOfBytesUsed(),
                playerid);
        }
        catch (const std::exception& e)
        {
            LogError("[te::sdk] Exception in hkHandleRpcPacket: %s", e.what());
            return oHandleRpcPacket(rp, data, length, playerid);
        }
        catch (...)
        {
            LogError("[te::sdk] Unknown exception in hkHandleRpcPacket");
            return oHandleRpcPacket(rp, data, length, playerid);
        }
    }

    bool ReplayIncomingRPC(uint8_t rpcId, const uint8_t* data, int numBytes)
    {
        if (numBytes < 0 || (numBytes > 0 && !data))
        {
            LogWarn("[te::sdk] ReplayIncomingRPC: invalid buffer");
            return false;
        }

        void* peer = nullptr;
        PlayerID playerId{};
        {
            std::lock_guard<std::mutex> lock(g_rpcPeerMutex);
            peer = g_rakPeer;
            playerId = g_playerId;
        }

        if (!oHandleRpcPacket || !peer)
        {
            LogWarn("[te::sdk] Cannot replay RPC %u: no incoming RPC seen yet", rpcId);
            return false;
        }

        // Same layout hkHandleRpcPacket hands to the original handler:
        // [packet id][rpc id][compressed bit count][payload bits]
        RakNet::BitStream replay;
        replay.Write<uint8_t>(ID_RPC);
        replay.Write<uint8_t>(rpcId);

        const unsigned int dataBits = static_cast<unsigned int>(numBytes) * 8u;
        replay.WriteCompressed(dataBits);
        if (dataBits > 0)
            replay.WriteBits(data, static_cast<int>(dataBits), false);

        // Call the original handler directly, bypassing TE hooks
        const bool result = oHandleRpcPacket(peer,
            reinterpret_cast<const char*>(replay.GetData()),
            replay.GetNumberOfBytesUsed(),
            playerId);

        LogDebug("[te::sdk] Replayed RPC %u (%d bytes) -> %d", rpcId, numBytes, result ? 1 : 0);
        return result;
    }

    // ==================================================================
    // Initialisation / shutdown
    // ==================================================================

    namespace
    {
        bool AttachHandleRpcPacketHook()
        {
            const std::uintptr_t address = GetHandleRpcPacketAddress();
            if (!address)
            {
                LogWarn("[te::sdk] No handle_rpc_packet offset for the detected SA-MP version");
                return false;
            }

            if (!EnsureMinHook())
                return false;

            if (!InstallHook(reinterpret_cast<void*>(address),
                             reinterpret_cast<void*>(&hkHandleRpcPacket),
                             reinterpret_cast<void**>(&oHandleRpcPacket), "handle_rpc_packet"))
            {
                return false;
            }

            Log("[te::sdk] handle_rpc_packet hook attached at 0x%p", reinterpret_cast<void*>(address));
            return true;
        }

        struct VtableHook
        {
            size_t index;
            void* detour;
            void** original;
            const char* name;
        };

        bool AttachRakNetHooks(void* rakClient)
        {
            if (!rakClient)
            {
                LogError("[te::sdk] rakClient is null");
                return false;
            }

            void** vtable = *reinterpret_cast<void***>(rakClient);
            if (!vtable)
            {
                LogError("[te::sdk] rakClient vtable is null");
                return false;
            }

            if (!EnsureMinHook())
                return false;

            // A function pointer only converts to void* via an explicit cast;
            // relying on the MSVC extension breaks under /permissive-.
            const VtableHook hooks[] = {
                {  1, reinterpret_cast<void*>(&hkConnect),        reinterpret_cast<void**>(&oConnect),        "Connect"          },
                {  2, reinterpret_cast<void*>(&hkDisconnect),     reinterpret_cast<void**>(&oDisconnect),     "Disconnect"       },
                {  6, reinterpret_cast<void*>(&hkSend_BitStream), reinterpret_cast<void**>(&oSend_BitStream), "Send(BitStream*)" },
                {  7, reinterpret_cast<void*>(&hkSend_CharPtr),   reinterpret_cast<void**>(&oSend_CharPtr),   "Send(char*)"      },
                {  8, reinterpret_cast<void*>(&hkReceive),        reinterpret_cast<void**>(&oReceive),        "Receive"          },
                { 25, reinterpret_cast<void*>(&hkRPC_BitStream),  reinterpret_cast<void**>(&oRPC_BitStream),  "RPC(BitStream*)"  },
                { 26, reinterpret_cast<void*>(&hkRPC_CharPtr),    reinterpret_cast<void**>(&oRPC_CharPtr),    "RPC(char*)"       },
                { 27, reinterpret_cast<void*>(&hkRPC_Extended),   reinterpret_cast<void**>(&oRPC_Extended),   "RPC_"             },
            };

            for (const auto& hook : hooks)
            {
                if (!IsReadable(&vtable[hook.index], sizeof(void*)))
                {
                    LogError("[te::sdk] vtable slot %zu (%s) is not readable", hook.index, hook.name);
                    return false;
                }

                if (!InstallHook(vtable[hook.index], hook.detour, hook.original, hook.name))
                    return false;   // caller rolls the earlier hooks back
            }

            Log("[te::sdk] All RakNet function hooks attached successfully");
            return true;
        }

        void ResetTrampolines()
        {
            oConnect = nullptr;
            oDisconnect = nullptr;
            oSend_BitStream = nullptr;
            oSend_CharPtr = nullptr;
            oReceive = nullptr;
            oRPC_BitStream = nullptr;
            oRPC_CharPtr = nullptr;
            oRPC_Extended = nullptr;
            oHandleRpcPacket = nullptr;
        }
    }

    InitResult InitRakNetHooksEx()
    {
        std::lock_guard<std::recursive_mutex> lock(g_initMutex);

        if (g_initialized)
            return InitResult::AlreadyInitialized;

        // Mark the log session once, not on every retry - the documented
        // "loop until it succeeds" pattern used to write a SESSION START
        // header on each failed attempt.
        static std::once_flag sessionOnce;
        std::call_once(sessionOnce, [] { logging::ResetSession(); });

        if (!GetSAMPBase())
            return InitResult::SampNotLoaded;

        const SAMPVersion version = GetSAMPVersion();
        if (version == SAMPVersion::Unknown || !GetVersionProfile())
        {
            LogWarn("[te::sdk] Unrecognised SA-MP build (signature 0x%08X)", GetSAMPVersionSignature());
            return InitResult::UnsupportedVersion;
        }

        void* sampInfo = GetSAMPInfo();
        if (!sampInfo)
            return InitResult::SampNotLoaded;

        Log("[te::sdk] TE SDK %s starting", TE_SDK_VERSION_STRING);
        Log("[te::sdk] Detected SA-MP version: %s", TranslateSAMPVersion(version).c_str());
        Log("[te::sdk] SAMP info found at %p", sampInfo);

        void* rakSlot = GetRakNetInterface();
        if (!rakSlot)
        {
            LogError("[te::sdk] RakNet interface slot is not available");
            return InitResult::RakNetUnavailable;
        }

        void* rakClient = *static_cast<void**>(rakSlot);
        if (!rakClient || !IsReadable(rakClient, sizeof(void*)))
        {
            LogWarn("[te::sdk] RakClient is not constructed yet");
            return InitResult::RakNetUnavailable;
        }

        void** vtable = *reinterpret_cast<void***>(rakClient);
        if (!vtable)
        {
            LogError("[te::sdk] RakClient vtable is null");
            return InitResult::RakNetUnavailable;
        }

        LocalClient = new TERakClient(rakClient, vtable);

        if (!AttachRakNetHooks(rakClient))
        {
            LogError("[te::sdk] Failed to attach RakNet hooks - rolling back");
            RemoveAllHooks();
            ResetTrampolines();
            delete LocalClient;
            LocalClient = nullptr;
            return InitResult::HookFailed;
        }

        g_incomingRpcHooked = IsSupportedSAMPVersion(version) && AttachHandleRpcPacketHook();
        if (!g_incomingRpcHooked)
        {
            LogWarn("[te::sdk] Incoming RPC hook unavailable for %s - "
                    "outgoing hooks are still active",
                    TranslateSAMPVersion(version).c_str());
        }

        g_initialized = true;
        Log("[te::sdk] RakNet hooks initialised. Incoming RPC support: %s",
            g_incomingRpcHooked ? "YES" : "NO");

        return InitResult::Ok;
    }

    bool InitRakNetHooks()
    {
        const InitResult result = InitRakNetHooksEx();
        // AlreadyInitialized counts as success so a retry loop terminates
        return result == InitResult::Ok || result == InitResult::AlreadyInitialized;
    }

    void ShutdownRakNetHooks()
    {
        std::lock_guard<std::recursive_mutex> lock(g_initMutex);

        // Chat command hook first - it owns its own MinHook entry
        helper::samp::ClearChatCommands();

        RemoveAllHooks();
        ResetTrampolines();

        ClearRaknetCallbacks();

        delete LocalClient;
        LocalClient = nullptr;

        {
            std::lock_guard<std::mutex> peerLock(g_rpcPeerMutex);
            g_rakPeer = nullptr;
            g_playerId = PlayerID{ 0, 0 };
        }

        {
            std::lock_guard<std::mutex> sessionLock(g_sessionMutex);
            g_session = SessionInfo{};
        }

        if (g_minHookOwned)
        {
            MH_Uninitialize();
            g_minHookOwned = false;
        }

        const bool wasInitialized = g_initialized;
        g_initialized = false;
        g_incomingRpcHooked = false;

        if (wasInitialized)
            Log("[te::sdk] RakNet hooks removed");

        logging::Flush();
    }
}
