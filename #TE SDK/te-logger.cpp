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

    static bool sessionReset = true; // Flag to track if this is a new session

    void ResetSession()
    {
        sessionReset = true;
    }

    void Log(const char* fmt, ...)
    {
        const std::filesystem::path logFolder(kLogFolderName);
        std::filesystem::create_directories(logFolder);

        char modulePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
        std::filesystem::path exePath(modulePath);
        std::string exeName = exePath.stem().string();
        if (exeName.empty())
            exeName = "te_runtime";

        const int pid = _getpid();

        std::call_once(g_cleanupOnceFlag, [folder = logFolder]()
            {
                CleanupOldLogs(folder);
            });

        std::call_once(g_logFileInitFlag, [folder = logFolder, exeName, pid]()
            {
                const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                std::ostringstream filenameBuilder;
                filenameBuilder << exeName << "_" << pid << "_" << timestamp << ".log";
                g_logFilePath = folder / filenameBuilder.str();
            });

        const std::string filepathStr = g_logFilePath.string();

        FILE* file = nullptr;
        fopen_s(&file, filepathStr.c_str(), "a");
        if (!file)
            return;

        if (sessionReset)
        {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);

            std::tm tm_buf{};
            localtime_s(&tm_buf, &time_t);

            fprintf(
                file,
                "=== SESSION START (%s, pid %d): %04d-%02d-%02d %02d:%02d:%02d ===\n",
                exeName.c_str(),
                pid,
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