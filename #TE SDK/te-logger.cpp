#include "te-logger.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <string>
#include <vector>
#include <process.h>
#include <mutex>
#include <atomic>

namespace te::sdk::helper::logging
{
    namespace
    {
        constexpr auto kLogFolderName = L"te_sdk";
        constexpr auto kRetentionDuration = std::chrono::hours(72);
        constexpr unsigned long long kDefaultMaxFileSize = 16ull * 1024 * 1024;

        // ---- configuration (atomics: readable without taking the log mutex) ----

        // Master switch for file logging. When false, Log() bails out before
        // touching the filesystem, so no te_sdk folder is ever created.
        std::atomic<bool> g_loggingEnabled{ true };
        std::atomic<Level> g_minLevel{ Level::Info };
        std::atomic<unsigned long long> g_maxFileSize{ kDefaultMaxFileSize };

        // ---- state guarded by g_logMutex ----

        std::recursive_mutex g_logMutex;

        FILE* g_file = nullptr;
        unsigned long long g_bytesWritten = 0;
        bool g_sessionReset = true;

        char g_modName[64] = {};
        std::filesystem::path g_baseDir;       // empty => derive from module path
        std::filesystem::path g_activeDir;     // directory the open file lives in
        std::string g_exeName;
        int g_pid = 0;
        bool g_processInfoCached = false;

        const char* LevelTag(Level level)
        {
            switch (level)
            {
            case Level::Trace: return "TRACE";
            case Level::Debug: return "DEBUG";
            case Level::Info:  return "INFO ";
            case Level::Warn:  return "WARN ";
            case Level::Error: return "ERROR";
            default:           return "?????";
            }
        }

        // Directory of the binary that linked the SDK. Using the module path
        // rather than the process working directory keeps logs next to the mod
        // no matter what the game sets its CWD to.
        std::filesystem::path GetModuleDirectory()
        {
            HMODULE self = nullptr;
            if (GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&LevelTag),
                    &self))
            {
                wchar_t modulePath[MAX_PATH]{};
                if (GetModuleFileNameW(self, modulePath, MAX_PATH))
                {
                    std::error_code ec;
                    std::filesystem::path p(modulePath);
                    p = p.parent_path();
                    if (!p.empty() && std::filesystem::exists(p, ec))
                        return p;
                }
            }

            // Fall back to the working directory if the module path is unavailable
            return std::filesystem::path();
        }

        void CacheProcessInfo()
        {
            if (g_processInfoCached)
                return;

            char modulePath[MAX_PATH]{};
            GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
            std::filesystem::path exePath(modulePath);
            g_exeName = exePath.stem().string();
            if (g_exeName.empty())
                g_exeName = "te_runtime";

            g_pid = _getpid();
            g_processInfoCached = true;
        }

        void CleanupOldLogs(const std::filesystem::path& folder)
        {
            std::error_code ec;
            if (!std::filesystem::exists(folder, ec))
                return;

            const auto cutoff = std::filesystem::file_time_type::clock::now() - kRetentionDuration;

            std::filesystem::directory_iterator it(folder, ec);
            if (ec)
                return;

            for (const auto& entry : it)
            {
                std::error_code entryEc;
                if (!entry.is_regular_file(entryEc) || entryEc)
                    continue;

                if (entry.path().extension() != ".log")
                    continue;

                auto lastWrite = std::filesystem::last_write_time(entry.path(), entryEc);
                if (entryEc || lastWrite >= cutoff)
                    continue;

                std::error_code removeEc;
                std::filesystem::remove(entry.path(), removeEc);
            }
        }

        // Caller must hold g_logMutex.
        void CloseFileLocked()
        {
            if (g_file)
            {
                fflush(g_file);
                fclose(g_file);
                g_file = nullptr;
            }
            g_bytesWritten = 0;
        }

        // Caller must hold g_logMutex. Returns true if a usable file is open.
        bool EnsureFileLocked()
        {
            if (g_file)
                return true;

            CacheProcessInfo();

            std::filesystem::path root = g_baseDir;
            if (root.empty())
            {
                std::filesystem::path moduleDir = GetModuleDirectory();
                root = moduleDir.empty() ? std::filesystem::path(kLogFolderName)
                                         : moduleDir / kLogFolderName;
            }

            // Group logs of a named mod into their own sub-directory
            std::filesystem::path folder = root;
            if (g_modName[0] != '\0')
                folder /= g_modName;

            std::error_code ec;
            std::filesystem::create_directories(folder, ec);
            if (ec && !std::filesystem::exists(folder))
                return false;

            if (folder != g_activeDir)
            {
                CleanupOldLogs(folder);
                g_activeDir = folder;
            }

            const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            char filename[128]{};
            _snprintf_s(filename, sizeof(filename), _TRUNCATE, "%s_%d_%lld.log",
                        g_exeName.c_str(), g_pid, static_cast<long long>(timestamp));

            const std::filesystem::path fullPath = folder / filename;

            FILE* file = nullptr;
            if (_wfopen_s(&file, fullPath.c_str(), L"ab") != 0 || !file)
                return false;

            // 64 KiB of buffering keeps per-line cost to a memcpy; the file is
            // flushed on rotation, on Flush() and on Shutdown().
            setvbuf(file, nullptr, _IOFBF, 64 * 1024);

            g_file = file;
            g_bytesWritten = 0;
            return true;
        }

        // Caller must hold g_logMutex.
        void WriteSessionHeaderLocked()
        {
            auto now = std::chrono::system_clock::now();
            auto timeVal = std::chrono::system_clock::to_time_t(now);

            std::tm tmBuf{};
            localtime_s(&tmBuf, &timeVal);

            int written;
            if (g_modName[0] != '\0')
            {
                written = fprintf(
                    g_file,
                    "=== SESSION START (%s | %s, pid %d): %04d-%02d-%02d %02d:%02d:%02d ===\n",
                    g_modName, g_exeName.c_str(), g_pid,
                    tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
                    tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
            }
            else
            {
                written = fprintf(
                    g_file,
                    "=== SESSION START (%s, pid %d): %04d-%02d-%02d %02d:%02d:%02d ===\n",
                    g_exeName.c_str(), g_pid,
                    tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
                    tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
            }

            if (written > 0)
                g_bytesWritten += static_cast<unsigned long long>(written);
        }

        void WriteLine(Level level, const char* fmt, va_list args)
        {
            if (!fmt)
                return;

            if (!g_loggingEnabled.load(std::memory_order_relaxed))
                return;

            if (level < g_minLevel.load(std::memory_order_relaxed))
                return;

            // Format outside the lock so a slow/large message does not stall
            // other threads (the network thread in particular).
            char stackBuffer[1024];
            std::vector<char> heapBuffer;
            const char* message = stackBuffer;

            va_list argsCopy;
            va_copy(argsCopy, args);
            int needed = _vsnprintf_s(stackBuffer, sizeof(stackBuffer), _TRUNCATE, fmt, args);
            if (needed < 0)
            {
                // Message did not fit; find the real length and format again
                const int exact = _vscprintf(fmt, argsCopy);
                if (exact > 0)
                {
                    heapBuffer.resize(static_cast<size_t>(exact) + 1);
                    if (_vsnprintf_s(heapBuffer.data(), heapBuffer.size(), _TRUNCATE, fmt, argsCopy) >= 0)
                        message = heapBuffer.data();
                }
            }
            va_end(argsCopy);

            // Wall-clock stamp with millisecond resolution
            const auto now = std::chrono::system_clock::now();
            const auto timeVal = std::chrono::system_clock::to_time_t(now);
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count() % 1000;

            std::tm tmBuf{};
            localtime_s(&tmBuf, &timeVal);

            const DWORD threadId = GetCurrentThreadId();

            std::lock_guard<std::recursive_mutex> lock(g_logMutex);

            if (!EnsureFileLocked())
                return;

            if (g_sessionReset)
            {
                WriteSessionHeaderLocked();
                g_sessionReset = false;
            }

            const int written = fprintf(
                g_file, "[%02d:%02d:%02d.%03d] [%s] [t:%lu] %s\n",
                tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, static_cast<int>(millis),
                LevelTag(level), static_cast<unsigned long>(threadId), message);

            if (written > 0)
                g_bytesWritten += static_cast<unsigned long long>(written);

            const unsigned long long maxSize = g_maxFileSize.load(std::memory_order_relaxed);
            if (maxSize != 0 && g_bytesWritten >= maxSize)
            {
                // Rotate: close the current file, the next line opens a new one
                CloseFileLocked();
                g_sessionReset = true;
            }
        }
    }

    void SetLoggingEnabled(bool enabled)
    {
        g_loggingEnabled.store(enabled, std::memory_order_relaxed);
        if (!enabled)
        {
            std::lock_guard<std::recursive_mutex> lock(g_logMutex);
            CloseFileLocked();
        }
    }

    bool IsLoggingEnabled()
    {
        return g_loggingEnabled.load(std::memory_order_relaxed);
    }

    void SetMinLevel(Level level)
    {
        g_minLevel.store(level, std::memory_order_relaxed);
    }

    Level GetMinLevel()
    {
        return g_minLevel.load(std::memory_order_relaxed);
    }

    void SetModName(const char* name)
    {
        std::lock_guard<std::recursive_mutex> lock(g_logMutex);

        char newName[sizeof(g_modName)]{};
        if (name)
            strncpy_s(newName, sizeof(newName), name, _TRUNCATE);

        if (strcmp(newName, g_modName) == 0)
            return;

        memcpy(g_modName, newName, sizeof(g_modName));

        // The mod name selects the log sub-directory, so an already open file
        // lives in the wrong place now. Close it and start a fresh session in
        // the correct folder on the next line - this makes SetModName() safe to
        // call after the SDK has already logged something.
        CloseFileLocked();
        g_sessionReset = true;
    }

    const char* GetModName()
    {
        return g_modName;
    }

    void SetLogDirectory(const char* absolutePath)
    {
        std::lock_guard<std::recursive_mutex> lock(g_logMutex);

        g_baseDir = (absolutePath && absolutePath[0] != '\0')
            ? std::filesystem::path(absolutePath)
            : std::filesystem::path();

        CloseFileLocked();
        g_activeDir.clear();
        g_sessionReset = true;
    }

    void SetMaxFileSize(unsigned long long bytes)
    {
        g_maxFileSize.store(bytes, std::memory_order_relaxed);
    }

    void Log(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        WriteLine(Level::Info, fmt, args);
        va_end(args);
    }

    void LogAt(Level level, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        WriteLine(level, fmt, args);
        va_end(args);
    }

    void LogTrace(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        WriteLine(Level::Trace, fmt, args);
        va_end(args);
    }

    void LogDebug(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        WriteLine(Level::Debug, fmt, args);
        va_end(args);
    }

    void LogWarn(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        WriteLine(Level::Warn, fmt, args);
        va_end(args);
    }

    void LogError(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        WriteLine(Level::Error, fmt, args);
        va_end(args);
    }

    void Flush()
    {
        std::lock_guard<std::recursive_mutex> lock(g_logMutex);
        if (g_file)
            fflush(g_file);
    }

    void ResetSession()
    {
        std::lock_guard<std::recursive_mutex> lock(g_logMutex);
        g_sessionReset = true;
    }

    void Shutdown()
    {
        std::lock_guard<std::recursive_mutex> lock(g_logMutex);
        CloseFileLocked();
        g_activeDir.clear();
        g_sessionReset = true;
    }
}
