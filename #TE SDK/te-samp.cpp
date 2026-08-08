#include "te-sdk.h"

#include <MinHook.h>
#include <cctype>
#include <cstring>
#include <atomic>
#include <map>
#include <mutex>
#include <string>

namespace te::sdk::helper::samp
{
    using namespace te::sdk::helper::logging;

    namespace
    {
        // ---- pointer-chain helpers ----

        template<typename T>
        bool ReadAt(uintptr_t address, T& out)
        {
            if (!IsReadable(reinterpret_cast<const void*>(address), sizeof(T)))
                return false;
            out = *reinterpret_cast<const T*>(address);
            return true;
        }

        void* GetPlayerPool()
        {
            const VersionProfile* profile = GetVersionProfile();
            if (!profile)
                return nullptr;

            void* netGame = GetSAMPInfo();
            if (!netGame)
                return nullptr;

            void* pools = nullptr;
            if (!ReadAt(reinterpret_cast<uintptr_t>(netGame) + profile->netGamePools, pools) || !pools)
                return nullptr;

            void* playerPool = nullptr;
            if (!ReadAt(reinterpret_cast<uintptr_t>(pools) + profile->poolsPlayerPool, playerPool))
                return nullptr;

            return playerPool;
        }

        // Copy a NUL-terminated string out of game memory, bounded both by
        // `maxLength` and by where the readable region ends.
        std::string CopyGameString(uintptr_t address, size_t maxLength)
        {
            std::string result;
            if (!address)
                return result;

            result.reserve(32);
            for (size_t i = 0; i < maxLength; ++i)
            {
                char c = '\0';
                if (!ReadAt(address + i, c))
                    break;
                if (c == '\0')
                    break;
                result.push_back(c);
            }
            return result;
        }

        // Read an MSVC x86 std::string out of memory (R1/R2 local player name).
        // Layout: [16-byte SSO buffer][4-byte size][4-byte capacity] = 24 bytes
        std::string ReadStdString(uintptr_t address)
        {
            uint32_t capacity = 0;
            if (!ReadAt(address + 20, capacity))
                return {};

            uint32_t size = 0;
            if (!ReadAt(address + 16, size))
                return {};

            if (capacity <= 15)
            {
                // SSO: string data is inline at `address`
                return CopyGameString(address, 16);
            }

            uintptr_t heapPtr = 0;
            if (!ReadAt(address, heapPtr) || !heapPtr)
                return {};

            // Trust the region walk over `size`, which may be garbage if we are
            // reading a half-initialised string
            return CopyGameString(heapPtr, size > 0 && size < 4096 ? size : 256);
        }

        // Returns a stable pointer for a value that lives in game memory.
        // One buffer per thread, so two threads never stomp on each other.
        const char* Stabilise(std::string&& value)
        {
            thread_local std::string buffer;
            buffer = std::move(value);
            return buffer.c_str();
        }

        // ---- guarded calls into samp.dll ----
        //
        // These live in their own functions because MSVC forbids __try in a
        // function that needs C++ unwinding. Validating the pointer chain
        // covers the common failure (a stale/incorrect offset yielding a null
        // or unmapped pointer); the SEH frame catches the rest instead of
        // taking the whole game down.

        using AddToChatWndFunc = void(__thiscall*)(void*, int, const char*, const char*, uint32_t, uint32_t);
        using tSendCmd = void(__thiscall*)(void* pInput, const char* command);

        bool GuardedAddToChatWnd(AddToChatWndFunc fn, void* chatInfo, const char* text, uint32_t color)
        {
            __try
            {
                fn(chatInfo, 8, text, "", color, color);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool GuardedSendCmd(tSendCmd fn, void* pInput, const char* command)
        {
            __try
            {
                fn(pInput, command);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        // ---- chat command hook state ----

        std::map<std::string, ChatCommandCallback> g_registeredCommands;
        std::mutex g_cmdMutex;                    // guards g_registeredCommands
        std::mutex g_cmdHookMutex;                // serialises install/uninstall
        tSendCmd oSendCmd = nullptr;
        std::atomic<bool> g_cmdHookInstalled{ false };
        void* g_cmdHookTarget = nullptr;

        std::string ToLower(const char* begin, const char* end)
        {
            std::string result(begin, end);
            for (auto& c : result)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return result;
        }

        void __fastcall hkSendCmd(void* pInput, void* /*edx*/, const char* command)
        {
            if (command && command[0] == '/')
            {
                // Parse command name (first word after '/')
                const char* cmdStart = command + 1;
                const char* space = cmdStart;
                while (*space && *space != ' ')
                    ++space;

                const std::string cmdName = ToLower(cmdStart, space);

                // Copy the handler out and release the lock BEFORE invoking it.
                // Calling user code while holding g_cmdMutex deadlocked as soon
                // as the handler touched Register/UnregisterChatCommand.
                ChatCommandCallback handler;
                {
                    std::lock_guard<std::mutex> lock(g_cmdMutex);
                    auto it = g_registeredCommands.find(cmdName);
                    if (it != g_registeredCommands.end())
                        handler = it->second;
                }

                if (handler)
                {
                    // Skip whitespace after the command name to get params
                    const char* params = space;
                    while (*params == ' ')
                        ++params;

                    handler(params);
                    return; // handled client-side, don't forward to the server
                }
            }

            if (oSendCmd)
                oSendCmd(pInput, command);
        }

        bool InstallCmdHook()
        {
            std::lock_guard<std::mutex> lock(g_cmdHookMutex);

            if (g_cmdHookInstalled.load(std::memory_order_acquire))
                return true;

            const VersionProfile* profile = GetVersionProfile();
            if (!profile || profile->fnSendCommand == 0)
            {
                LogError("[te::sdk::samp] No SendCommand offset for the detected SA-MP version");
                return false;
            }

            const uintptr_t sampBase = GetSAMPBase();
            if (!sampBase)
            {
                LogError("[te::sdk::samp] samp.dll is not loaded");
                return false;
            }

            void* target = reinterpret_cast<void*>(sampBase + profile->fnSendCommand);

            const MH_STATUS initStatus = MH_Initialize();
            if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED)
            {
                LogError("[te::sdk::samp] MH_Initialize failed: %d", initStatus);
                return false;
            }

            const MH_STATUS createStatus =
                MH_CreateHook(target, reinterpret_cast<void*>(&hkSendCmd),
                              reinterpret_cast<void**>(&oSendCmd));
            if (createStatus != MH_OK)
            {
                LogError("[te::sdk::samp] MH_CreateHook(SendCommand) failed: %d", createStatus);
                return false;
            }

            const MH_STATUS enableStatus = MH_EnableHook(target);
            if (enableStatus != MH_OK)
            {
                LogError("[te::sdk::samp] MH_EnableHook(SendCommand) failed: %d", enableStatus);
                MH_RemoveHook(target);
                oSendCmd = nullptr;
                return false;
            }

            g_cmdHookTarget = target;
            g_cmdHookInstalled.store(true, std::memory_order_release);
            return true;
        }

        void UninstallCmdHook()
        {
            std::lock_guard<std::mutex> lock(g_cmdHookMutex);

            if (!g_cmdHookInstalled.load(std::memory_order_acquire))
                return;

            if (g_cmdHookTarget)
            {
                MH_DisableHook(g_cmdHookTarget);
                MH_RemoveHook(g_cmdHookTarget);
                g_cmdHookTarget = nullptr;
            }

            oSendCmd = nullptr;
            g_cmdHookInstalled.store(false, std::memory_order_release);
        }
    }

    // ------------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------------

    bool AddChatMessage(const char* text, uint32_t color)
    {
        if (!text)
        {
            LogWarn("[te::sdk::samp] AddChatMessage: null text");
            return false;
        }

        const VersionProfile* profile = GetVersionProfile();
        if (!profile || profile->chatInfo == 0 || profile->fnAddToChatWnd == 0)
        {
            LogWarn("[te::sdk::samp] AddChatMessage: no profile for version %s",
                    TranslateSAMPVersion(GetSAMPVersion()).c_str());
            return false;
        }

        const uintptr_t sampBase = GetSAMPBase();
        if (!sampBase)
            return false;

        void* chatInfo = nullptr;
        if (!ReadAt(sampBase + profile->chatInfo, chatInfo) || !chatInfo)
        {
            LogWarn("[te::sdk::samp] AddChatMessage: chat instance not ready");
            return false;
        }

        auto fn = reinterpret_cast<AddToChatWndFunc>(sampBase + profile->fnAddToChatWnd);
        if (!GuardedAddToChatWnd(fn, chatInfo, text, color))
        {
            LogError("[te::sdk::samp] AddChatMessage: AddToChatWindow faulted");
            return false;
        }

        return true;
    }

    bool SendCommand(const char* command)
    {
        if (!command)
        {
            LogWarn("[te::sdk::samp] SendCommand: null command");
            return false;
        }

        const VersionProfile* profile = GetVersionProfile();
        if (!profile || profile->inputInfo == 0 || profile->fnSendCommand == 0)
        {
            LogWarn("[te::sdk::samp] SendCommand: unsupported SA-MP version");
            return false;
        }

        const uintptr_t sampBase = GetSAMPBase();
        if (!sampBase)
            return false;

        void* pInput = nullptr;
        if (!ReadAt(sampBase + profile->inputInfo, pInput) || !pInput)
            return false;

        // Go through the trampoline when our hook is installed, so sending a
        // command from code does not re-enter our own dispatcher.
        tSendCmd fn = g_cmdHookInstalled.load(std::memory_order_acquire) && oSendCmd
            ? oSendCmd
            : reinterpret_cast<tSendCmd>(sampBase + profile->fnSendCommand);

        return GuardedSendCmd(fn, pInput, command);
    }

    const char* GetServerName()
    {
        const VersionProfile* profile = GetVersionProfile();
        if (!profile)
            return Stabilise({});

        void* netGame = GetSAMPInfo();
        if (!netGame)
            return Stabilise({});

        return Stabilise(CopyGameString(
            reinterpret_cast<uintptr_t>(netGame) + profile->netGameHostname, 255));
    }

    const char* GetPlayerName()
    {
        const VersionProfile* profile = GetVersionProfile();
        if (!profile)
            return Stabilise({});

        void* playerPool = GetPlayerPool();
        if (!playerPool)
            return Stabilise({});

        const uintptr_t nameAddr =
            reinterpret_cast<uintptr_t>(playerPool) + profile->playerPoolLocalName;

        if (profile->localNameIsStdString)
            return Stabilise(ReadStdString(nameAddr));

        // R3+ and DL: fixed char buffer (MAX_PLAYER_NAME is 24)
        return Stabilise(CopyGameString(nameAddr, 24));
    }

    uint16_t GetPlayerId()
    {
        // SA-MP stores the local player id as a uint16. Reading it as a 32-bit
        // int pulled the adjacent struct field in as the high 16 bits, so
        // callers had to mask with 0xFFFF; read it at its real width instead.
        const VersionProfile* profile = GetVersionProfile();
        if (!profile)
            return kInvalidPlayerId;

        void* playerPool = GetPlayerPool();
        if (!playerPool)
            return kInvalidPlayerId;

        uint16_t id = kInvalidPlayerId;
        if (!ReadAt(reinterpret_cast<uintptr_t>(playerPool) + profile->playerPoolLocalId, id))
            return kInvalidPlayerId;

        return id;
    }

    bool IsGameLoaded()
    {
        const VersionProfile* profile = GetVersionProfile();
        if (!profile)
            return false;

        void* netGame = GetSAMPInfo();
        if (!netGame)
            return false;

        int gameState = 0;
        if (!ReadAt(reinterpret_cast<uintptr_t>(netGame) + profile->netGameGameState, gameState))
            return false;

        return gameState >= 5;
    }

    bool IsPlayerSpawned()
    {
        const VersionProfile* profile = GetVersionProfile();
        if (!profile)
            return false;

        void* netGame = GetSAMPInfo();
        if (!netGame)
            return false;

        int gameState = 0;
        if (!ReadAt(reinterpret_cast<uintptr_t>(netGame) + profile->netGameGameState, gameState))
            return false;

        if (gameState != 14)
            return false;

        void* playerPool = GetPlayerPool();
        if (!playerPool)
            return false;

        void* localPlayer = nullptr;
        if (!ReadAt(reinterpret_cast<uintptr_t>(playerPool) + profile->playerPoolLocalPlayer, localPlayer))
            return false;

        return localPlayer != nullptr;
    }

    bool RegisterChatCommand(const char* cmd, ChatCommandCallback callback)
    {
        if (!cmd || cmd[0] == '\0' || !callback)
        {
            LogWarn("[te::sdk::samp] RegisterChatCommand: invalid parameters");
            return false;
        }

        if (!InstallCmdHook())
        {
            LogError("[te::sdk::samp] RegisterChatCommand: SendCommand hook unavailable");
            return false;
        }

        const std::string cmdName = ToLower(cmd, cmd + strlen(cmd));

        std::lock_guard<std::mutex> lock(g_cmdMutex);
        g_registeredCommands[cmdName] = std::move(callback);
        return true;
    }

    bool UnregisterChatCommand(const char* cmd)
    {
        if (!cmd || cmd[0] == '\0')
            return false;

        const std::string cmdName = ToLower(cmd, cmd + strlen(cmd));

        // Destroy the std::function outside the lock: it may own objects whose
        // destructor calls back into the SDK.
        ChatCommandCallback removed;
        {
            std::lock_guard<std::mutex> lock(g_cmdMutex);
            auto it = g_registeredCommands.find(cmdName);
            if (it == g_registeredCommands.end())
                return false;

            removed = std::move(it->second);
            g_registeredCommands.erase(it);
        }

        return true;
    }

    void ClearChatCommands()
    {
        std::map<std::string, ChatCommandCallback> removed;
        {
            std::lock_guard<std::mutex> lock(g_cmdMutex);
            removed.swap(g_registeredCommands);
        }

        UninstallCmdHook();
        // `removed` is destroyed here, outside every SDK lock
    }

    bool IsChatCommandRegistered(const char* cmd)
    {
        if (!cmd || cmd[0] == '\0')
            return false;

        const std::string cmdName = ToLower(cmd, cmd + strlen(cmd));

        std::lock_guard<std::mutex> lock(g_cmdMutex);
        return g_registeredCommands.find(cmdName) != g_registeredCommands.end();
    }
}
