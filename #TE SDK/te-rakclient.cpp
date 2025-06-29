#include "te-rakclient.h"

TERakClient::TERakClient(void* pRakClientInterface)
{
    pRakClient = reinterpret_cast<local_RakClientInterface*>(pRakClientInterface);
}

bool TERakClient::SendRPC(int rpcId, BitStream* bitStream, PacketPriority priority,
    PacketReliability reliability, char orderingChannel, bool shiftTimestamp)
{
    if (!pRakClient || !bitStream)
        return false;

    int rpcUniqueId = rpcId;
    return pRakClient->RPC(&rpcUniqueId, bitStream, priority, reliability, orderingChannel, shiftTimestamp);
}

bool TERakClient::SendPacket(BitStream* bitStream, PacketPriority priority,
    PacketReliability reliability, char orderingChannel)
{
    if (!pRakClient || !bitStream)
        return false;

    return pRakClient->Send(bitStream, priority, reliability, orderingChannel);
}
