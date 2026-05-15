#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <unordered_map>
#include <string>
#include <cfloat>

namespace VibeEngine {

// ============================================================================
// Profiler
// CPU scope timer. Use the PROFILE_SCOPE("name") macro to time any block.
//
//   void Foo() {
//       PROFILE_SCOPE("Foo");
//       ...
//   }
//
// Query results each frame:
//   float ms  = Profiler::Get().GetMs("Foo");         // last frame raw
//   float sms = Profiler::Get().GetSmoothedMs("Foo"); // EMA smoothed
//
// Or iterate everything:
//   for (auto& [name, data] : Profiler::Get().GetAllScopes()) { ... }
// ============================================================================
class Profiler {
public:
    static Profiler& Get() { static Profiler inst; return inst; }

    // Per-scope statistics
    struct ScopeData {
        float lastMs   = 0.f;                              // current frame accumulator
        float smoothMs = 0.f;                              // EMA smoothed
        float minMs    = FLT_MAX;
        float maxMs    = 0.f;
    };

    // Call once at the start of each frame.
    // Commits last frame's times into smooth/min/max, then resets accumulators.
    void BeginFrame();

    // Called by ProfileScope — adds elapsed ms to the named scope.
    void Record(const char* name, float ms);

    // Raw last-frame total (may be 0 until the frame completes)
    float GetMs(const char* name) const;

    // EMA-smoothed value — stable for display
    float GetSmoothedMs(const char* name) const;

    // Access all recorded scopes (for overlay / ImGui / title bar)
    const std::unordered_map<std::string, ScopeData>& GetAllScopes() const { return m_Scopes; }

    // QPC frequency — used by ProfileScope (fetched once at startup)
    LONGLONG GetFrequency() const { return m_Freq.QuadPart; }

private:
    Profiler() { QueryPerformanceFrequency(&m_Freq); }

    // Exponential Moving Average factor (0 = no response, 1 = no smoothing)
    static constexpr float k_Alpha = 0.15f;

    LARGE_INTEGER m_Freq = {};
    std::unordered_map<std::string, ScopeData> m_Scopes;
};


// ============================================================================
// ProfileScope  (RAII)
// Created by the PROFILE_SCOPE macro; records elapsed ms on destruction.
// ============================================================================
class ProfileScope {
public:
    explicit ProfileScope(const char* name)
        : m_Name(name)
        , m_InvFreq(1000.0 / static_cast<double>(Profiler::Get().GetFrequency()))
    {
        QueryPerformanceCounter(&m_Start);
    }

    ~ProfileScope()
    {
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        float ms = static_cast<float>((end.QuadPart - m_Start.QuadPart) * m_InvFreq);
        Profiler::Get().Record(m_Name, ms);
    }

private:
    const char*   m_Name;
    double        m_InvFreq; // pre-computed: 1000 / freq  (ms conversion factor)
    LARGE_INTEGER m_Start = {};
};

} // namespace VibeEngine

// Macro — unique variable name via line number so nested scopes don't collide
#define PROFILE_SCOPE(name) VibeEngine::ProfileScope _prof_##__LINE__(name)
