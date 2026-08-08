#pragma once

#include <cstdint>
#include "FullRakNet/BitStream.h"
#include "FullRakNet/PacketPriority.h"
#include "FullRakNet/RakNetStatistics.h"

class local_RakClientInterface
{
public:
    virtual ~local_RakClientInterface() = default;

    virtual bool Connect(const char*, unsigned short, unsigned short, unsigned int, int) = 0;
    virtual void Disconnect(unsigned int, unsigned char = 0) = 0;
    virtual void InitializeSecurity(const char*, const char*) = 0;
    virtual void SetPassword(const char*) = 0;
    virtual bool HasPassword(void) const = 0;
    virtual bool Send(const char*, int, PacketPriority, PacketReliability, char) = 0;
    virtual bool Send(RakNet::BitStream*, PacketPriority, PacketReliability, char) = 0;
    virtual Packet* Receive(void) = 0;
    virtual void DeallocatePacket(Packet*) = 0;
    virtual void PingServer(void) = 0;
    virtual void PingServer(const char*, unsigned short, unsigned short, bool) = 0;
    virtual int GetAveragePing(void) = 0;
    virtual int GetLastPing(void) const = 0;
    virtual int GetLowestPing(void) const = 0;
    virtual int GetPlayerPing(PlayerID) = 0;
    virtual void StartOccasionalPing(void) = 0;
    virtual void StopOccasionalPing(void) = 0;
    virtual bool IsConnected(void) const = 0;
    virtual unsigned int GetSynchronizedRandomInteger(void) const = 0;
    virtual bool GenerateCompressionLayer(unsigned int[256], bool) = 0;
    virtual bool DeleteCompressionLayer(bool) = 0;
    virtual void RegisterAsRemoteProcedureCall(int*, void (*)(RPCParameters*)) = 0;
    virtual void RegisterClassMemberRPC(int*, void*) = 0;
    virtual void UnregisterAsRemoteProcedureCall(int*) = 0;
    virtual bool RPC(int*, const char*, unsigned int, PacketPriority, PacketReliability, char, bool) = 0;
    virtual bool RPC(int*, RakNet::BitStream*, PacketPriority, PacketReliability, char, bool) = 0;
    virtual bool RPC_(int*, RakNet::BitStream*, PacketPriority, PacketReliability, char, bool, NetworkID) = 0;
    virtual void SetTrackFrequencyTable(bool) = 0;
    virtual bool GetSendFrequencyTable(unsigned int[256]) = 0;
    virtual float GetCompressionRatio(void) const = 0;
    virtual float GetDecompressionRatio(void) const = 0;
    virtual void AttachPlugin(void*) = 0;
    virtual void DetachPlugin(void*) = 0;
    virtual RakNet::BitStream* GetStaticServerData(void) = 0;
    virtual void SetStaticServerData(const char*, int) = 0;
    virtual RakNet::BitStream* GetStaticClientData(PlayerID) = 0;
    virtual void SetStaticClientData(PlayerID, const char*, int) = 0;
    virtual void SendStaticClientDataToServer(void) = 0;
    virtual PlayerID GetServerID(void) const = 0;
    virtual PlayerID GetPlayerID(void) const = 0;
    virtual PlayerID GetInternalID(void) const = 0;
    virtual const char* PlayerIDToDottedIP(PlayerID) const = 0;
    virtual void PushBackPacket(Packet*, bool) = 0;
    virtual void SetRouterInterface(void*) = 0;
    virtual void RemoveRouterInterface(void*) = 0;
    virtual void SetTimeoutTime(RakNetTime) = 0;
    virtual bool SetMTUSize(int) = 0;
    virtual int GetMTUSize(void) const = 0;
    virtual void AllowConnectionResponseIPMigration(bool) = 0;
    virtual void AdvertiseSystem(const char*, unsigned short, const char*, int) = 0;
    virtual RakNetStatisticsStruct* const GetStatistics(void) = 0;
    virtual void ApplyNetworkSimulator(double, unsigned short, unsigned short) = 0;
    virtual bool IsNetworkSimulatorActive(void) = 0;
    virtual PlayerIndex GetPlayerIndex(void) = 0;
};

class TERakClient
{
public:
    TERakClient(void* rawInterface, void** originalVtable);

    // False once the SDK has been shut down, or if construction was handed a
    // null interface/vtable. Every Send*/RPC path checks this first.
    bool IsValid() const;

    void* GetRawInterface() { return raw; }
    void** GetVTable() { return originalVtable; }

    // Returns the original vtable entry, or nullptr when the index is out of
    // range or the slot is not readable. Callers MUST null-check.
    void* GetOriginalRaw(size_t index);

    template<typename Fn>
    Fn GetOriginal(size_t index)
    {
        return reinterpret_cast<Fn>(GetOriginalRaw(index));
    }

    local_RakClientInterface* GetInterface()
    {
        return &wrapper;
    }

    bool SendRPC(int rpcId, RakNet::BitStream* bitStream, PacketPriority priority,
        PacketReliability reliability, char orderingChannel, bool shiftTimestamp);

    bool SendPacket(RakNet::BitStream* bitStream, PacketPriority priority,
        PacketReliability reliability, char orderingChannel);

    bool SendRPC(int rpcId, RakNet::BitStream* bitStream)
    {
        return SendRPC(rpcId, bitStream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
    }

    bool SendRPC(int rpcId, RakNet::BitStream* bitStream, PacketPriority priority, PacketReliability reliability)
    {
        return SendRPC(rpcId, bitStream, priority, reliability, 0, false);
    }

    bool SendRPC(int rpcId, RakNet::BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel)
    {
        return SendRPC(rpcId, bitStream, priority, reliability, orderingChannel, false);
    }

    bool SendPacket(RakNet::BitStream* bitStream)
    {
        return SendPacket(bitStream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
    }

    bool SendPacket(RakNet::BitStream* bitStream, PacketPriority priority, PacketReliability reliability)
    {
        return SendPacket(bitStream, priority, reliability, 0);
    }
private:
    void* raw;
    void** originalVtable;

    class Wrapper : public local_RakClientInterface
    {
    public:
        Wrapper(TERakClient* owner) : owner(owner) {}

        bool Connect(const char* host,
            unsigned short serverPort,
            unsigned short clientPort,
            unsigned int depreciated,
            int threadSleepTimer) override
        {
            using Fn = bool(__thiscall*)(void*, const char*, unsigned short, unsigned short, unsigned int, int);
            Fn fn = owner->GetOriginal<Fn>(1);
            if (!fn) return false;
            return fn(owner->raw, host, serverPort, clientPort, depreciated, threadSleepTimer);
        }

        void Disconnect(unsigned int blockDuration, unsigned char orderingChannel = 0) override
        {
            using Fn = void(__thiscall*)(void*, unsigned int, unsigned char);
            Fn fn = owner->GetOriginal<Fn>(2);
            if (!fn) return;
            fn(owner->raw, blockDuration, orderingChannel);
        }

        void InitializeSecurity(const char* privKeyP, const char* privKeyQ) override
        {
            using Fn = void(__thiscall*)(void*, const char*, const char*);
            Fn fn = owner->GetOriginal<Fn>(3);
            if (!fn) return;
            fn(owner->raw, privKeyP, privKeyQ);
        }

        void SetPassword(const char* password) override
        {
            using Fn = void(__thiscall*)(void*, const char*);
            Fn fn = owner->GetOriginal<Fn>(4);
            if (!fn) return;
            fn(owner->raw, password);
        }

        bool HasPassword(void) const override
        {
            using Fn = bool(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(5);
            if (!fn) return false;
            return fn(owner->raw);
        }

        bool Send(const char* data, int length, PacketPriority priority, PacketReliability reliability, char orderingChannel) override
        {
            using Fn = bool(__thiscall*)(void*, const char*, int, PacketPriority, PacketReliability, char);
            Fn fn = owner->GetOriginal<Fn>(7);
            if (!fn) return false;
            return fn(owner->raw, data, length, priority, reliability, orderingChannel);
        }

        bool Send(RakNet::BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel) override
        {
            using Fn = bool(__thiscall*)(void*, RakNet::BitStream*, PacketPriority, PacketReliability, char);
            Fn fn = owner->GetOriginal<Fn>(6);
            if (!fn) return false;
            return fn(owner->raw, bitStream, priority, reliability, orderingChannel);
        }

        Packet* Receive(void) override
        {
            using Fn = Packet * (__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(8);
            if (!fn) return nullptr;
            return fn(owner->raw);
        }

        void DeallocatePacket(Packet* packet) override
        {
            using Fn = void(__thiscall*)(void*, Packet*);
            Fn fn = owner->GetOriginal<Fn>(9);
            if (!fn) return;
            fn(owner->raw, packet);
        }

        void PingServer(void) override
        {
            using Fn = void(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(10);
            if (!fn) return;
            fn(owner->raw);
        }

        void PingServer(const char* host, unsigned short remotePort, unsigned short clientPort, bool onlyReplyOnAcceptingConnections) override
        {
            using Fn = void(__thiscall*)(void*, const char*, unsigned short, unsigned short, bool);
            Fn fn = owner->GetOriginal<Fn>(11);
            if (!fn) return;
            fn(owner->raw, host, remotePort, clientPort, onlyReplyOnAcceptingConnections);
        }

        int GetAveragePing(void) override
        {
            using Fn = int(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(12);
            if (!fn) return 0;
            return fn(owner->raw);
        }

        int GetLastPing(void) const override
        {
            using Fn = int(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(13);
            if (!fn) return 0;
            return fn(owner->raw);
        }

        int GetLowestPing(void) const override
        {
            using Fn = int(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(14);
            if (!fn) return 0;
            return fn(owner->raw);
        }

        int GetPlayerPing(PlayerID playerId) override
        {
            using Fn = int(__thiscall*)(void*, PlayerID);
            Fn fn = owner->GetOriginal<Fn>(15);
            if (!fn) return 0;
            return fn(owner->raw, playerId);
        }

        void StartOccasionalPing(void) override
        {
            using Fn = void(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(16);
            if (!fn) return;
            fn(owner->raw);
        }

        void StopOccasionalPing(void) override
        {
            using Fn = void(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(17);
            if (!fn) return;
            fn(owner->raw);
        }

        bool IsConnected(void) const override
        {
            using Fn = bool(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(18);
            if (!fn) return false;
            return fn(owner->raw);
        }

        unsigned int GetSynchronizedRandomInteger(void) const override
        {
            using Fn = unsigned int(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(19);
            if (!fn) return 0;
            return fn(owner->raw);
        }

        bool GenerateCompressionLayer(unsigned int frequencyTable[256], bool inputLayer) override
        {
            using Fn = bool(__thiscall*)(void*, unsigned int[256], bool);
            Fn fn = owner->GetOriginal<Fn>(20);
            if (!fn) return false;
            return fn(owner->raw, frequencyTable, inputLayer);
        }

        bool DeleteCompressionLayer(bool inputLayer) override
        {
            using Fn = bool(__thiscall*)(void*, bool);
            Fn fn = owner->GetOriginal<Fn>(21);
            if (!fn) return false;
            return fn(owner->raw, inputLayer);
        }

        void RegisterAsRemoteProcedureCall(int* uniqueID, void (*functionPointer)(RPCParameters*)) override
        {
            using Fn = void(__thiscall*)(void*, int*, void (*)(RPCParameters*));
            Fn fn = owner->GetOriginal<Fn>(22);
            if (!fn) return;
            fn(owner->raw, uniqueID, functionPointer);
        }

        void RegisterClassMemberRPC(int* uniqueID, void* obj) override
        {
            using Fn = void(__thiscall*)(void*, int*, void*);
            Fn fn = owner->GetOriginal<Fn>(23);
            if (!fn) return;
            fn(owner->raw, uniqueID, obj);
        }

        void UnregisterAsRemoteProcedureCall(int* uniqueID) override
        {
            using Fn = void(__thiscall*)(void*, int*);
            Fn fn = owner->GetOriginal<Fn>(24);
            if (!fn) return;
            fn(owner->raw, uniqueID);
        }

        bool RPC(int* uniqueID, const char* data, unsigned int bitLength, PacketPriority priority, PacketReliability reliability, char orderingChannel, bool shiftTimestampByClientTime) override
        {
            using Fn = bool(__thiscall*)(void*, int*, const char*, unsigned int, PacketPriority, PacketReliability, char, bool);
            Fn fn = owner->GetOriginal<Fn>(26);
            if (!fn) return false;
            return fn(owner->raw, uniqueID, data, bitLength, priority, reliability, orderingChannel, shiftTimestampByClientTime);
        }

        bool RPC(int* uniqueID, RakNet::BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel, bool shiftTimestampByClientTime) override
        {
            using Fn = bool(__thiscall*)(void*, int*, RakNet::BitStream*, PacketPriority, PacketReliability, char, bool);
            Fn fn = owner->GetOriginal<Fn>(25);
            if (!fn) return false;
            return fn(owner->raw, uniqueID, bitStream, priority, reliability, orderingChannel, shiftTimestampByClientTime);
        }

        bool RPC_(int* uniqueID, RakNet::BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel, bool shiftTimestampByClientTime, NetworkID networkID) override
        {
            using Fn = bool(__thiscall*)(void*, int*, RakNet::BitStream*, PacketPriority, PacketReliability, char, bool, NetworkID);
            Fn fn = owner->GetOriginal<Fn>(27);
            if (!fn) return false;
            return fn(owner->raw, uniqueID, bitStream, priority, reliability, orderingChannel, shiftTimestampByClientTime, networkID);
        }

        void SetTrackFrequencyTable(bool trackFrequencyTable) override
        {
            using Fn = void(__thiscall*)(void*, bool);
            Fn fn = owner->GetOriginal<Fn>(28);
            if (!fn) return;
            fn(owner->raw, trackFrequencyTable);
        }

        bool GetSendFrequencyTable(unsigned int frequencyTable[256]) override
        {
            using Fn = bool(__thiscall*)(void*, unsigned int[256]);
            Fn fn = owner->GetOriginal<Fn>(29);
            if (!fn) return false;
            return fn(owner->raw, frequencyTable);
        }

        float GetCompressionRatio(void) const override
        {
            using Fn = float(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(30);
            if (!fn) return 0.0f;
            return fn(owner->raw);
        }

        float GetDecompressionRatio(void) const override
        {
            using Fn = float(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(31);
            if (!fn) return 0.0f;
            return fn(owner->raw);
        }

        void AttachPlugin(void* plugin) override
        {
            using Fn = void(__thiscall*)(void*, void*);
            Fn fn = owner->GetOriginal<Fn>(32);
            if (!fn) return;
            fn(owner->raw, plugin);
        }

        void DetachPlugin(void* plugin) override
        {
            using Fn = void(__thiscall*)(void*, void*);
            Fn fn = owner->GetOriginal<Fn>(33);
            if (!fn) return;
            fn(owner->raw, plugin);
        }

        RakNet::BitStream* GetStaticServerData(void) override
        {
            using Fn = RakNet::BitStream * (__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(34);
            if (!fn) return nullptr;
            return fn(owner->raw);
        }

        void SetStaticServerData(const char* data, int length) override
        {
            using Fn = void(__thiscall*)(void*, const char*, int);
            Fn fn = owner->GetOriginal<Fn>(35);
            if (!fn) return;
            fn(owner->raw, data, length);
        }

        RakNet::BitStream* GetStaticClientData(PlayerID playerId) override
        {
            using Fn = RakNet::BitStream * (__thiscall*)(void*, PlayerID);
            Fn fn = owner->GetOriginal<Fn>(36);
            if (!fn) return nullptr;
            return fn(owner->raw, playerId);
        }

        void SetStaticClientData(PlayerID playerId, const char* data, int length) override
        {
            using Fn = void(__thiscall*)(void*, PlayerID, const char*, int);
            Fn fn = owner->GetOriginal<Fn>(37);
            if (!fn) return;
            fn(owner->raw, playerId, data, length);
        }

        void SendStaticClientDataToServer(void) override
        {
            using Fn = void(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(38);
            if (!fn) return;
            fn(owner->raw);
        }

        PlayerID GetServerID(void) const override
        {
            using Fn = void(__thiscall*)(void*, PlayerID*);
            Fn fn = reinterpret_cast<Fn>(owner->GetOriginalRaw(39));
            if (!fn) return PlayerID{};
            PlayerID result{};
            fn(owner->raw, &result);
            return result;
        }

        PlayerID GetPlayerID(void) const override
        {
            using Fn = void(__thiscall*)(void*, PlayerID*);
            Fn fn = reinterpret_cast<Fn>(owner->GetOriginalRaw(40));
            if (!fn) return PlayerID{};
            PlayerID result{};
            fn(owner->raw, &result);
            return result;
        }

        PlayerID GetInternalID(void) const override
        {
            using Fn = void(__thiscall*)(void*, PlayerID*);
            Fn fn = reinterpret_cast<Fn>(owner->GetOriginalRaw(41));
            if (!fn) return PlayerID{};
            PlayerID result{};
            fn(owner->raw, &result);
            return result;
        }

        const char* PlayerIDToDottedIP(PlayerID playerId) const override
        {
            using Fn = const char* (__thiscall*)(void*, PlayerID);
            Fn fn = owner->GetOriginal<Fn>(42);
            if (!fn) return nullptr;
            return fn(owner->raw, playerId);
        }

        void PushBackPacket(Packet* packet, bool pushAtHead) override
        {
            using Fn = void(__thiscall*)(void*, Packet*, bool);
            Fn fn = owner->GetOriginal<Fn>(43);
            if (!fn) return;
            fn(owner->raw, packet, pushAtHead);
        }

        void SetRouterInterface(void* routerInterface) override
        {
            using Fn = void(__thiscall*)(void*, void*);
            Fn fn = owner->GetOriginal<Fn>(44);
            if (!fn) return;
            fn(owner->raw, routerInterface);
        }

        void RemoveRouterInterface(void* routerInterface) override
        {
            using Fn = void(__thiscall*)(void*, void*);
            Fn fn = owner->GetOriginal<Fn>(45);
            if (!fn) return;
            fn(owner->raw, routerInterface);
        }

        void SetTimeoutTime(RakNetTime timeoutMS) override
        {
            using Fn = void(__thiscall*)(void*, RakNetTime);
            Fn fn = owner->GetOriginal<Fn>(46);
            if (!fn) return;
            fn(owner->raw, timeoutMS);
        }

        bool SetMTUSize(int size) override
        {
            using Fn = bool(__thiscall*)(void*, int);
            Fn fn = owner->GetOriginal<Fn>(47);
            if (!fn) return false;
            return fn(owner->raw, size);
        }

        int GetMTUSize(void) const override
        {
            using Fn = int(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(48);
            if (!fn) return 0;
            return fn(owner->raw);
        }

        void AllowConnectionResponseIPMigration(bool allow) override
        {
            using Fn = void(__thiscall*)(void*, bool);
            Fn fn = owner->GetOriginal<Fn>(49);
            if (!fn) return;
            fn(owner->raw, allow);
        }

        void AdvertiseSystem(const char* host, unsigned short remotePort, const char* data, int dataLength) override
        {
            using Fn = void(__thiscall*)(void*, const char*, unsigned short, const char*, int);
            Fn fn = owner->GetOriginal<Fn>(50);
            if (!fn) return;
            fn(owner->raw, host, remotePort, data, dataLength);
        }

        RakNetStatisticsStruct* const GetStatistics(void) override
        {
            using Fn = RakNetStatisticsStruct* const (__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(51);
            if (!fn) return nullptr;
            return fn(owner->raw);
        }

        void ApplyNetworkSimulator(double maxSendBPS, unsigned short minExtraLatency, unsigned short extraLatencyVariance) override
        {
            using Fn = void(__thiscall*)(void*, double, unsigned short, unsigned short);
            Fn fn = owner->GetOriginal<Fn>(52);
            if (!fn) return;
            fn(owner->raw, maxSendBPS, minExtraLatency, extraLatencyVariance);
        }

        bool IsNetworkSimulatorActive(void) override
        {
            using Fn = bool(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(53);
            if (!fn) return false;
            return fn(owner->raw);
        }

        PlayerIndex GetPlayerIndex(void) override
        {
            using Fn = PlayerIndex(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(54);
            if (!fn) return PlayerIndex{};
            return fn(owner->raw);
        }

    private:
        TERakClient* owner;
    };

    Wrapper wrapper{ this };
};