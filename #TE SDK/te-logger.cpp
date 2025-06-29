#include "te-logger.h"
#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include <filesystem>

namespace te_sdk::helper
{
    void Log(const char* fmt, ...)
    {
        const char* folder = "te_sdk";
        const char* filepath = "te_sdk/te_sdk.log";

        std::filesystem::create_directory(folder);

        FILE* file = nullptr;
        fopen_s(&file, filepath, "a");
        if (!file)
            return;

        va_list args;
        va_start(args, fmt);
        vfprintf(file, fmt, args);
        fprintf(file, "\n");
        va_end(args);

        fclose(file);
    }
}
