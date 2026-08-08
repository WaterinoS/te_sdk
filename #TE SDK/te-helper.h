#pragma once

// Self-contained: do not rely on te-inc.h having been included first.
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace te::sdk::helper
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
        R5,

        // Not a real version - marks the end of the profile table
        Count
    };

    // Every address the SDK needs for a given SA-MP build, in one place.
    // Offsets marked "samp.dll" are relative to the samp.dll base address;
    // the rest are relative to the struct named in the comment.
    struct VersionProfile
    {
        // --- samp.dll relative ---
        uintptr_t sampInfo          = 0;  // CNetGame**
        uintptr_t chatInfo          = 0;  // CChat**
        uintptr_t inputInfo         = 0;  // CInput**
        uintptr_t fnAddToChatWnd    = 0;
        uintptr_t fnSendCommand     = 0;
        uintptr_t fnHandleRpcPacket = 0;  // 0 => incoming RPC hook unavailable

        // --- CNetGame relative ---
        uintptr_t netGameRakClient  = 0;  // slot holding RakClientInterface*
        uintptr_t netGameHostname   = 0;
        uintptr_t netGameGameState  = 0;
        uintptr_t netGamePools      = 0;

        // --- pool relative ---
        uintptr_t poolsPlayerPool     = 0;
        uintptr_t playerPoolLocalId   = 0;
        uintptr_t playerPoolLocalName = 0;
        uintptr_t playerPoolLocalPlayer = 0;

        // R1/R2 store the local player name as an MSVC std::string,
        // R3+ and DL use a plain fixed char buffer.
        bool localNameIsStdString = false;

        // Set for entries the SDK ships with; a profile installed through
        // RegisterVersionProfile() leaves this false.
        bool builtin = false;
    };

    // ---- memory safety helpers ----

    // True when the whole range [ptr, ptr + size) is committed and readable.
    // Unlike a single VirtualQuery this walks region boundaries, so a range
    // straddling the end of a committed block is correctly rejected.
    bool IsReadable(const void* ptr, size_t size);

    // Same, but the range must also be writable.
    bool IsWritable(const void* ptr, size_t size);

    // Safe memory read - returns T{} when the address is not readable
    template<typename T>
    T ReadMemory(uintptr_t address)
    {
        if (!IsReadable(reinterpret_cast<const void*>(address), sizeof(T)))
            return T{};
        return *reinterpret_cast<const T*>(address);
    }

    // Safe memory write - returns true on success
    template<typename T>
    bool WriteMemory(uintptr_t address, T value)
    {
        void* ptr = reinterpret_cast<void*>(address);
        if (!IsReadable(ptr, sizeof(T)))
            return false;

        DWORD oldProtect = 0;
        if (!VirtualProtect(ptr, sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        *reinterpret_cast<T*>(address) = value;

        DWORD restored = 0;
        VirtualProtect(ptr, sizeof(T), oldProtect, &restored);
        return true;
    }

    // ---- version detection ----

    // Get samp.dll base address (0 when samp.dll is not loaded)
    uintptr_t GetSAMPBase();

    // Get current detected SAMP version. The result is cached after the first
    // successful detection.
    SAMPVersion GetSAMPVersion();

    // The raw build stamp read out of samp.dll during detection. Useful when
    // reporting an unrecognised build so a profile can be added for it.
    uint32_t GetSAMPVersionSignature();

    std::string TranslateSAMPVersion(SAMPVersion version);

    // Offsets in use for the detected version, or nullptr when the version is
    // unknown / has no profile installed.
    const VersionProfile* GetVersionProfile();
    const VersionProfile* GetVersionProfile(SAMPVersion version);

    // Install or replace the offset profile for a version. This is the escape
    // hatch for running on a SA-MP build the shipped table does not cover:
    // register a profile, force the version with SetSAMPVersionOverride(), and
    // the whole SDK picks up the new addresses without a rebuild.
    // Must be called before InitRakNetHooks().
    bool RegisterVersionProfile(SAMPVersion version, const VersionProfile& profile);

    // Force the detected version instead of sniffing samp.dll. Pass
    // SAMPVersion::Unknown to go back to automatic detection.
    void SetSAMPVersionOverride(SAMPVersion version);

    // Returns pointer to the CNetGame (SAMP_INFO) instance
    void* GetSAMPInfo();

    // Returns the address of the slot holding the RakClientInterface pointer
    void* GetRakNetInterface();

    std::uintptr_t GetHandleRpcPacketAddress();

    // ---- pattern scanning ----

    // IDA-style signature scan over a module's executable image, e.g.
    //   FindPattern("samp.dll", "55 8B EC 83 EC ?? 53 56 57")
    // '?' and '??' match any byte. Returns 0 when not found.
    uintptr_t FindPattern(const char* moduleName, const char* pattern);

    // ---- RPC parsing ----

    struct ExtractedRPC
    {
        bool hasAcks = false;
        uint16_t msgNum = 0;
        uint8_t reliability = 0;
        uint8_t orderingChannel = 0;
        uint16_t orderingIndex = 0;
        bool isSplitPacket = false;
        uint16_t length = 0;
        uint8_t packetId = 0;
        uint8_t rpcId = 0;
        std::vector<uint8_t> payload;

        // Split packet properties
        uint16_t splitPacketId = 0;
        uint32_t splitPacketIndex = 0;
        uint32_t splitPacketCount = 0;
        uint32_t splitPacketTotalLength = 0;
        uint16_t maxSplitPacketSize = 0;
    };

    // Extract RPC data from a raw RakNet datagram
    bool ExtractRPCData(const char* data, int len, ExtractedRPC& out);
}
