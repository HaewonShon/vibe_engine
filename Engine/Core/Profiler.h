#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <unordered_map>
#include <string>

namespace VibeEngine {

class Profiler {
public:
    static Profiler& Get() { static Profiler inst; return inst; }

    void BeginFrame();
    void Record(const char* name, float ms);
    float GetMs(const char* name) const;

private:
    std::unordered_map<std::string, float> m_Durations;
};

// RAII scope timer
class ProfileScope {
public:
    ProfileScope(const char* name) : m_Name(name)
    {
        QueryPerformanceFrequency(&m_Freq);
        QueryPerformanceCounter(&m_Start);
    }
    ~ProfileScope()
    {
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        float ms = static_cast<float>(end.QuadPart - m_Start.QuadPart)
                 / static_cast<float>(m_Freq.QuadPart) * 1000.f;
        Profiler::Get().Record(m_Name, ms);
    }
private:
    const char*   m_Name;
    LARGE_INTEGER m_Start = {};
    LARGE_INTEGER m_Freq  = {};
};

} // namespace VibeEngine

#define PROFILE_SCOPE(name) VibeEngine::ProfileScope _prof_##__LINE__(name)
