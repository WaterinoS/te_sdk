#include "te-logger.h"
#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <process.h>
#include <mutex>

namespace te::sdk::helper::logging
{
    namespace
    {
        constexpr auto kLogFolderName = "te_sdk";
        constexpr auto kRetentionDuration = std::chrono::hours(72);

        std::once_flag g_cleanupOnceFlag;
        std::once_flag g_logFileInitFlag;
        std::filesystem::path g_logFilePath;

        // Cached per-process metadata, populated once alongside g_logFileInitFlag
        std::string g_exeName;
        int g_pid = 0;

        // Mutex protecting sessionReset and serialising concurrent Log() writes
        std::mutex g_logMutex;

        void CleanupOldLogs(const std::filesystem::path& folder)
        {
            try
            {
                if (!std::filesystem::exists(folder))
                    return;

                const auto cutoff = std::filesystem::file_time_type::clock::now() - kRetentionDuration;

                for (const auto& entry : std::filesystem::directory_iterator(folder))
                {
                    if (!entry.is_regular_file())
                        continue;

                    if (entry.path().extension() != ".log")
                        continue;

                    std::error_code timeEc;
                    auto lastWrite = std::filesystem::last_write_time(entry.path(), timeEc);
                    if (timeEc || lastWrite >= cutoff)
                        continue;

                    std::error_code removeEc;
                    std::filesystem::remove(entry.path(), removeEc);
                }
            }
            catch (const std::filesystem::filesystem_error&)
            {
                // Swallow cleanup errors - logging should never throw
            }
        }
    }

    // Protected by g_logMutex; written only in ResetSession() or the first Log() call
    static bool sessionReset = true;

    void ResetSession()
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        sessionReset = true;
    }

    void Log(const char* fmt, ...)
    {
        // Initialise the log folder and file path exactly once, outside the lock
        const std::filesystem::path logFolder(kLogFolderName);

        std::call_once(g_cleanupOnceFlag, [&logFolder]()
            {
                std::filesystem::create_directories(logFolder);
                CleanupOldLogs(logFolder);
            });

        std::call_once(g_logFileInitFlag, [&logFolder]()
            {
                // Cache process metadata once to avoid per-call overhead
                char modulePath[MAX_PATH]{};
                GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
                std::filesystem::path exePath(modulePath);
                g_exeName = exePath.stem().string();
                if (g_exeName.empty())
                    g_exeName = "te_runtime";

                g_pid = _getpid();

                const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                std::ostringstream filenameBuilder;
                filenameBuilder << g_exeName << "_" << g_pid << "_" << timestamp << ".log";
                g_logFilePath = logFolder / filenameBuilder.str();
                std::filesystem::create_directories(logFolder);
            });

        const std::string filepathStr = g_logFilePath.string();

        // Serialise writes and sessionReset flag access across threads
        std::lock_guard<std::mutex> lock(g_logMutex);

        FILE* file = nullptr;
        fopen_s(&file, filepathStr.c_str(), "a");
        if (!file)
            return;

        if (sessionReset)
        {
            auto now = std::chrono::system_clock::now();
            auto time_t_val = std::chrono::system_clock::to_time_t(now);

            std::tm tm_buf{};
            localtime_s(&tm_buf, &time_t_val);

            fprintf(
                file,
                "=== SESSION START (%s, pid %d): %04d-%02d-%02d %02d:%02d:%02d ===\n",
                g_exeName.c_str(),
                g_pid,
                tm_buf.tm_year + 1900,
                tm_buf.tm_mon + 1,
                tm_buf.tm_mday,
                tm_buf.tm_hour,
                tm_buf.tm_min,
                tm_buf.tm_sec);

            sessionReset = false;
        }

        va_list args;
        va_start(args, fmt);
        vfprintf(file, fmt, args);
        fprintf(file, "\n");
        va_end(args);

        fclose(file);
    }
}