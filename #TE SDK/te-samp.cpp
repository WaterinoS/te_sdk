#include "te-sdk.h"
#include <MinHook.h>
#include <cctype>

namespace te::sdk::helper::samp
{
    using namespace te::sdk::helper::logging;

    // ---- Offset Table ----

    struct SAMPOffsets {
        uintptr_t pInput;
        uintptr_t szHostname;
        uintptr_t iGameState;
        uintptr_t pPools;
        uintptr_t poolsPlayerPool;
        uintptr_t playerPoolLocalId;
        uintptr_t playerPoolLocalName;
        uintptr_t playerPoolLocalPlayer;
        uintptr_t fnSendCmd;
    };

    static const SAMPOffsets g_offsets[] = {
        // R1
        { 0x21A0E8, 0x121, 0x3BD, 0x3CD, 0x18, 0x4,    0xA,    0x22,   0x65C60 },
        // R2
        { 0x21A0F0, 0x11D, 0x3B5, 0x3C5, 0x8,  0x0,    0x6,    0x1E,   0x65D30 },
        // DL
        { 0x2ACA14, 0x131, 0x3D5, 0x3DE, 0x8,  0x2F1C, 0x2F20, 0x2F3A, 0x69340 },
        // R3
        { 0x26E8CC, 0x131, 0x3CD, 0x3DE, 0x8,  0x2F1C, 0x2F20, 0x2F3A, 0x69190 },
        // R4
        { 0x26E9FC, 0x131, 0x3CD, 0x3DE, 0x8,  0x2F1C, 0x2F20, 0x2F3A, 0x698C0 },
        // R4v2
        { 0x26E9FC, 0x131, 0x3CD, 0x3DE, 0x8,  0x2F1C, 0x2F20, 0x2F3A, 0x698C0 },
        // R5. The three CPlayerPool offsets below USED TO BE 0x2F1C / 0x2F20 / 0x2F3A -
        // R3/R4 values carried forward unchanged. They are wrong: R5 puts the local block
        // back at the FRONT of CPlayerPool, and 0x2F1C..0x2F3A land in the tail of the
        // CRemotePlayer*[1004] array (0x1F8A..0x2F3A) and on m_nLargestId respectively.
        // The practical damage was silent and severe: GetPlayerId() read a misaligned u16
        // out of the part of the array no real server fills, so it returned a CONSTANT 0 -
        // every caller that asked "which player am I" was told "player 0" for the whole
        // session, and per-player features (weapon skins, nameplate self-suppression,
        // lagcomp self-exclusion) acted on somebody else's identity.
        //
        // Replacements are disassembled from the shipped 0.3.7-R5 samp.dll, not inferred:
        //   0x04 localId     - CPlayerPool::SetPlayerScore (rva 0xE4B0) and ::SetPlayerPing
        //                      (rva 0xE4F0) both open with `cmp ax, [ecx+4]` and take the
        //                      local-block path when it matches; getter at rva 0x2DB0.
        //   0x0A localName   - the constructor (rva 0x13FD0) initialises an MSVC std::string
        //                      there: capacity 15 at +0x1E, size 0 at +0x1A, buf[0]=0 at
        //                      +0x0A. Note it is a std::string on R5, like R1/R2 - NOT the
        //                      fixed char buffer R3/R4/DL use (see GetPlayerName below).
        //   0x26 localPlayer - the same constructor stores the freshly-new'd CLocalPlayer*
        //                      (allocation size 0x324) at +0x26.
        { 0x26EB84, 0x131, 0x3CD, 0x3DE, 0x4,  0x04,   0x0A,   0x26,   0x69900 },
    };

    // Maps SAMPVersion enum to g_offsets index
    static int VersionToIndex(te::sdk::helper::SAMPVersion ver)
    {
        switch (ver)
        {
        case SAMPVersion::R1:   return 0;
        case SAMPVersion::R2:   return 1;
        case SAMPVersion::DL:   return 2;
        case SAMPVersion::R3:   return 3;
        case SAMPVersion::R4:   return 4;
        case SAMPVersion::R4v2: return 5;
        case SAMPVersion::R5:   return 6;
        default:                return -1;
        }
    }

    // ---- Internal Helpers ----

    static const SAMPOffsets* GetOffsets()
    {
        int idx = VersionToIndex(GetSAMPVersion());
        if (idx < 0) return nullptr;
        return &g_offsets[idx];
    }

    static bool IsValidPtr(void* ptr)
    {
        if (!ptr) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) return false;
        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
        return (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0;
    }

    static void* GetPlayerPool()
    {
        const SAMPOffsets* offsets = GetOffsets();
        if (!offsets) return nullptr;

        void* netGame = GetSAMPInfo();
        if (!netGame) return nullptr;

        uintptr_t pPoolsAddr = reinterpret_cast<uintptr_t>(netGame) + offsets->pPools;
        if (!IsValidPtr(reinterpret_cast<void*>(pPoolsAddr))) return nullptr;

        void* pools = *reinterpret_cast<void**>(pPoolsAddr);
        if (!IsValidPtr(pools)) return nullptr;

        uintptr_t pPlayerPoolAddr = reinterpret_cast<uintptr_t>(pools) + offsets->poolsPlayerPool;
        if (!IsValidPtr(reinterpret_cast<void*>(pPlayerPoolAddr))) return nullptr;

        void* playerPool = *reinterpret_cast<void**>(pPlayerPoolAddr);
        return IsValidPtr(playerPool) ? playerPool : nullptr;
    }

    // Read MSVC x86 std::string from memory (R1/R2/R5 - see GetPlayerName)
    // Layout: [16-byte SSO buffer][4-byte size][4-byte capacity] = 24 bytes
    static const char* ReadStdString(uintptr_t addr)
    {
        if (!IsValidPtr(reinterpret_cast<void*>(addr))) return "";

        uint32_t capacity = *reinterpret_cast<uint32_t*>(addr + 20);
        if (capacity <= 15)
        {
            // SSO: string data is inline at addr
            return reinterpret_cast<const char*>(addr);
        }
        else
        {
            // Heap-allocated: first pointer in the buffer is the data pointer
            const char* heapPtr = *reinterpret_cast<const char**>(addr);
            return IsValidPtr(const_cast<char*>(heapPtr)) ? heapPtr : "";
        }
    }

    // ---- RegisterChatCommand Hook Infrastructure ----

    using tSendCmd = void(__thiscall*)(void* pInput, const char* command);
    static std::map<std::string, ChatCommandCallback> g_registeredCommands;
    static std::mutex g_cmdMutex;
    static tSendCmd oSendCmd = nullptr;
    static bool g_cmdHookInstalled = false;

    static void __fastcall hkSendCmd(void* pInput, void* /*edx*/, const char* command)
    {
        if (command && command[0] == '/')
        {
            // Parse command name (first word after '/')
            const char* cmdStart = command + 1;
            const char* space = cmdStart;
            while (*space && *space != ' ') ++space;

            std::string cmdName(cmdStart, space);
            // Lowercase for case-insensitive matching
            for (auto& c : cmdName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            std::lock_guard<std::mutex> lock(g_cmdMutex);
            auto it = g_registeredCommands.find(cmdName);
            if (it != g_registeredCommands.end())
            {
                // Skip whitespace after command name to get params
                const char* params = space;
                while (*params == ' ') ++params;

                it->second(params);
                return; // Don't forward to server
            }
        }

        // Not a registered command, forward to original
        oSendCmd(pInput, command);
    }

    static bool InstallCmdHook()
    {
        const SAMPOffsets* offsets = GetOffsets();
        if (!offsets) return false;

        uintptr_t sampBase = GetSAMPBase();
        if (!sampBase) return false;

        void* fnTarget = reinterpret_cast<void*>(sampBase + offsets->fnSendCmd);

        if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED)
        {
            Log("[RegisterChatCommand] Failed to initialize MinHook");
            return false;
        }

        if (MH_CreateHook(fnTarget, &hkSendCmd, reinterpret_cast<void**>(&oSendCmd)) != MH_OK)
        {
            Log("[RegisterChatCommand] Failed to create hook on SendCommand");
            return false;
        }

        if (MH_EnableHook(fnTarget) != MH_OK)
        {
            Log("[RegisterChatCommand] Failed to enable SendCommand hook");
            return false;
        }

        g_cmdHookInstalled = true;
        return true;
    }

    // ---- Public Functions ----

    bool AddChatMessage(const char* text, uint32_t color)
    {
        if (!text)
        {
            Log("[SendChatMessage] Invalid text parameter");
            return false;
        }

        HMODULE sampModule = GetModuleHandleA("samp.dll");
        if (!sampModule)
        {
            Log("[SendChatMessage] samp.dll not found");
            return false;
        }

        SAMPVersion sampVersion = GetSAMPVersion();

        uintptr_t sampBase = reinterpret_cast<uintptr_t>(sampModule);
        uintptr_t chatInfoOffset = 0;
        uintptr_t addToChatWndOffset = 0;

        switch (sampVersion)
        {
        case SAMPVersion::R1:
            chatInfoOffset = 0x21A0E4;
            addToChatWndOffset = 0x64010;
            break;
        case SAMPVersion::R2:
            chatInfoOffset = 0x21A0EC;
            addToChatWndOffset = 0x640E0;
            break;
        case SAMPVersion::DL:
            chatInfoOffset = 0x2ACA10;
            addToChatWndOffset = 0x67650;
            break;
        case SAMPVersion::R3:
            chatInfoOffset = 0x26E8C8;
            addToChatWndOffset = 0x67460;
            break;
        case SAMPVersion::R4:
            chatInfoOffset = 0x26E9F8;
            addToChatWndOffset = 0x67BA0;
            break;
        case SAMPVersion::R4v2:
            chatInfoOffset = 0x26E9F8;
            addToChatWndOffset = 0x67BE0;
            break;
        case SAMPVersion::R5:
            chatInfoOffset = 0x26EB80;
            addToChatWndOffset = 0x67BE0;
            break;
        case SAMPVersion::Unknown:
        default:
            Log("[SendChatMessage] Unknown or unsupported SAMP version");
            return false;
        }

        try
        {
            uintptr_t* pChatInfo = reinterpret_cast<uintptr_t*>(sampBase + chatInfoOffset);

            MEMORY_BASIC_INFORMATION mbi{};
            bool isReadable = pChatInfo != nullptr &&
                VirtualQuery(pChatInfo, &mbi, sizeof(mbi)) &&
                (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
                !(mbi.Protect & PAGE_GUARD) &&
                !(mbi.Protect & PAGE_NOACCESS);

            if (!isReadable || !*pChatInfo)
            {
                Log("[SendChatMessage] Invalid chat info pointer for version %s",
                    TranslateSAMPVersion(sampVersion).c_str());
                return false;
            }

            uintptr_t chatInfo = *pChatInfo;
            uintptr_t addToChatWndFunc = sampBase + addToChatWndOffset;

            using AddToChatWndFunc = void(__thiscall*)(void*, int, const char*, const char*, uint32_t, uint32_t);
            auto addToChatWnd = reinterpret_cast<AddToChatWndFunc>(addToChatWndFunc);

            addToChatWnd(reinterpret_cast<void*>(chatInfo), 8, text, "", color, color);
            return true;
        }
        catch (const std::exception& e)
        {
            Log("[SendChatMessage] Exception occurred: %s", e.what());
            return false;
        }
        catch (...)
        {
            Log("[SendChatMessage] Unknown exception occurred");
            return false;
        }
    }

    bool SendCommand(const char* command)
    {
        if (!command)
        {
            Log("[SendCommand] Invalid command parameter");
            return false;
        }

        const SAMPOffsets* offsets = GetOffsets();
        if (!offsets)
        {
            Log("[SendCommand] Unsupported SAMP version");
            return false;
        }

        uintptr_t sampBase = GetSAMPBase();
        if (!sampBase)
        {
            Log("[SendCommand] samp.dll not found");
            return false;
        }

        // Get CInput pointer
        uintptr_t pInputAddr = sampBase + offsets->pInput;
        if (!IsValidPtr(reinterpret_cast<void*>(pInputAddr))) return false;

        void* pInput = *reinterpret_cast<void**>(pInputAddr);
        if (!IsValidPtr(pInput)) return false;

        // If the hook is installed, call through the original to avoid re-entering our hook
        if (g_cmdHookInstalled && oSendCmd)
        {
            oSendCmd(pInput, command);
        }
        else
        {
            auto fnSendCmd = reinterpret_cast<tSendCmd>(sampBase + offsets->fnSendCmd);
            fnSendCmd(pInput, command);
        }

        return true;
    }

    const char* GetServerName()
    {
        const SAMPOffsets* offsets = GetOffsets();
        if (!offsets) return "";

        void* netGame = GetSAMPInfo();
        if (!netGame) return "";

        return reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(netGame) + offsets->szHostname);
    }

    const char* GetPlayerName()
    {
        const SAMPOffsets* offsets = GetOffsets();
        if (!offsets) return "";

        void* playerPool = GetPlayerPool();
        if (!playerPool) return "";

        uintptr_t nameAddr = reinterpret_cast<uintptr_t>(playerPool) + offsets->playerPoolLocalName;

        SAMPVersion ver = GetSAMPVersion();
        // R5 belongs with R1/R2, not with R3/R4: its local name is a std::string (proven in
        // the constructor at rva 0x13FD0 - see the offset table). It was being read as a raw
        // char buffer, which on an SSO string happens to look right and on a heap-allocated
        // one (a name longer than 15 characters) hands back the raw POINTER bytes as text.
        if (ver == SAMPVersion::R1 || ver == SAMPVersion::R2 || ver == SAMPVersion::R5)
        {
            return ReadStdString(nameAddr);
        }

        // R3/R4/DL: fixed char buffer, read directly
        if (!IsValidPtr(reinterpret_cast<void*>(nameAddr))) return "";
        return reinterpret_cast<const char*>(nameAddr);
    }

    uint16_t GetPlayerId()
    {
        // SA:MP stores the local player id as a uint16 at playerPoolLocalId; reading it
        // as a 32-bit int pulled in the adjacent struct field as the high 16 bits (garbage),
        // so callers had to mask & 0xFFFF. Read it at its real width and return the SA:MP
        // INVALID_PLAYER_ID sentinel (0xFFFF) on failure.
        const SAMPOffsets* offsets = GetOffsets();
        if (!offsets) return 0xFFFF;

        void* playerPool = GetPlayerPool();
        if (!playerPool) return 0xFFFF;

        uintptr_t idAddr = reinterpret_cast<uintptr_t>(playerPool) + offsets->playerPoolLocalId;
        if (!IsValidPtr(reinterpret_cast<void*>(idAddr))) return 0xFFFF;

        return *reinterpret_cast<uint16_t*>(idAddr);
    }

    bool IsGameLoaded()
    {
        const SAMPOffsets* offsets = GetOffsets();
        if (!offsets) return false;

        void* netGame = GetSAMPInfo();
        if (!netGame) return false;

        uintptr_t stateAddr = reinterpret_cast<uintptr_t>(netGame) + offsets->iGameState;
        if (!IsValidPtr(reinterpret_cast<void*>(stateAddr))) return false;

        int gameState = *reinterpret_cast<int*>(stateAddr);
        return gameState >= 5;
    }

    bool IsPlayerSpawned()
    {
        const SAMPOffsets* offsets = GetOffsets();
        if (!offsets) return false;

        void* netGame = GetSAMPInfo();
        if (!netGame) return false;

        uintptr_t stateAddr = reinterpret_cast<uintptr_t>(netGame) + offsets->iGameState;
        if (!IsValidPtr(reinterpret_cast<void*>(stateAddr))) return false;

        int gameState = *reinterpret_cast<int*>(stateAddr);
        if (gameState != 14) return false;

        // Verify CLocalPlayer pointer is not null
        void* playerPool = GetPlayerPool();
        if (!playerPool) return false;

        uintptr_t localPlayerAddr = reinterpret_cast<uintptr_t>(playerPool) + offsets->playerPoolLocalPlayer;
        if (!IsValidPtr(reinterpret_cast<void*>(localPlayerAddr))) return false;

        void* localPlayer = *reinterpret_cast<void**>(localPlayerAddr);
        return localPlayer != nullptr;
    }

    bool RegisterChatCommand(const char* cmd, ChatCommandCallback callback)
    {
        if (!cmd || !callback)
        {
            Log("[RegisterChatCommand] Invalid parameters");
            return false;
        }

        // Install hook on first call
        if (!g_cmdHookInstalled)
        {
            if (!InstallCmdHook())
            {
                Log("[RegisterChatCommand] Failed to install SendCommand hook");
                return false;
            }
        }

        // Lowercase the command name for case-insensitive matching
        std::string cmdName(cmd);
        for (auto& c : cmdName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        std::lock_guard<std::mutex> lock(g_cmdMutex);
        g_registeredCommands[cmdName] = std::move(callback);
        return true;
    }
}
