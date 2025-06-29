#pragma once

#include <cstdint>

namespace te_sdk::helper
{
    enum class SAMPVersion
    {
        Unknown = 0,
        R1,
        R2,
        DL,
        R3,
        R4,
        R4v2,
        R5
    };

    struct ExtractedRPC {
        bool hasAcks = false;
        uint16_t msgNum = 0;
        uint8_t reliability = 0;
        uint8_t orderingChannel = 0;
        uint16_t orderingIndex = 0;
        bool isSplitPacket = false;
        uint16_t length = 0;
        BYTE packetId = 0;
        BYTE rpcId = 0;
        std::vector<BYTE> payload;

        // Split packet properties
        uint16_t splitPacketId = 0;
        uint32_t splitPacketIndex = 0;
        uint32_t splitPacketCount = 0;
        uint32_t splitPacketTotalLength = 0;
        uint16_t maxSplitPacketSize = 0;
    };

    // Get current detected SAMP version
    SAMPVersion GetSAMPVersion();

    // Returns pointer to SAMP_INFO struct
    void* GetSAMPInfo();

    // Returns RakClientInterface pointer based on version
    void* GetRakNetInterface();

	// Initialize WSA hooks
    bool AttachWSAHooks();

    std::string TranslateSAMPVersion(SAMPVersion version);

	// Extract RPC data from a given data buffer
    bool ExtractRPCData(const char* data, int len, ExtractedRPC& out);
}