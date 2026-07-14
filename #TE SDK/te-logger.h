#pragma once

namespace te::sdk::helper::logging
{
    // Enable or disable file logging. When disabled, Log() is a no-op and no
    // te_sdk folder or log files are ever created. Enabled by default.
    void SetLoggingEnabled(bool enabled);

    // Returns whether file logging is currently enabled
    bool IsLoggingEnabled();

    // Set mod name to be included in log session headers and available for prefixing
    void SetModName(const char* name);

    // Returns current mod name, or empty string if not set
    const char* GetModName();

    void Log(const char* fmt, ...);
    void ResetSession();
}