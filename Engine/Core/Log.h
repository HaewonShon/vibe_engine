#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>

namespace VibeEngine {

enum class LogLevel { Info, Warn, Error };

void LogMessage(LogLevel level, const char* fmt, ...);

} // namespace VibeEngine

#define LOG_INFO(...)  VibeEngine::LogMessage(VibeEngine::LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  VibeEngine::LogMessage(VibeEngine::LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) VibeEngine::LogMessage(VibeEngine::LogLevel::Error, __VA_ARGS__)
