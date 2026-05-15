#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "Log.h"
#include <cstdarg>
#include <cstdio>

namespace VibeEngine {

// ---- Level helpers ---------------------------------------------------------
static const char* LevelTag(LogLevel level)
{
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

// ---- Init / Shutdown -------------------------------------------------------
void Log::Init(const char* filename)
{
    fopen_s(&m_File, filename, "w");
    // Non-fatal if file can't be opened — we'll still write to debug output
}

void Log::Shutdown()
{
    if (m_File) {
        fflush(m_File);
        fclose(m_File);
        m_File = nullptr;
    }
}

// ---- Write -----------------------------------------------------------------
void Log::Write(LogLevel level, const char* file, int line,
                const char* fmt, ...)
{
    if (level < m_MinLevel) return;

    // --- timestamp ---
    SYSTEMTIME st;
    GetLocalTime(&st);

    // --- format the user message ---
    char msgBuf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    // --- assemble full line ---
    // Format: [HH:MM:SS.mmm] [LEVEL] [file:line]  message
    char lineBuf[1280];
    snprintf(lineBuf, sizeof(lineBuf),
        "[%02d:%02d:%02d.%03d] [%s] [%s:%d]  %s\n",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        LevelTag(level),
        file, line,
        msgBuf);

    // --- outputs ---
    OutputDebugStringA(lineBuf);

    if (m_File) {
        fputs(lineBuf, m_File);
        fflush(m_File);          // flush immediately so crashes don't eat logs
    }
}

} // namespace VibeEngine
