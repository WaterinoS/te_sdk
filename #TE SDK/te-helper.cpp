#include "te-sdk.h"
#include "FullRakNet/PacketEnumerations.h"

#include <atomic>
#include <mutex>
#include <cstring>
#include <cstdlib>

using namespace RakNet;

namespace te::sdk::helper
{
    // ------------------------------------------------------------------
    // Memory safety helpers
    // ------------------------------------------------------------------

    namespace
    {
        constexpr DWORD kReadableProtect =
            PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

        constexpr DWORD kWritableProtect =
            PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

        bool CheckRange(const void* ptr, size_t size, DWORD requiredProtect)
        {
            if (!ptr || size == 0)
                return false;

            const uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
            const uintptr_t end = start + size;
            if (end < start)   // address space wrap
                return false;

            uintptr_t cursor = start;
            while (cursor < end)
            {
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi))
                    return false;

                if (mbi.State != MEM_COMMIT)
                    return false;

                if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
                    return false;

                if ((mbi.Protect & requiredProtect) == 0)
                    return false;

                const uintptr_t regionEnd =
                    reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;

                // Defensive: a non-advancing region would spin forever
                if (regionEnd <= cursor)
                    return false;

                cursor = regionEnd;
            }

            return true;
        }
    }

    bool IsReadable(const void* ptr, size_t size)
    {
        return CheckRange(ptr, size, kReadableProtect);
    }

    bool IsWritable(const void* ptr, size_t size)
    {
        return CheckRange(ptr, size, kWritableProtect);
    }

    // ------------------------------------------------------------------
    // Version profiles
    //
    // Every samp.dll address the SDK touches lives in this one table. It used
    // to be spread over three places (an offsets array here, a second array in
    // te-samp.cpp with a different version ordering, and an inline switch in
    // AddChatMessage), which made adding a version a three-way edit.
    // ------------------------------------------------------------------

    namespace
    {
        constexpr int kProfileCount = static_cast<int>(SAMPVersion::Count);

        VersionProfile MakeProfile(
            uintptr_t sampInfo, uintptr_t chatInfo, uintptr_t inputInfo,
            uintptr_t fnAddToChatWnd, uintptr_t fnSendCommand, uintptr_t fnHandleRpcPacket,
            uintptr_t netGameRakClient, uintptr_t netGameHostname,
            uintptr_t netGameGameState, uintptr_t netGamePools,
            uintptr_t poolsPlayerPool, uintptr_t localId, uintptr_t localName,
            uintptr_t localPlayer, bool localNameIsStdString)
        {
            VersionProfile p{};
            p.sampInfo = sampInfo;
            p.chatInfo = chatInfo;
            p.inputInfo = inputInfo;
            p.fnAddToChatWnd = fnAddToChatWnd;
            p.fnSendCommand = fnSendCommand;
            p.fnHandleRpcPacket = fnHandleRpcPacket;
            p.netGameRakClient = netGameRakClient;
            p.netGameHostname = netGameHostname;
            p.netGameGameState = netGameGameState;
            p.netGamePools = netGamePools;
            p.poolsPlayerPool = poolsPlayerPool;
            p.playerPoolLocalId = localId;
            p.playerPoolLocalName = localName;
            p.playerPoolLocalPlayer = localPlayer;
            p.localNameIsStdString = localNameIsStdString;
            p.builtin = true;
            return p;
        }

        std::once_flag g_profileInitFlag;
        std::mutex g_profileMutex;
        VersionProfile g_profiles[kProfileCount];

        void InitProfiles()
        {
            //                             sampInfo    chatInfo    inputInfo   addToChat  sendCmd   handleRpc  rakCl  host   state  pools  plPool  id      name    player  stdStr
            g_profiles[static_cast<int>(SAMPVersion::R1)] =
                MakeProfile(0x21A0F8, 0x21A0E4, 0x21A0E8, 0x64010, 0x65C60, 0x372F0,
                            969, 0x121, 0x3BD, 0x3CD, 0x18, 0x4, 0xA, 0x22, true);

            g_profiles[static_cast<int>(SAMPVersion::R2)] =
                MakeProfile(0x21A100, 0x21A0EC, 0x21A0F0, 0x640E0, 0x65D30, 0x373D0,
                            24, 0x11D, 0x3B5, 0x3C5, 0x8, 0x0, 0x6, 0x1E, true);

            g_profiles[static_cast<int>(SAMPVersion::DL)] =
                MakeProfile(0x2ACA24, 0x2ACA10, 0x2ACA14, 0x67650, 0x69340, 0x3A8A0,
                            44, 0x131, 0x3D5, 0x3DE, 0x8, 0x2F1C, 0x2F20, 0x2F3A, false);

            g_profiles[static_cast<int>(SAMPVersion::R3)] =
                MakeProfile(0x26E8DC, 0x26E8C8, 0x26E8CC, 0x67460, 0x69190, 0x3A6A0,
                            44, 0x131, 0x3CD, 0x3DE, 0x8, 0x2F1C, 0x2F20, 0x2F3A, false);

            g_profiles[static_cast<int>(SAMPVersion::R4)] =
                MakeProfile(0x26EA0C, 0x26E9F8, 0x26E9FC, 0x67BA0, 0x698C0, 0x3AD90,
                            44, 0x131, 0x3CD, 0x3DE, 0x8, 0x2F1C, 0x2F20, 0x2F3A, false);

            // R4-2 shares R4's data layout but has no verified handle_rpc_packet
            // offset, so incoming RPC hooks stay off for it (0 = unavailable).
            g_profiles[static_cast<int>(SAMPVersion::R4v2)] =
                MakeProfile(0x26EA0C, 0x26E9F8, 0x26E9FC, 0x67BE0, 0x698C0, 0,
                            0, 0x131, 0x3CD, 0x3DE, 0x8, 0x2F1C, 0x2F20, 0x2F3A, false);

            g_profiles[static_cast<int>(SAMPVersion::R5)] =
                MakeProfile(0x26EB94, 0x26EB80, 0x26EB84, 0x67BE0, 0x69900, 0x3ADE0,
                            0, 0x131, 0x3CD, 0x3DE, 0x4, 0x2F1C, 0x2F20, 0x2F3A, false);
        }

        const VersionProfile* ProfileFor(SAMPVersion version)
        {
            std::call_once(g_profileInitFlag, InitProfiles);

            const int index = static_cast<int>(version);
            if (index <= static_cast<int>(SAMPVersion::Unknown) || index >= kProfileCount)
                return nullptr;

            const VersionProfile* profile = &g_profiles[index];
            // sampInfo is mandatory - an all-zero entry means "no profile"
            return profile->sampInfo != 0 ? profile : nullptr;
        }

        // Cached detection result. Stored as a single atomic so there is no
        // window where the "is cached" flag and the value disagree.
        std::atomic<SAMPVersion> g_cachedVersion{ SAMPVersion::Unknown };
        std::atomic<SAMPVersion> g_versionOverride{ SAMPVersion::Unknown };
        std::atomic<uint32_t> g_versionSignature{ 0 };

        uintptr_t GetModuleBase(const wchar_t* moduleName)
        {
            return reinterpret_cast<uintptr_t>(GetModuleHandleW(moduleName));
        }
    }

    bool RegisterVersionProfile(SAMPVersion version, const VersionProfile& profile)
    {
        std::call_once(g_profileInitFlag, InitProfiles);

        const int index = static_cast<int>(version);
        if (index <= static_cast<int>(SAMPVersion::Unknown) || index >= kProfileCount)
            return false;

        if (profile.sampInfo == 0)
            return false;

        std::lock_guard<std::mutex> lock(g_profileMutex);
        g_profiles[index] = profile;
        g_profiles[index].builtin = false;
        return true;
    }

    void SetSAMPVersionOverride(SAMPVersion version)
    {
        g_versionOverride.store(version, std::memory_order_relaxed);
        // Drop the cached detection so the override takes effect immediately
        g_cachedVersion.store(SAMPVersion::Unknown, std::memory_order_relaxed);
    }

    uintptr_t GetSAMPBase()
    {
        return GetModuleBase(L"samp.dll");
    }

    SAMPVersion GetSAMPVersion()
    {
        const SAMPVersion forced = g_versionOverride.load(std::memory_order_relaxed);
        if (forced != SAMPVersion::Unknown)
            return forced;

        const SAMPVersion cached = g_cachedVersion.load(std::memory_order_relaxed);
        if (cached != SAMPVersion::Unknown)
            return cached;

        const uintptr_t sampBase = GetSAMPBase();
        if (!sampBase)
            return SAMPVersion::Unknown;

        SAMPVersion detected = SAMPVersion::Unknown;

        const uint32_t val128 = ReadMemory<uint32_t>(sampBase + 0x128);
        switch (val128)
        {
        case 0x5542F47A: detected = SAMPVersion::R1; break;
        case 0x59C30C94: detected = SAMPVersion::R2; break;
        case 0x5A6A3130: detected = SAMPVersion::DL; break;
        default: break;
        }

        if (detected != SAMPVersion::Unknown)
        {
            g_versionSignature.store(val128, std::memory_order_relaxed);
        }
        else
        {
            const uint32_t val120 = ReadMemory<uint32_t>(sampBase + 0x120);
            switch (val120)
            {
            case 0x5C0B4243: detected = SAMPVersion::R3;   break;
            case 0x5DD606CD: detected = SAMPVersion::R4;   break;
            case 0x6094ACAB: detected = SAMPVersion::R4v2; break;
            case 0x6372C39E: detected = SAMPVersion::R5;   break;
            default: break;
            }
            g_versionSignature.store(detected != SAMPVersion::Unknown ? val120 : val128,
                                     std::memory_order_relaxed);
        }

        // Only cache a real hit - samp.dll may still be initialising
        if (detected != SAMPVersion::Unknown)
            g_cachedVersion.store(detected, std::memory_order_relaxed);

        return detected;
    }

    uint32_t GetSAMPVersionSignature()
    {
        GetSAMPVersion();
        return g_versionSignature.load(std::memory_order_relaxed);
    }

    const VersionProfile* GetVersionProfile(SAMPVersion version)
    {
        return ProfileFor(version);
    }

    const VersionProfile* GetVersionProfile()
    {
        return ProfileFor(GetSAMPVersion());
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

    void* GetSAMPInfo()
    {
        const uintptr_t sampBase = GetSAMPBase();
        if (!sampBase)
            return nullptr;

        const VersionProfile* profile = GetVersionProfile();
        if (!profile)
            return nullptr;

        return ReadMemory<void*>(sampBase + profile->sampInfo);
    }

    void* GetRakNetInterface()
    {
        void* sampInfo = GetSAMPInfo();
        if (!sampInfo)
            return nullptr;

        const VersionProfile* profile = GetVersionProfile();
        if (!profile)
            return nullptr;

        // Address of the slot holding the RakClientInterface pointer - the
        // caller dereferences it.
        void* slot = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(sampInfo) + profile->netGameRakClient);

        return IsReadable(slot, sizeof(void*)) ? slot : nullptr;
    }

    std::uintptr_t GetHandleRpcPacketAddress()
    {
        const VersionProfile* profile = GetVersionProfile();
        if (!profile || profile->fnHandleRpcPacket == 0)
            return 0;

        const uintptr_t base = GetSAMPBase();
        if (!base)
            return 0;

        return base + profile->fnHandleRpcPacket;
    }

    // ------------------------------------------------------------------
    // Pattern scanning
    // ------------------------------------------------------------------

    namespace
    {
        struct PatternByte
        {
            uint8_t value;
            bool wildcard;
        };

        bool ParsePattern(const char* pattern, std::vector<PatternByte>& out)
        {
            out.clear();
            if (!pattern)
                return false;

            for (const char* cursor = pattern; *cursor;)
            {
                if (*cursor == ' ')
                {
                    ++cursor;
                    continue;
                }

                if (*cursor == '?')
                {
                    ++cursor;
                    if (*cursor == '?')
                        ++cursor;
                    out.push_back({ 0, true });
                    continue;
                }

                char* end = nullptr;
                const unsigned long value = strtoul(cursor, &end, 16);
                if (end == cursor || value > 0xFF)
                    return false;

                out.push_back({ static_cast<uint8_t>(value), false });
                cursor = end;
            }

            return !out.empty();
        }

        // Image size straight out of the PE headers - avoids a psapi dependency.
        size_t GetImageSize(uintptr_t moduleBase)
        {
            if (!IsReadable(reinterpret_cast<const void*>(moduleBase), sizeof(IMAGE_DOS_HEADER)))
                return 0;

            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
                return 0;

            const uintptr_t ntAddress = moduleBase + dos->e_lfanew;
            if (!IsReadable(reinterpret_cast<const void*>(ntAddress), sizeof(IMAGE_NT_HEADERS)))
                return 0;

            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(ntAddress);
            if (nt->Signature != IMAGE_NT_SIGNATURE)
                return 0;

            return nt->OptionalHeader.SizeOfImage;
        }
    }

    uintptr_t FindPattern(const char* moduleName, const char* pattern)
    {
        if (!moduleName)
            return 0;

        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(moduleName));
        if (!base)
            return 0;

        std::vector<PatternByte> bytes;
        if (!ParsePattern(pattern, bytes))
            return 0;

        const size_t imageSize = GetImageSize(base);
        if (imageSize < bytes.size())
            return 0;

        const size_t lastStart = imageSize - bytes.size();

        for (size_t offset = 0; offset <= lastStart; ++offset)
        {
            const uintptr_t candidate = base + offset;

            // Skip over unreadable stretches instead of faulting on them
            if (!IsReadable(reinterpret_cast<const void*>(candidate), bytes.size()))
                continue;

            const auto* memory = reinterpret_cast<const uint8_t*>(candidate);

            bool matched = true;
            for (size_t i = 0; i < bytes.size(); ++i)
            {
                if (!bytes[i].wildcard && memory[i] != bytes[i].value)
                {
                    matched = false;
                    break;
                }
            }

            if (matched)
                return candidate;
        }

        return 0;
    }

    // ------------------------------------------------------------------
    // RPC parsing
    // ------------------------------------------------------------------

    bool ExtractRPCData(const char* data, int len, ExtractedRPC& out)
    {
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

        if (out.reliability == UNRELIABLE_SEQUENCED ||
            out.reliability == RELIABLE_SEQUENCED ||
            out.reliability == RELIABLE_ORDERED)
        {
            if (!bitStream.ReadBits(reinterpret_cast<unsigned char*>(&out.orderingChannel), 5))
                return false;
            if (!bitStream.Read(reinterpret_cast<char*>(&out.orderingIndex), sizeof(out.orderingIndex)))
                return false;
        }

        if (!bitStream.Read(out.isSplitPacket))
            return false;

        if (out.isSplitPacket)
        {
            // Read split packet information (NO total length!)
            if (!bitStream.Read(out.splitPacketId))
                return false;
            if (!bitStream.ReadCompressed(out.splitPacketIndex))
                return false;
            if (!bitStream.ReadCompressed(out.splitPacketCount))
                return false;

            // Read fragment data length (in bits)
            if (!bitStream.ReadCompressed(out.length))
                return false;

            std::vector<uint8_t> packetData((out.length + 7) / 8, 0);
            if (packetData.empty())
                return false;
            if (!bitStream.ReadAlignedBytes(packetData.data(), static_cast<int>(packetData.size())))
                return false;

            out.payload = std::move(packetData);

            out.packetId = 0;
            out.rpcId = 0;
        }
        else
        {
            if (!bitStream.ReadCompressed(out.length))
                return false;

            std::vector<uint8_t> packetData((out.length + 7) / 8, 0);
            if (packetData.empty())
                return false;
            if (!bitStream.ReadAlignedBytes(packetData.data(), static_cast<int>(packetData.size())))
                return false;

            BitStream dataBitStream(packetData.data(),
                                    static_cast<unsigned int>(packetData.size()), false);

            if (!dataBitStream.Read(out.packetId))
                return false;

            if (out.packetId != ID_RPC)
                return false;

            if (!dataBitStream.Read(out.rpcId))
                return false;

            const int unreadBytes = dataBitStream.GetNumberOfUnreadBits() / 8;
            if (unreadBytes > 0)
            {
                out.payload.resize(static_cast<size_t>(unreadBytes));
                if (!dataBitStream.Read(reinterpret_cast<char*>(out.payload.data()), unreadBytes))
                    return false;
            }
            else
            {
                out.payload.clear();
            }
        }

        return true;
    }
}
