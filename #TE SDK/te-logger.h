#pragma once

namespace te::sdk::helper::logging
{
    // Severity of a log line. Lines below the configured minimum level are
    // discarded before any formatting or file I/O happens.
    enum class Level
    {
        Trace = 0,
        Debug = 1,
        Info  = 2,
        Warn  = 3,
        Error = 4,
        Off   = 5   // only valid as an argument to SetMinLevel()
    };

    // Enable or disable file logging. When disabled, Log() is a no-op and no
    // te_sdk folder or log files are ever created. Enabled by default.
    void SetLoggingEnabled(bool enabled);

    // Returns whether file logging is currently enabled
    bool IsLoggingEnabled();

    // Discard every line below `level`. Defaults to Level::Info.
    // Pass Level::Off to silence logging without tearing down the open file.
    void SetMinLevel(Level level);
    Level GetMinLevel();

    // Set mod name. Used in session headers and as the log sub-directory.
    // May be called at any time - if a log file is already open it is closed
    // and the next line re-opens a fresh file under the new sub-directory.
    void SetModName(const char* name);

    // Returns current mod name, or empty string if not set
    const char* GetModName();

    // Directory the logs are written to. Defaults to a "te_sdk" folder next to
    // the module that loaded the SDK (NOT the process working directory).
    // Pass nullptr or "" to restore the default. Closes any open log file.
    void SetLogDirectory(const char* absolutePath);

    // Maximum size of a single log file in bytes before it is rotated
    // (a new file with a fresh timestamp is started). 0 disables rotation.
    // Defaults to 16 MiB.
    void SetMaxFileSize(unsigned long long bytes);

    // Log a line at Level::Info (printf-style formatting)
    void Log(const char* fmt, ...);

    // Log a line at an explicit level
    void LogAt(Level level, const char* fmt, ...);

    // Convenience wrappers
    void LogTrace(const char* fmt, ...);
    void LogDebug(const char* fmt, ...);
    void LogWarn(const char* fmt, ...);
    void LogError(const char* fmt, ...);

    // Force pending buffered output to disk. Called automatically on rotation
    // and by Shutdown(); useful before a deliberate crash/breakpoint.
    void Flush();

    // Mark the start of a new logical session - the next line written emits a
    // "SESSION START" header.
    void ResetSession();

    // Close the log file and release all logger resources. Safe to call more
    // than once; a later Log() simply re-opens a new file.
    void Shutdown();
}
