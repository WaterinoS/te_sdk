#include "te-logger.h"
#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace te_sdk::helper::logging
{
    static bool sessionReset = true; // Flag to track if this is a new session

    void ResetSession()
    {
        sessionReset = true;
    }

    void Log(const char* fmt, ...)
    {
        const char* folder = "te_sdk";
        const char* filepath = "te_sdk/te_sdk.log";

        std::filesystem::create_directory(folder);

        // Determine file mode based on session reset flag
        const char* mode = sessionReset ? "w" : "a";

        FILE* file = nullptr;
        fopen_s(&file, filepath, mode);
        if (!file)
            return;

        // If this is a session reset, log session start information
        if (sessionReset)
        {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);

            std::tm tm_buf;
            localtime_s(&tm_buf, &time_t);

            fprintf(file, "=== SESSION START: %04d-%02d-%02d %02d:%02d:%02d ===\n",
                tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

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