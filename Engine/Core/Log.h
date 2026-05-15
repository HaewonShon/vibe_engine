#pragma once
#include <cstdio>

namespace VibeEngine {

// Log verbosity levels — in increasing severity
enum class LogLevel { Trace, Info, Warn, Error };

// ============================================================================
// Log  (singleton)
//
// Call Log::Get().Init() early (Application ctor handles this automatically).
// Use the macros below — they inject file + line automatically.
//
//   LOG_INFO("Loaded %d assets", count);
//   LOG_WARN("Texture missing: %s", path);
//   LOG_ERROR("DX12 init failed (hr=0x%08X)", hr);
//   LOG_TRACE("Update dt=%.4f", dt);   // verbose, filtered in non-Debug
//
// Output format:
//   [14:23:45.123] [INFO ] [Application.cpp:39]  Application loop started.
// ============================================================================
class Log {
public:
    static Log& Get() { static Log inst; return inst; }

    // Open log file. Called by Application ctor.
    void Init(const char* filename = "vibe_engine.log");

    // Flush and close log file. Called by Application dtor.
    void Shutdown();

    // Set minimum level — messages below this are silently dropped.
    void     SetMinLevel(LogLevel level) { m_MinLevel = level; }
    LogLevel GetMinLevel()         const { return m_MinLevel; }

    // Write a message. Use the macros instead of calling this directly.
    void Write(LogLevel level, const char* file, int line,
               const char* fmt, ...);

private:
    Log() = default;

    LogLevel m_MinLevel = LogLevel::Trace;
    FILE*    m_File     = nullptr;
};

} // namespace VibeEngine

// ---- Macros -----------------------------------------------------------------
// Strip full path — keep only the filename for readability
#define LOG_FILENAME (::strrchr(__FILE__, '\\') ? ::strrchr(__FILE__, '\\') + 1 : __FILE__)

#define LOG_TRACE(...) VibeEngine::Log::Get().Write(VibeEngine::LogLevel::Trace, LOG_FILENAME, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  VibeEngine::Log::Get().Write(VibeEngine::LogLevel::Info,  LOG_FILENAME, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  VibeEngine::Log::Get().Write(VibeEngine::LogLevel::Warn,  LOG_FILENAME, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) VibeEngine::Log::Get().Write(VibeEngine::LogLevel::Error, LOG_FILENAME, __LINE__, __VA_ARGS__)
