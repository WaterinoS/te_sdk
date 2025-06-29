#pragma once

#include "FullRakNet/BitStream.h"
#include "FullRakNet/PacketPriority.h"
#include "FullRakNet/RakNetStatistics.h"

using namespace RakNet;

namespace te_sdk::detail {
    struct HookForwarder;
}

class HookedRakClientInterface
{
public:
    virtual ~HookedRakClientInterface() {}

    virtual bool Connect(const char* host, unsigned short serverPort, unsigned short clientPort,
        unsigned int depreciated, int threadSleepTimer);

    virtual void Disconnect(unsigned int blockDuration, unsigned char orderingChannel = 0);

    virtual void InitializeSecurity(const char* privKeyP, const char* privKeyQ);

    virtual void SetPassword(const char* _password);

    virtual bool HasPassword(void) const;

    virtual bool Send(const char* data, int length, PacketPriority priority, PacketReliability reliability,
        char orderingChannel);

    virtual bool Send(BitStream* bitStream, PacketPriority priority, PacketReliability reliability,
        char orderingChannel);

    virtual DataPacket* Receive(void);

    virtual void DeallocatePacket(DataPacket* packet);

    virtual void PingServer(void);

    virtual void PingServer(const char* host, unsigned short serverPort, unsigned short clientPort,
        bool onlyReplyOnAcceptingConnections);

    virtual int GetAveragePing(void);

    virtual int GetLastPing(void) const;

    virtual int GetLowestPing(void) const;

    virtual int GetPlayerPing(PlayerID playerId);

    virtual void StartOccasionalPing(void);

    virtual void StopOccasionalPing(void);

    virtual bool IsConnected(void) const;

    virtual unsigned int GetSynchronizedRandomInteger(void) const;

    virtual bool GenerateCompressionLayer(unsigned int inputFrequencyTable[256], bool inputLayer);

    virtual bool DeleteCompressionLayer(bool inputLayer);

    virtual void RegisterAsRemoteProcedureCall(int* uniqueID, void (*functionPointer)(RPCParameters* rpcParms));

    virtual void RegisterClassMemberRPC(int* uniqueID, void* functionPointer);

    virtual void UnregisterAsRemoteProcedureCall(int* uniqueID);

    virtual bool RPC(int* uniqueID, const char* data, unsigned int bitLength, PacketPriority priority,
        PacketReliability reliability, char orderingChannel, bool shiftTimestamp);

    virtual bool RPC(int* uniqueID, BitStream* bitStream, PacketPriority priority, PacketReliability reliability,
        char orderingChannel, bool shiftTimestamp);

    virtual bool RPC_(int* uniqueID, BitStream* bitStream, PacketPriority priority, PacketReliability reliability,
        char orderingChannel, bool shiftTimestamp, NetworkID networkID);

    virtual void SetTrackFrequencyTable(bool b);

    virtual bool GetSendFrequencyTable(unsigned int outputFrequencyTable[256]);

    virtual float GetCompressionRatio(void) const;

    virtual float GetDecompressionRatio(void) const;

    virtual void AttachPlugin(void* messageHandler);

    virtual void DetachPlugin(void* messageHandler);

    virtual BitStream* GetStaticServerData(void);

    virtual void SetStaticServerData(const char* data, int length);

    virtual BitStream* GetStaticClientData(PlayerID playerId);

    virtual void SetStaticClientData(PlayerID playerId, const char* data, int length);

    virtual void SendStaticClientDataToServer(void);

    virtual PlayerID GetServerID(void) const;

    virtual PlayerID GetPlayerID(void) const;

    virtual PlayerID GetInternalID(void) const;

    virtual const char* PlayerIDToDottedIP(PlayerID playerId) const;

    virtual void PushBackPacket(DataPacket* packet, bool pushAtHead);

    virtual void SetRouterInterface(void* routerInterface);

    virtual void RemoveRouterInterface(void* routerInterface);

    virtual void SetTimeoutTime(RakNetTime timeMS);

    virtual bool SetMTUSize(int size);

    virtual int GetMTUSize(void) const;

    virtual void AllowConnectionResponseIPMigration(bool allow);

    virtual void AdvertiseSystem(const char* host, unsigned short remotePort, const char* data, int dataLength);

    virtual RakNetStatisticsStruct* const GetStatistics(void);

    virtual void ApplyNetworkSimulator(double maxSendBPS, unsigned short minExtraPing,
        unsigned short extraPingVariance);

    virtual bool IsNetworkSimulatorActive(void);

    virtual PlayerIndex GetPlayerIndex(void);

    void SetForwarder(te_sdk::forwarder::HookForwarder* fwd);

    private:
        te_sdk::forwarder::HookForwarder* forwarder = nullptr;
};