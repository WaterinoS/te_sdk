#include "te-sdk.h"

#include "FullRakNet/PacketEnumerations.h"

using namespace RakNet;
using namespace te_sdk;

bool HookedRakClientInterface::Connect(const char* host, unsigned short serverPort, unsigned short clientPort,
    unsigned int depreciated, int threadSleepTimer)
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->Connect(host, serverPort, clientPort, depreciated, threadSleepTimer) : false;
}

void HookedRakClientInterface::Disconnect(unsigned int blockDuration, unsigned char orderingChannel)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->Disconnect(blockDuration, orderingChannel);
}

void HookedRakClientInterface::InitializeSecurity(const char* privKeyP, const char* privKeyQ)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->InitializeSecurity(privKeyP, privKeyQ);
}

void HookedRakClientInterface::SetPassword(const char* _password)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->SetPassword(_password);
}

bool HookedRakClientInterface::HasPassword(void) const
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->HasPassword() : false;
}

bool HookedRakClientInterface::Send(const char* data, int length, PacketPriority priority, PacketReliability reliability, char orderingChannel)
{
    if (!LocalClient || !LocalClient->GetInterface()) return false;

    if (data && length > 0 && forwarder)
    {
        BitStream bs(reinterpret_cast<unsigned char*>(const_cast<char*>(data)), length, false);
        uint8_t packetId = 0;
        bs.Read(packetId);
        if (!forwarder->OutgoingPacket(packetId, &bs, this))
			return false;

        bs.ResetReadPointer();
    }

    return LocalClient->GetInterface()->Send(data, length, priority, reliability, orderingChannel);
}

bool HookedRakClientInterface::Send(BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel)
{
    if (!LocalClient || !LocalClient->GetInterface()) return false;

    if (bitStream && forwarder)
    {
        uint8_t packetId = 0;
        bitStream->Read(packetId);
        if (!forwarder->OutgoingPacket(packetId, bitStream, this))
            return false;

        bitStream->ResetReadPointer();
    }

    return LocalClient->GetInterface()->Send(bitStream, priority, reliability, orderingChannel);
}

Packet* HookedRakClientInterface::Receive(void)
{
    Packet* p = LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->Receive() : nullptr;

    if (p && p->data && p->length > 0 && forwarder)
    {
        uint8_t packetId = p->data[0];

        if (packetId == PacketEnumeration::ID_RPC)
        {
            if (p->length >= 2)
            {
                BitStream bs(p->data, p->length, false);
                bs.IgnoreBits(8);

                uint8_t rpcId;
                if (bs.Read(rpcId))
                {
                    if (!forwarder->IncomingRpc(rpcId, &bs, this))
                    {
                        return nullptr;
                    }
                }
            }
        }
        else
        {
            BitStream bs(p->data, p->length, false);
            if (!forwarder->IncomingPacket(packetId, &bs, this))
            {
                return nullptr;
            }
        }
    }

    return p;
}

void HookedRakClientInterface::DeallocatePacket(Packet* packet)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->DeallocatePacket(packet);
}

void HookedRakClientInterface::PingServer(void)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->PingServer();
}

void HookedRakClientInterface::PingServer(const char* host, unsigned short serverPort, unsigned short clientPort, bool onlyReplyOnAcceptingConnections)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->PingServer(host, serverPort, clientPort, onlyReplyOnAcceptingConnections);
}

int HookedRakClientInterface::GetAveragePing(void)
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetAveragePing() : 0;
}

int HookedRakClientInterface::GetLastPing(void) const
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetLastPing() : 0;
}

int HookedRakClientInterface::GetLowestPing(void) const
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetLowestPing() : 0;
}

int HookedRakClientInterface::GetPlayerPing(PlayerID playerId)
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetPlayerPing(playerId) : 0;
}

void HookedRakClientInterface::StartOccasionalPing(void)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->StartOccasionalPing();
}

void HookedRakClientInterface::StopOccasionalPing(void)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->StopOccasionalPing();
}

bool HookedRakClientInterface::IsConnected(void) const
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->IsConnected() : false;
}

unsigned int HookedRakClientInterface::GetSynchronizedRandomInteger(void) const
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetSynchronizedRandomInteger() : 0;
}

bool HookedRakClientInterface::GenerateCompressionLayer(unsigned int inputFrequencyTable[256], bool inputLayer)
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GenerateCompressionLayer(inputFrequencyTable, inputLayer) : false;
}

bool HookedRakClientInterface::DeleteCompressionLayer(bool inputLayer)
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->DeleteCompressionLayer(inputLayer) : false;
}

void HookedRakClientInterface::RegisterAsRemoteProcedureCall(int* uniqueID, void (*functionPointer)(RPCParameters* rpcParms))
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->RegisterAsRemoteProcedureCall(uniqueID, functionPointer);
}

void HookedRakClientInterface::RegisterClassMemberRPC(int* uniqueID, void* functionPointer)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->RegisterClassMemberRPC(uniqueID, functionPointer);
}

void HookedRakClientInterface::UnregisterAsRemoteProcedureCall(int* uniqueID)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->UnregisterAsRemoteProcedureCall(uniqueID);
}

bool HookedRakClientInterface::RPC(int* uniqueID, const char* data, unsigned int bitLength, PacketPriority priority, PacketReliability reliability, char orderingChannel, bool shiftTimestamp)
{
    if (!LocalClient || !LocalClient->GetInterface()) return false;

    if (uniqueID && data && bitLength > 0 && forwarder)
    {
        BitStream bs(reinterpret_cast<unsigned char*>(const_cast<char*>(data)), BITS_TO_BYTES(bitLength), false);
        if (!forwarder->OutgoingRpc(static_cast<uint8_t>(*uniqueID), &bs, this))
			return false;

        bs.ResetReadPointer();
    }

    return LocalClient->GetInterface()->RPC(uniqueID, data, bitLength, priority, reliability, orderingChannel, shiftTimestamp);
}

bool HookedRakClientInterface::RPC(int* uniqueID, BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel, bool shiftTimestamp)
{
    if (!LocalClient || !LocalClient->GetInterface()) return false;

    if (uniqueID && bitStream && forwarder)
    {
        if (!forwarder->OutgoingRpc(static_cast<uint8_t>(*uniqueID), bitStream, this))
            return false;

        bitStream->ResetReadPointer();
    }

    return LocalClient->GetInterface()->RPC(uniqueID, bitStream, priority, reliability, orderingChannel, shiftTimestamp);
}

bool HookedRakClientInterface::RPC_(int* uniqueID, BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel, bool shiftTimestamp, NetworkID networkID)
{
    if (!LocalClient || !LocalClient->GetInterface()) return false;

    if (uniqueID && bitStream && forwarder)
    {
        if (!forwarder->OutgoingRpc(static_cast<uint8_t>(*uniqueID), bitStream, this))
            return false;

        bitStream->ResetReadPointer();
    }

    return LocalClient->GetInterface()->RPC_(uniqueID, bitStream, priority, reliability, orderingChannel, shiftTimestamp, networkID);
}

void HookedRakClientInterface::SetTrackFrequencyTable(bool b)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->SetTrackFrequencyTable(b);
}

bool HookedRakClientInterface::GetSendFrequencyTable(unsigned int outputFrequencyTable[256])
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetSendFrequencyTable(outputFrequencyTable) : false;
}

float HookedRakClientInterface::GetCompressionRatio(void) const
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetCompressionRatio() : 0.0f;
}

float HookedRakClientInterface::GetDecompressionRatio(void) const
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetDecompressionRatio() : 0.0f;
}

void HookedRakClientInterface::AttachPlugin(void* messageHandler)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->AttachPlugin(messageHandler);
}

void HookedRakClientInterface::DetachPlugin(void* messageHandler)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->DetachPlugin(messageHandler);
}

BitStream* HookedRakClientInterface::GetStaticServerData(void)
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetStaticServerData() : nullptr;
}

void HookedRakClientInterface::SetStaticServerData(const char* data, int length)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->SetStaticServerData(data, length);
}

BitStream* HookedRakClientInterface::GetStaticClientData(PlayerID playerId)
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetStaticClientData(playerId) : nullptr;
}

void HookedRakClientInterface::SetStaticClientData(PlayerID playerId, const char* data, int length)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->SetStaticClientData(playerId, data, length);
}

void HookedRakClientInterface::SendStaticClientDataToServer(void)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->SendStaticClientDataToServer();
}

PlayerID HookedRakClientInterface::GetServerID(void) const
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetServerID() : PlayerID();
}

PlayerID HookedRakClientInterface::GetPlayerID(void) const
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetPlayerID() : PlayerID();
}

PlayerID HookedRakClientInterface::GetInternalID(void) const
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetInternalID() : PlayerID();
}

const char* HookedRakClientInterface::PlayerIDToDottedIP(PlayerID playerId) const
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->PlayerIDToDottedIP(playerId) : "";
}

void HookedRakClientInterface::PushBackPacket(Packet* packet, bool pushAtHead)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->PushBackPacket(packet, pushAtHead);
}

void HookedRakClientInterface::SetRouterInterface(void* routerInterface)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->SetRouterInterface(routerInterface);
}

void HookedRakClientInterface::RemoveRouterInterface(void* routerInterface)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->RemoveRouterInterface(routerInterface);
}

void HookedRakClientInterface::SetTimeoutTime(RakNetTime timeMS)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->SetTimeoutTime(timeMS);
}

bool HookedRakClientInterface::SetMTUSize(int size)
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->SetMTUSize(size) : false;
}

int HookedRakClientInterface::GetMTUSize(void) const
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetMTUSize() : 0;
}

void HookedRakClientInterface::AllowConnectionResponseIPMigration(bool allow)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->AllowConnectionResponseIPMigration(allow);
}

void HookedRakClientInterface::AdvertiseSystem(const char* host, unsigned short remotePort, const char* data, int dataLength)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->AdvertiseSystem(host, remotePort, data, dataLength);
}

RakNetStatisticsStruct* const HookedRakClientInterface::GetStatistics(void)
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetStatistics() : nullptr;
}

void HookedRakClientInterface::ApplyNetworkSimulator(double maxSendBPS, unsigned short minExtraPing, unsigned short extraPingVariance)
{
    if (LocalClient && LocalClient->GetInterface())
        LocalClient->GetInterface()->ApplyNetworkSimulator(maxSendBPS, minExtraPing, extraPingVariance);
}

bool HookedRakClientInterface::IsNetworkSimulatorActive(void)
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->IsNetworkSimulatorActive() : false;
}

PlayerIndex HookedRakClientInterface::GetPlayerIndex(void)
{
    return LocalClient && LocalClient->GetInterface() ? LocalClient->GetInterface()->GetPlayerIndex() : 0;
}

void HookedRakClientInterface::SetForwarder(te_sdk::forwarder::HookForwarder* fwd)
{
    forwarder = fwd;
}