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
    virtual DataPacket* Receive(void) = 0;
    virtual void DeallocatePacket(DataPacket*) = 0;
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
    virtual void PushBackPacket(DataPacket*, bool) = 0;
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
    TERakClient(void* pRakClientInterface);

    bool SendRPC(int rpcId, BitStream* bitStream, PacketPriority priority = HIGH_PRIORITY,
        PacketReliability reliability = RELIABLE_ORDERED, char orderingChannel = 0, bool shiftTimestamp = false);

    bool SendPacket(BitStream* bitStream, PacketPriority priority = HIGH_PRIORITY,
        PacketReliability reliability = UNRELIABLE_SEQUENCED, char orderingChannel = 0);

    local_RakClientInterface* GetInterface() { return pRakClient; }

private:
    local_RakClientInterface* pRakClient;
};
