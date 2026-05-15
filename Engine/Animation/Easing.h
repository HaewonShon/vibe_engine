#pragma once
#include <cmath>

namespace VibeEngine {

// ============================================================================
// EasingMode — specifies the interpolation curve applied when transitioning
// TOWARD a keyframe's value.
//   "In"  = slow start, fast end
//   "Out" = fast start, slow end
// ============================================================================
enum class EasingMode {
    Linear,
    Step,         // instant jump at t = 1
    EaseIn,       // quadratic — slow start
    EaseOut,      // quadratic — slow end
    EaseInOut,    // quadratic — slow start + slow end
    SineIn,
    SineOut,
    SineInOut,
    ElasticOut,   // springy overshoot
    BounceOut,    // multi-bounce decay
};

// ============================================================================
// Easing::Apply  — maps a [0,1] alpha through the chosen curve
// ============================================================================
namespace Easing {

inline float Apply(EasingMode mode, float t)
{
    // clamp to [0, 1]
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);

    constexpr float Pi = 3.14159265358979f;

    switch (mode)
    {
    case EasingMode::Linear:   return t;

    case EasingMode::Step:     return t >= 1.f ? 1.f : 0.f;

    case EasingMode::EaseIn:   return t * t;

    case EasingMode::EaseOut:  return t * (2.f - t);

    case EasingMode::EaseInOut:
        return t < 0.5f ? 2.f * t * t
                        : -1.f + (4.f - 2.f * t) * t;

    case EasingMode::SineIn:
        return 1.f - std::cos(t * Pi * 0.5f);

    case EasingMode::SineOut:
        return std::sin(t * Pi * 0.5f);

    case EasingMode::SineInOut:
        return 0.5f * (1.f - std::cos(t * Pi));

    case EasingMode::ElasticOut:
    {
        if (t == 0.f || t == 1.f) return t;
        const float p = 0.3f;
        return std::pow(2.f, -10.f * t)
             * std::sin((t - p / 4.f) * (2.f * Pi) / p) + 1.f;
    }

    case EasingMode::BounceOut:
    {
        if (t < 1.f / 2.75f)      { return 7.5625f * t * t; }
        if (t < 2.f / 2.75f)      { t -= 1.500f / 2.75f; return 7.5625f * t * t + 0.75f;    }
        if (t < 2.5f / 2.75f)     { t -= 2.250f / 2.75f; return 7.5625f * t * t + 0.9375f;  }
        t -= 2.625f / 2.75f;      return 7.5625f * t * t + 0.984375f;
    }

    default: return t;
    }
}

} // namespace Easing
} // namespace VibeEngine
