#pragma once

#include <cstdint>

namespace te::sdk::helper
{
    // Get samp.dll base address
    uintptr_t GetSAMPBase();

    // Safe memory read - returns T{} on failure
    template<typename T>
    T ReadMemory(uintptr_t address)
    {
        void* ptr = reinterpret_cast<void*>(address);
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(ptr, &mbi, sizeof(mbi)))
        {
            DWORD protect = mbi.Protect & 0xFF;
            bool readable = (protect == PAGE_READONLY) ||
                            (protect == PAGE_READWRITE) ||
                            (protect == PAGE_EXECUTE_READ) ||
                            (protect == PAGE_EXECUTE_READWRITE);
            if (readable)
                return *reinterpret_cast<T*>(address);
        }

        DWORD oldProtect;
        if (!VirtualProtect(ptr, sizeof(T), PAGE_EXECUTE_READ, &oldProtect))
            return T{};
        T value = *reinterpret_cast<T*>(address);
        VirtualProtect(ptr, sizeof(T), oldProtect, &oldProtect);
        return value;
    }

    // Safe memory write - returns true on success
    template<typename T>
    bool WriteMemory(uintptr_t address, T value)
    {
        void* ptr = reinterpret_cast<void*>(address);
        DWORD oldProtect;
        if (!VirtualProtect(ptr, sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;
        *reinterpret_cast<T*>(address) = value;
        VirtualProtect(ptr, sizeof(T), oldProtect, &oldProtect);
        return true;
    }

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

    std::string TranslateSAMPVersion(SAMPVersion version);

	// Extract RPC data from a given data buffer
    bool ExtractRPCData(const char* data, int len, ExtractedRPC& out);

    std::uintptr_t GetHandleRpcPacketAddress();
}