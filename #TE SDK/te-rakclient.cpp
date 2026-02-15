#include "te-sdk.h"

using namespace RakNet;

TERakClient::TERakClient(void* rawInterface, void** originalVtable)
{
    this->raw = rawInterface;
    this->originalVtable = originalVtable;
}

bool TERakClient::SendRPC(int rpcId, BitStream* bitStream, PacketPriority priority,
    PacketReliability reliability, char orderingChannel, bool shiftTimestamp)
{
    if (!bitStream)
        return false;

    return GetInterface()->RPC(&rpcId, bitStream, priority, reliability, orderingChannel, shiftTimestamp);
}

bool TERakClient::SendPacket(BitStream* bitStream, PacketPriority priority,
    PacketReliability reliability, char orderingChannel)
{
    if (!bitStream)
        return false;

    return GetInterface()->Send(bitStream, priority, reliability, orderingChannel);
}

void* TERakClient::GetOriginalRaw(size_t index)
{
    if (!originalVtable)
        return nullptr;

    void* fn = originalVtable[index];
    return fn;
}