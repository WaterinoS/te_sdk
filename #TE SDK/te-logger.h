#pragma once

namespace te_sdk::helper::logging
{
    void Log(const char* fmt, ...);
    void ResetSession(); // New function to reset session flag
}