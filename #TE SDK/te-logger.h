#pragma once

namespace te::sdk::helper::logging
{
    void Log(const char* fmt, ...);
    void ResetSession(); // New function to reset session flag
}