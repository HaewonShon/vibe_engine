#include "AnimationClip.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace VibeEngine {

// ---------------------------------------------------------------------------
// Float3 linear interpolation
// ---------------------------------------------------------------------------
static XMFLOAT3 Lerp3(const XMFLOAT3& a, const XMFLOAT3& b, float t)
{
    return { a.x + (b.x - a.x) * t,
             a.y + (b.y - a.y) * t,
             a.z + (b.z - a.z) * t };
}

// ============================================================================
// Float3Track
// ============================================================================

void Float3Track::AddKey(float time, XMFLOAT3 value, EasingMode easing)
{
    keys.push_back({ time, value, easing });
    Sort();
}

void Float3Track::Sort()
{
    std::sort(keys.begin(), keys.end(),
              [](const Key& a, const Key& b) { return a.time < b.time; });
}

XMFLOAT3 Float3Track::Sample(float t) const
{
    if (keys.empty())         return {};
    if (keys.size() == 1)     return keys[0].value;
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time)  return keys.back().value;

    // Binary search for the surrounding pair
    auto it = std::lower_bound(keys.begin(), keys.end(), t,
        [](const Key& k, float v) { return k.time < v; });

    const Key& k1 = *it;
    const Key& k0 = *(it - 1);

    float span  = k1.time - k0.time;
    float alpha = (span > 1e-6f) ? (t - k0.time) / span : 1.f;

    // Easing curve is the property of the TARGET keyframe (k1)
    alpha = Easing::Apply(k1.easing, alpha);

    return Lerp3(k0.value, k1.value, alpha);
}

// ============================================================================
// AnimationClip
// ============================================================================

AnimationClip::AnimationClip(std::string name)
    : m_Name(std::move(name))
{}

AnimationClip& AnimationClip::AddPositionKey(float t, XMFLOAT3 v, EasingMode e)
{
    position.AddKey(t, v, e);
    return *this;
}
AnimationClip& AnimationClip::AddRotationKey(float t, XMFLOAT3 euler, EasingMode e)
{
    rotation.AddKey(t, euler, e);
    return *this;
}
AnimationClip& AnimationClip::AddScaleKey(float t, XMFLOAT3 v, EasingMode e)
{
    scale.AddKey(t, v, e);
    return *this;
}

void AnimationClip::Sample(float t, bool loop,
                             XMFLOAT3& outPos,
                             XMFLOAT3& outRot,
                             XMFLOAT3& outScl) const
{
    // Wrap or clamp time to [0, duration]
    if (m_Duration > 0.f) {
        if (loop) {
            t = std::fmod(t, m_Duration);
            if (t < 0.f) t += m_Duration;
        } else {
            t = t < 0.f ? 0.f : (t > m_Duration ? m_Duration : t);
        }
    }

    if (position.HasKeys()) outPos = position.Sample(t);
    if (rotation.HasKeys()) outRot = rotation.Sample(t);
    if (scale.HasKeys())    outScl = scale.Sample(t);
}

} // namespace VibeEngine
