#include "HierarchicalAnimationClip.h"
#include <cmath>

using namespace DirectX;

namespace VibeEngine {

HierarchicalAnimationClip::HierarchicalAnimationClip(std::string name)
    : m_Name(std::move(name))
{}

// ============================================================================
// Fluent key builders
// ============================================================================

HierarchicalAnimationClip& HierarchicalAnimationClip::AddPositionKey(
    const std::string& bone, float t, XMFLOAT3 v, EasingMode e)
{
    GetOrCreate(bone).position.AddKey(t, v, e);
    return *this;
}

HierarchicalAnimationClip& HierarchicalAnimationClip::AddRotationKey(
    const std::string& bone, float t, XMFLOAT3 euler, EasingMode e)
{
    GetOrCreate(bone).rotation.AddKey(t, euler, e);
    return *this;
}

HierarchicalAnimationClip& HierarchicalAnimationClip::AddScaleKey(
    const std::string& bone, float t, XMFLOAT3 v, EasingMode e)
{
    GetOrCreate(bone).scale.AddKey(t, v, e);
    return *this;
}

// ============================================================================
// Sampling
// ============================================================================

void HierarchicalAnimationClip::SampleBone(
    const std::string& bone, float t, bool loop,
    XMFLOAT3& outPos, XMFLOAT3& outRot, XMFLOAT3& outScl) const
{
    auto it = m_Bones.find(bone);
    if (it == m_Bones.end()) return; // this clip has no tracks for this bone

    // Wrap or clamp playback time to [0, duration]
    float st = t;
    if (m_Duration > 0.f) {
        if (loop) {
            st = std::fmod(t, m_Duration);
            if (st < 0.f) st += m_Duration;
        } else {
            st = t < 0.f ? 0.f : (t > m_Duration ? m_Duration : t);
        }
    }

    const BoneTracks& tr = it->second;
    if (tr.position.HasKeys()) outPos = tr.position.Sample(st);
    if (tr.rotation.HasKeys()) outRot = tr.rotation.Sample(st);
    if (tr.scale.HasKeys())    outScl = tr.scale.Sample(st);
}

} // namespace VibeEngine
