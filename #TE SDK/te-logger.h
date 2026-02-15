#pragma once

namespace te::sdk::helper::logging
{
    // Set mod name to be included in log session headers and available for prefixing
    void SetModName(const char* name);

    // Returns current mod name, or empty string if not set
    const char* GetModName();

    void Log(const char* fmt, ...);
    void ResetSession();
}