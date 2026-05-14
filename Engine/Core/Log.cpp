#include "Log.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace VibeEngine {

static FILE* s_LogFile = nullptr;

static FILE* GetLogFile()
{
    if (!s_LogFile)
        fopen_s(&s_LogFile, "vibe_engine.log", "a");
    return s_LogFile;
}

void LogMessage(LogLevel level, const char* fmt, ...)
{
    static const char* prefixes[] = { "[INFO] ", "[WARN] ", "[ERROR]" };
    const char* prefix = prefixes[static_cast<int>(level)];

    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char line[1088];
    snprintf(line, sizeof(line), "%s %s\n", prefix, msg);

    OutputDebugStringA(line);

    if (FILE* f = GetLogFile()) {
        fputs(line, f);
        fflush(f);
    }
}

} // namespace VibeEngine
