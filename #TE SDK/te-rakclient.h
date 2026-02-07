#pragma once

#include <cstdint>
#include "FullRakNet/BitStream.h"
#include "FullRakNet/PacketPriority.h"
#include "FullRakNet/RakNetStatistics.h"

using namespace RakNet;

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
    virtual bool Send(BitStream*, PacketPriority, PacketReliability, char) = 0;
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
    virtual bool RPC(int*, BitStream*, PacketPriority, PacketReliability, char, bool) = 0;
    virtual bool RPC_(int*, BitStream*, PacketPriority, PacketReliability, char, bool, NetworkID) = 0;
    virtual void SetTrackFrequencyTable(bool) = 0;
    virtual bool GetSendFrequencyTable(unsigned int[256]) = 0;
    virtual float GetCompressionRatio(void) const = 0;
    virtual float GetDecompressionRatio(void) const = 0;
    virtual void AttachPlugin(void*) = 0;
    virtual void DetachPlugin(void*) = 0;
    virtual BitStream* GetStaticServerData(void) = 0;
    virtual void SetStaticServerData(const char*, int) = 0;
    virtual BitStream* GetStaticClientData(PlayerID) = 0;
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

    void* GetRawInterface() { return raw; }
    void** GetVTable() { return originalVtable; }
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

    bool SendRPC(int rpcId, BitStream* bitStream, PacketPriority priority,
        PacketReliability reliability, char orderingChannel, bool shiftTimestamp);

    bool SendPacket(BitStream* bitStream, PacketPriority priority,
        PacketReliability reliability, char orderingChannel);

    bool SendRPC(int rpcId, BitStream* bitStream)
    {
        return SendRPC(rpcId, bitStream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
    }

    bool SendRPC(int rpcId, BitStream* bitStream, PacketPriority priority, PacketReliability reliability)
    {
        return SendRPC(rpcId, bitStream, priority, reliability, 0, false);
    }

    bool SendRPC(int rpcId, BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel)
    {
        return SendRPC(rpcId, bitStream, priority, reliability, orderingChannel, false);
    }

    bool SendPacket(BitStream* bitStream)
    {
        return SendPacket(bitStream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
    }

    bool SendPacket(BitStream* bitStream, PacketPriority priority, PacketReliability reliability)
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
            return fn(owner->raw, host, serverPort, clientPort, depreciated, threadSleepTimer);
        }

        void Disconnect(unsigned int blockDuration, unsigned char orderingChannel = 0) override
        {
            using Fn = void(__thiscall*)(void*, unsigned int, unsigned char);
            Fn fn = owner->GetOriginal<Fn>(2);
            fn(owner->raw, blockDuration, orderingChannel);
        }

        void InitializeSecurity(const char* privKeyP, const char* privKeyQ) override
        {
            using Fn = void(__thiscall*)(void*, const char*, const char*);
            Fn fn = owner->GetOriginal<Fn>(3);
            fn(owner->raw, privKeyP, privKeyQ);
        }

        void SetPassword(const char* password) override
        {
            using Fn = void(__thiscall*)(void*, const char*);
            Fn fn = owner->GetOriginal<Fn>(4);
            fn(owner->raw, password);
        }

        bool HasPassword(void) const override
        {
            using Fn = bool(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(5);
            return fn(owner->raw);
        }

        bool Send(const char* data, int length, PacketPriority priority, PacketReliability reliability, char orderingChannel) override
        {
            using Fn = bool(__thiscall*)(void*, const char*, int, PacketPriority, PacketReliability, char);
            Fn fn = owner->GetOriginal<Fn>(7);
            return fn(owner->raw, data, length, priority, reliability, orderingChannel);
        }

        bool Send(BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel) override
        {
            using Fn = bool(__thiscall*)(void*, BitStream*, PacketPriority, PacketReliability, char);
            Fn fn = owner->GetOriginal<Fn>(6);
            return fn(owner->raw, bitStream, priority, reliability, orderingChannel);
        }

        Packet* Receive(void) override
        {
            using Fn = Packet * (__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(8);
            return fn(owner->raw);
        }

        void DeallocatePacket(Packet* packet) override
        {
            using Fn = void(__thiscall*)(void*, Packet*);
            Fn fn = owner->GetOriginal<Fn>(9);
            fn(owner->raw, packet);
        }

        void PingServer(void) override
        {
            using Fn = void(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(10);
            fn(owner->raw);
        }

        void PingServer(const char* host, unsigned short remotePort, unsigned short clientPort, bool onlyReplyOnAcceptingConnections) override
        {
            using Fn = void(__thiscall*)(void*, const char*, unsigned short, unsigned short, bool);
            Fn fn = owner->GetOriginal<Fn>(11);
            fn(owner->raw, host, remotePort, clientPort, onlyReplyOnAcceptingConnections);
        }

        int GetAveragePing(void) override
        {
            using Fn = int(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(12);
            return fn(owner->raw);
        }

        int GetLastPing(void) const override
        {
            using Fn = int(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(13);
            return fn(owner->raw);
        }

        int GetLowestPing(void) const override
        {
            using Fn = int(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(14);
            return fn(owner->raw);
        }

        int GetPlayerPing(PlayerID playerId) override
        {
            using Fn = int(__thiscall*)(void*, PlayerID);
            Fn fn = owner->GetOriginal<Fn>(15);
            return fn(owner->raw, playerId);
        }

        void StartOccasionalPing(void) override
        {
            using Fn = void(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(16);
            fn(owner->raw);
        }

        void StopOccasionalPing(void) override
        {
            using Fn = void(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(17);
            fn(owner->raw);
        }

        bool IsConnected(void) const override
        {
            using Fn = bool(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(18);
            return fn(owner->raw);
        }

        unsigned int GetSynchronizedRandomInteger(void) const override
        {
            using Fn = unsigned int(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(19);
            return fn(owner->raw);
        }

        bool GenerateCompressionLayer(unsigned int frequencyTable[256], bool inputLayer) override
        {
            using Fn = bool(__thiscall*)(void*, unsigned int[256], bool);
            Fn fn = owner->GetOriginal<Fn>(20);
            return fn(owner->raw, frequencyTable, inputLayer);
        }

        bool DeleteCompressionLayer(bool inputLayer) override
        {
            using Fn = bool(__thiscall*)(void*, bool);
            Fn fn = owner->GetOriginal<Fn>(21);
            return fn(owner->raw, inputLayer);
        }

        void RegisterAsRemoteProcedureCall(int* uniqueID, void (*functionPointer)(RPCParameters*)) override
        {
            using Fn = void(__thiscall*)(void*, int*, void (*)(RPCParameters*));
            Fn fn = owner->GetOriginal<Fn>(22);
            fn(owner->raw, uniqueID, functionPointer);
        }

        void RegisterClassMemberRPC(int* uniqueID, void* obj) override
        {
            using Fn = void(__thiscall*)(void*, int*, void*);
            Fn fn = owner->GetOriginal<Fn>(23);
            fn(owner->raw, uniqueID, obj);
        }

        void UnregisterAsRemoteProcedureCall(int* uniqueID) override
        {
            using Fn = void(__thiscall*)(void*, int*);
            Fn fn = owner->GetOriginal<Fn>(24);
            fn(owner->raw, uniqueID);
        }

        bool RPC(int* uniqueID, const char* data, unsigned int bitLength, PacketPriority priority, PacketReliability reliability, char orderingChannel, bool shiftTimestampByClientTime) override
        {
            using Fn = bool(__thiscall*)(void*, int*, const char*, unsigned int, PacketPriority, PacketReliability, char, bool);
            Fn fn = owner->GetOriginal<Fn>(26);
            return fn(owner->raw, uniqueID, data, bitLength, priority, reliability, orderingChannel, shiftTimestampByClientTime);
        }

        bool RPC(int* uniqueID, BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel, bool shiftTimestampByClientTime) override
        {
            using Fn = bool(__thiscall*)(void*, int*, BitStream*, PacketPriority, PacketReliability, char, bool);
            Fn fn = owner->GetOriginal<Fn>(25);
            return fn(owner->raw, uniqueID, bitStream, priority, reliability, orderingChannel, shiftTimestampByClientTime);
        }

        bool RPC_(int* uniqueID, BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel, bool shiftTimestampByClientTime, NetworkID networkID) override
        {
            using Fn = bool(__thiscall*)(void*, int*, BitStream*, PacketPriority, PacketReliability, char, bool, NetworkID);
            Fn fn = owner->GetOriginal<Fn>(27);
            return fn(owner->raw, uniqueID, bitStream, priority, reliability, orderingChannel, shiftTimestampByClientTime, networkID);
        }

        void SetTrackFrequencyTable(bool trackFrequencyTable) override
        {
            using Fn = void(__thiscall*)(void*, bool);
            Fn fn = owner->GetOriginal<Fn>(28);
            fn(owner->raw, trackFrequencyTable);
        }

        bool GetSendFrequencyTable(unsigned int frequencyTable[256]) override
        {
            using Fn = bool(__thiscall*)(void*, unsigned int[256]);
            Fn fn = owner->GetOriginal<Fn>(29);
            return fn(owner->raw, frequencyTable);
        }

        float GetCompressionRatio(void) const override
        {
            using Fn = float(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(30);
            return fn(owner->raw);
        }

        float GetDecompressionRatio(void) const override
        {
            using Fn = float(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(31);
            return fn(owner->raw);
        }

        void AttachPlugin(void* plugin) override
        {
            using Fn = void(__thiscall*)(void*, void*);
            Fn fn = owner->GetOriginal<Fn>(32);
            fn(owner->raw, plugin);
        }

        void DetachPlugin(void* plugin) override
        {
            using Fn = void(__thiscall*)(void*, void*);
            Fn fn = owner->GetOriginal<Fn>(33);
            fn(owner->raw, plugin);
        }

        BitStream* GetStaticServerData(void) override
        {
            using Fn = BitStream * (__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(34);
            return fn(owner->raw);
        }

        void SetStaticServerData(const char* data, int length) override
        {
            using Fn = void(__thiscall*)(void*, const char*, int);
            Fn fn = owner->GetOriginal<Fn>(35);
            fn(owner->raw, data, length);
        }

        BitStream* GetStaticClientData(PlayerID playerId) override
        {
            using Fn = BitStream * (__thiscall*)(void*, PlayerID);
            Fn fn = owner->GetOriginal<Fn>(36);
            return fn(owner->raw, playerId);
        }

        void SetStaticClientData(PlayerID playerId, const char* data, int length) override
        {
            using Fn = void(__thiscall*)(void*, PlayerID, const char*, int);
            Fn fn = owner->GetOriginal<Fn>(37);
            fn(owner->raw, playerId, data, length);
        }

        void SendStaticClientDataToServer(void) override
        {
            using Fn = void(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(38);
            fn(owner->raw);
        }

        PlayerID GetServerID(void) const override
        {
            using Fn = PlayerID(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(39);
            return fn(owner->raw);
        }

        PlayerID GetPlayerID(void) const override
        {
            using Fn = PlayerID(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(40);
            return fn(owner->raw);
        }

        PlayerID GetInternalID(void) const override
        {
            using Fn = PlayerID(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(41);
            return fn(owner->raw);
        }

        const char* PlayerIDToDottedIP(PlayerID playerId) const override
        {
            using Fn = const char* (__thiscall*)(void*, PlayerID);
            Fn fn = owner->GetOriginal<Fn>(42);
            return fn(owner->raw, playerId);
        }

        void PushBackPacket(Packet* packet, bool pushAtHead) override
        {
            using Fn = void(__thiscall*)(void*, Packet*, bool);
            Fn fn = owner->GetOriginal<Fn>(43);
            fn(owner->raw, packet, pushAtHead);
        }

        void SetRouterInterface(void* routerInterface) override
        {
            using Fn = void(__thiscall*)(void*, void*);
            Fn fn = owner->GetOriginal<Fn>(44);
            fn(owner->raw, routerInterface);
        }

        void RemoveRouterInterface(void* routerInterface) override
        {
            using Fn = void(__thiscall*)(void*, void*);
            Fn fn = owner->GetOriginal<Fn>(45);
            fn(owner->raw, routerInterface);
        }

        void SetTimeoutTime(RakNetTime timeoutMS) override
        {
            using Fn = void(__thiscall*)(void*, RakNetTime);
            Fn fn = owner->GetOriginal<Fn>(46);
            fn(owner->raw, timeoutMS);
        }

        bool SetMTUSize(int size) override
        {
            using Fn = bool(__thiscall*)(void*, int);
            Fn fn = owner->GetOriginal<Fn>(47);
            return fn(owner->raw, size);
        }

        int GetMTUSize(void) const override
        {
            using Fn = int(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(48);
            return fn(owner->raw);
        }

        void AllowConnectionResponseIPMigration(bool allow) override
        {
            using Fn = void(__thiscall*)(void*, bool);
            Fn fn = owner->GetOriginal<Fn>(49);
            fn(owner->raw, allow);
        }

        void AdvertiseSystem(const char* host, unsigned short remotePort, const char* data, int dataLength) override
        {
            using Fn = void(__thiscall*)(void*, const char*, unsigned short, const char*, int);
            Fn fn = owner->GetOriginal<Fn>(50);
            fn(owner->raw, host, remotePort, data, dataLength);
        }

        RakNetStatisticsStruct* const GetStatistics(void) override
        {
            using Fn = RakNetStatisticsStruct* const (__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(51);
            return fn(owner->raw);
        }

        void ApplyNetworkSimulator(double maxSendBPS, unsigned short minExtraLatency, unsigned short extraLatencyVariance) override
        {
            using Fn = void(__thiscall*)(void*, double, unsigned short, unsigned short);
            Fn fn = owner->GetOriginal<Fn>(52);
            fn(owner->raw, maxSendBPS, minExtraLatency, extraLatencyVariance);
        }

        bool IsNetworkSimulatorActive(void) override
        {
            using Fn = bool(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(53);
            return fn(owner->raw);
        }

        PlayerIndex GetPlayerIndex(void) override
        {
            using Fn = PlayerIndex(__thiscall*)(void*);
            Fn fn = owner->GetOriginal<Fn>(54);
            return fn(owner->raw);
        }

    private:
        TERakClient* owner;
    };

    Wrapper wrapper{ this };
};