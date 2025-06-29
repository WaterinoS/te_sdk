#include "te-sdk.h"

#include "Detours/detours_x86.h"
#pragma comment(lib, "Detours/detours_x86.lib")

namespace te_sdk::helper
{
	using namespace te_sdk::helper::logging;

    static uintptr_t GetModuleBase(const wchar_t* moduleName)
    {
        return reinterpret_cast<uintptr_t>(GetModuleHandleW(moduleName));
    }

    static uint32_t ReadDWORD(uintptr_t address)
    {
        DWORD oldProtect;
        VirtualProtect(reinterpret_cast<void*>(address), 4, PAGE_EXECUTE_READWRITE, &oldProtect);
        uint32_t value = *reinterpret_cast<uint32_t*>(address);
        VirtualProtect(reinterpret_cast<void*>(address), 4, oldProtect, &oldProtect);
        return value;
    }

    SAMPVersion GetSAMPVersion()
    {
        uintptr_t sampBase = GetModuleBase(L"samp.dll");
        if (!sampBase)
            return SAMPVersion::Unknown;

        uint32_t val128 = ReadDWORD(sampBase + 0x128);
        switch (val128)
        {
        case 0x5542F47A: return SAMPVersion::R1;
        case 0x59C30C94: return SAMPVersion::R2;
        case 0x5A6A3130: return SAMPVersion::DL;
        }

        uint32_t val120 = ReadDWORD(sampBase + 0x120);
        switch (val120)
        {
        case 0x5C0B4243: return SAMPVersion::R3;
        case 0x5DD606CD: return SAMPVersion::R4;
        case 0x6094ACAB: return SAMPVersion::R4v2;
        case 0x6372C39E: return SAMPVersion::R5;
        }

        return SAMPVersion::Unknown;
    }

    void* GetSAMPInfo()
    {
        uintptr_t sampBase = GetModuleBase(L"samp.dll");
        if (!sampBase)
            return nullptr;

        SAMPVersion ver = GetSAMPVersion();

        uintptr_t offset = 0;
        switch (ver)
        {
        case SAMPVersion::R1:    offset = 0x21A0F8; break;
        case SAMPVersion::R2:    offset = 0x21A100; break;
        case SAMPVersion::DL:    offset = 0x2ACA24; break;
        case SAMPVersion::R3:    offset = 0x26E8DC; break;
        case SAMPVersion::R4:    offset = 0x26EA0C; break;
        case SAMPVersion::R4v2:  offset = 0x26EA0C; break;
        case SAMPVersion::R5:    offset = 0x26EB94; break;
        default: return nullptr;
        }

        return *reinterpret_cast<void**>(sampBase + offset);
    }

    void* GetRakNetInterface()
    {
        void* sampInfo = GetSAMPInfo();
        if (!sampInfo)
            return nullptr;

        SAMPVersion ver = GetSAMPVersion();
        uintptr_t offset = 0;

        switch (ver)
        {
        case SAMPVersion::R1:    offset = 969; break;
        case SAMPVersion::R2:    offset = 24; break;
        case SAMPVersion::R3:    offset = 44; break;
        case SAMPVersion::R4:    offset = 44; break;
        case SAMPVersion::R4v2:  offset = 0; break;
        case SAMPVersion::R5:    offset = 0; break;
        case SAMPVersion::DL:    offset = 44; break;
        default: return nullptr;
        }

        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(sampInfo) + offset);
    }

    std::string TranslateSAMPVersion(SAMPVersion version)
    {
        switch (version)
        {
        case SAMPVersion::R1:    return "0.3.7-R1";
        case SAMPVersion::R2:    return "0.3.7-R2";
        case SAMPVersion::DL:    return "0.3.DL-R1";
        case SAMPVersion::R3:    return "0.3.7-R3";
        case SAMPVersion::R4:    return "0.3.7-R4";
        case SAMPVersion::R4v2:  return "0.3.7-R4-2";
        case SAMPVersion::R5:    return "0.3.7-R5";
        default:                 return "Unknown";
        }
    }

    bool ExtractRPCData(const char* data, int len, ExtractedRPC& out) {
        if (!data || len <= 0)
            return false;

        BitStream bitStream(reinterpret_cast<unsigned char*>(const_cast<char*>(data)), len, false);

        if (!bitStream.Read(out.hasAcks))
            return false;

        if (!bitStream.Read(out.msgNum))
            return false;

        uint8_t reliabilityBits = 0;
        if (!bitStream.ReadBits(reinterpret_cast<unsigned char*>(&reliabilityBits), 4))
            return false;
        out.reliability = reliabilityBits;

        if (out.reliability == UNRELIABLE_SEQUENCED || out.reliability == RELIABLE_SEQUENCED || out.reliability == RELIABLE_ORDERED) {
            if (!bitStream.ReadBits(reinterpret_cast<unsigned char*>(&out.orderingChannel), 5))
                return false;
            if (!bitStream.Read(reinterpret_cast<char*>(&out.orderingIndex), sizeof(out.orderingIndex)))
                return false;
        }

        if (!bitStream.Read(out.isSplitPacket))
            return false;

        if (out.isSplitPacket) {
            // Read split packet information (NO total length!)
            if (!bitStream.Read(out.splitPacketId))
                return false;
            if (!bitStream.ReadCompressed(out.splitPacketIndex))
                return false;
            if (!bitStream.ReadCompressed(out.splitPacketCount))
                return false;

            // Read fragment data length
            if (!bitStream.ReadCompressed(out.length))
                return false;

            // Read fragment data
            std::vector<BYTE> packetData((out.length + 7) / 8, 0);
            if (!bitStream.ReadAlignedBytes(packetData.data(), packetData.size()))
                return false;

            out.payload = packetData;

            out.packetId = 0;
            out.rpcId = 0;
        }
        else {
            if (!bitStream.ReadCompressed(out.length))
                return false;

            std::vector<BYTE> packetData((out.length + 7) / 8, 0);
            if (!bitStream.ReadAlignedBytes(packetData.data(), packetData.size()))
                return false;

            BitStream dataBitStream(packetData.data(), packetData.size(), false);

            if (!dataBitStream.Read(out.packetId))
                return false;

            if (out.packetId != 20)
                return false;

            if (!dataBitStream.Read(out.rpcId))
                return false;

            int unreadBits = dataBitStream.GetNumberOfUnreadBits();
            int unreadBytes = unreadBits / 8;
            if (unreadBytes > 0) {
                out.payload.resize(unreadBytes);
                if (!dataBitStream.Read(reinterpret_cast<char*>(out.payload.data()), unreadBytes))
                    return false;
            }
        }

        return true;
    }

    bool AttachWSAHooks()
    {
        HMODULE ws2_32 = GetModuleHandleW(L"ws2_32.dll");
        if (!ws2_32) return false;

        oWSARecvFrom = reinterpret_cast<tWSARecvFrom>(GetProcAddress(ws2_32, "WSARecvFrom"));

        if (!oWSARecvFrom) return false;

        LONG error = DetourTransactionBegin();
        if (error != NO_ERROR) return false;

        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)oWSARecvFrom, &te_sdk::hkWSARecvFrom);

        error = DetourTransactionCommit();
        if (error != NO_ERROR) {
            DetourTransactionAbort();
            return false;
        }

        Log("[te_sdk] WSA hooks attached successfully");
        return true;
    }
}

