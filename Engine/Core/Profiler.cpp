#include "Profiler.h"
#include <algorithm>

namespace VibeEngine {

void Profiler::BeginFrame()
{
    // Commit the previous frame's accumulated time into smooth/min/max,
    // then reset the accumulator for the new frame.
    for (auto& [name, data] : m_Scopes) {
        // Update EMA — only after the first real frame (lastMs > 0 or smoothMs already set)
        if (data.smoothMs == 0.f)
            data.smoothMs = data.lastMs; // cold start: seed with raw value
        else
            data.smoothMs = k_Alpha * data.lastMs + (1.f - k_Alpha) * data.smoothMs;

        data.minMs = std::min(data.minMs, data.lastMs);
        data.maxMs = std::max(data.maxMs, data.lastMs);

        data.lastMs = 0.f; // reset accumulator
    }
}

void Profiler::Record(const char* name, float ms)
{
    m_Scopes[name].lastMs += ms; // accumulate (handles multiple calls per frame)
}

float Profiler::GetMs(const char* name) const
{
    auto it = m_Scopes.find(name);
    return it != m_Scopes.end() ? it->second.lastMs : 0.f;
}

float Profiler::GetSmoothedMs(const char* name) const
{
    auto it = m_Scopes.find(name);
    return it != m_Scopes.end() ? it->second.smoothMs : 0.f;
}

} // namespace VibeEngine
