#include "HierarchicalAnimator.h"
#include "../Core/GameObject.h"
#include "../Core/Transform.h"

using namespace DirectX;

namespace VibeEngine {

// ============================================================================
// Bone registry
// ============================================================================

void HierarchicalAnimator::RegisterBone(const std::string& name, GameObject* boneGO)
{
    m_Bones[name] = boneGO;
}

void HierarchicalAnimator::UnregisterBone(const std::string& name)
{
    m_Bones.erase(name);
}

bool HierarchicalAnimator::HasBone(const std::string& name) const
{
    return m_Bones.count(name) > 0;
}

int HierarchicalAnimator::GetBoneCount() const
{
    return static_cast<int>(m_Bones.size());
}

GameObject* HierarchicalAnimator::GetBone(const std::string& name) const
{
    auto it = m_Bones.find(name);
    return (it != m_Bones.end()) ? it->second : nullptr;
}

// ============================================================================
// Playback control
// ============================================================================

void HierarchicalAnimator::Play(std::shared_ptr<HierarchicalAnimationClip> clip,
                                 bool loop, float startTime)
{
    m_Clip      = std::move(clip);
    m_Time      = startTime;
    m_Loop      = loop;
    m_Playing   = true;
    m_Paused    = false;
    m_Blending  = false;
    m_FromClip.reset();
}

void HierarchicalAnimator::CrossfadeTo(
    std::shared_ptr<HierarchicalAnimationClip> clip,
    float blendSeconds, bool loop)
{
    if (!clip) return;
    if (!m_Clip || !m_Playing) { Play(clip, loop); return; }

    m_FromClip   = m_Clip;
    m_FromTime   = m_Time;
    m_Clip       = std::move(clip);
    m_Time       = 0.f;
    m_Loop       = loop;
    m_BlendDur   = blendSeconds > 0.f ? blendSeconds : 0.001f;
    m_BlendTimer = 0.f;
    m_Blending   = true;
    m_Playing    = true;
    m_Paused     = false;
}

void HierarchicalAnimator::Stop()
{
    m_Playing  = false;
    m_Paused   = false;
    m_Blending = false;
    m_Time     = 0.f;
    m_FromClip.reset();
}

void HierarchicalAnimator::Pause()  { if (m_Playing) m_Paused = true; }
void HierarchicalAnimator::Resume() { m_Paused = false; }

float HierarchicalAnimator::GetNormalizedTime() const
{
    if (!m_Clip || m_Clip->GetDuration() <= 0.f) return 0.f;
    return m_Time / m_Clip->GetDuration();
}

// ============================================================================
// Helpers
// ============================================================================

XMFLOAT3 HierarchicalAnimator::Lerp3(const XMFLOAT3& a,
                                      const XMFLOAT3& b, float t)
{
    return { a.x + (b.x - a.x) * t,
             a.y + (b.y - a.y) * t,
             a.z + (b.z - a.z) * t };
}

// ============================================================================
// Update — sample clip(s) and write to bone Transforms
// ============================================================================

void HierarchicalAnimator::Update(float dt)
{
    if (!m_Playing || m_Paused || !m_Clip) return;

    // ---- Advance time -------------------------------------------------------
    const float advance = dt * m_Speed;
    m_Time += advance;

    if (m_Blending) {
        m_FromTime   += advance;
        m_BlendTimer += advance;
        if (m_BlendTimer >= m_BlendDur) {
            m_Blending = false;
            m_FromClip.reset();
        }
    }

    // ---- Handle non-looping clip end ----------------------------------------
    if (!m_Loop && m_Clip->GetDuration() > 0.f
                && m_Time >= m_Clip->GetDuration())
    {
        m_Time    = m_Clip->GetDuration();
        m_Playing = false;
        if (m_OnComplete) m_OnComplete();
        // Fall through to apply the final pose this frame
    }

    // ---- Sample every registered bone ---------------------------------------
    for (auto& [name, boneGO] : m_Bones) {
        if (!boneGO) continue;
        Transform* t = boneGO->GetTransform();
        if (!t) continue;

        // Initialise from current Transform — tracks with no keys are untouched
        XMFLOAT3 pos = t->GetLocalPosition();
        XMFLOAT3 rot = t->GetLocalRotation();
        XMFLOAT3 scl = t->GetLocalScale();

        // Sample the destination clip
        m_Clip->SampleBone(name, m_Time, m_Loop, pos, rot, scl);

        // ---- Crossfade blend ------------------------------------------------
        if (m_Blending && m_FromClip) {
            // Sample the outgoing clip from the SAME base Transform values
            XMFLOAT3 fPos = t->GetLocalPosition();
            XMFLOAT3 fRot = t->GetLocalRotation();
            XMFLOAT3 fScl = t->GetLocalScale();
            m_FromClip->SampleBone(name, m_FromTime, /*loop=*/true,
                                   fPos, fRot, fScl);

            // alpha 0 = fully "from" clip, alpha 1 = fully destination clip
            const float alpha = m_BlendDur > 0.f
                              ? (m_BlendTimer / m_BlendDur)
                              : 1.f;

            pos = Lerp3(fPos, pos, alpha);
            rot = Lerp3(fRot, rot, alpha);
            scl = Lerp3(fScl, scl, alpha);
        }

        // ---- Write back to Transform ----------------------------------------
        t->SetPosition(pos);
        t->SetRotation(rot);
        t->SetScale   (scl);
    }
}

} // namespace VibeEngine
