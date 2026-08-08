#include "te-sdk.h"

using namespace RakNet;

// The SA-MP RakClient vtable has 55 entries (indices 0..54); anything past
// that is a bug in the SDK, not something to dereference and find out.
static constexpr size_t kMaxVtableIndex = 54;

TERakClient::TERakClient(void* rawInterface, void** originalVtable)
{
    this->raw = rawInterface;
    this->originalVtable = originalVtable;
}

bool TERakClient::IsValid() const
{
    return raw != nullptr && originalVtable != nullptr;
}

bool TERakClient::SendRPC(int rpcId, BitStream* bitStream, PacketPriority priority,
    PacketReliability reliability, char orderingChannel, bool shiftTimestamp)
{
    if (!bitStream || !IsValid())
        return false;

    return GetInterface()->RPC(&rpcId, bitStream, priority, reliability, orderingChannel, shiftTimestamp);
}

bool TERakClient::SendPacket(BitStream* bitStream, PacketPriority priority,
    PacketReliability reliability, char orderingChannel)
{
    if (!bitStream || !IsValid())
        return false;

    return GetInterface()->Send(bitStream, priority, reliability, orderingChannel);
}

void* TERakClient::GetOriginalRaw(size_t index)
{
    if (!originalVtable || index > kMaxVtableIndex)
        return nullptr;

    if (!te::sdk::helper::IsReadable(&originalVtable[index], sizeof(void*)))
        return nullptr;

    return originalVtable[index];
}
