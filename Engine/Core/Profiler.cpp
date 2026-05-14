#include "Profiler.h"

namespace VibeEngine {

void Profiler::BeginFrame()
{
    m_Durations.clear();
}

void Profiler::Record(const char* name, float ms)
{
    m_Durations[name] += ms;
}

float Profiler::GetMs(const char* name) const
{
    auto it = m_Durations.find(name);
    return it != m_Durations.end() ? it->second : 0.f;
}

} // namespace VibeEngine
