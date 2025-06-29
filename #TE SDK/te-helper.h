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

    // Get current detected SAMP version
    SAMPVersion GetSAMPVersion();

    // Returns pointer to SAMP_INFO struct
    void* GetSAMPInfo();

    // Returns RakClientInterface pointer based on version
    void* GetRakNetInterface();
}