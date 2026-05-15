#include "Animator.h"
#include "../Core/GameObject.h"
#include "../Core/Transform.h"

using namespace DirectX;

namespace VibeEngine {

// ---------------------------------------------------------------------------
static XMFLOAT3 Lerp3(const XMFLOAT3& a, const XMFLOAT3& b, float t)
{
    return { a.x + (b.x - a.x) * t,
             a.y + (b.y - a.y) * t,
             a.z + (b.z - a.z) * t };
}

// ============================================================================
// Playback control
// ============================================================================

void Animator::Play(std::shared_ptr<AnimationClip> clip,
                     bool loop, float startTime)
{
    m_Clip     = std::move(clip);
    m_Time     = startTime;
    m_Loop     = loop;
    m_Playing  = true;
    m_Paused   = false;
    m_Blending = false;
    m_FromClip.reset();
}

void Animator::CrossfadeTo(std::shared_ptr<AnimationClip> clip,
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

void Animator::Stop()
{
    m_Playing  = false;
    m_Paused   = false;
    m_Blending = false;
    m_Time     = 0.f;
    m_FromClip.reset();
}

void Animator::Pause()  { if (m_Playing) m_Paused = true; }
void Animator::Resume() { m_Paused = false; }

float Animator::GetNormalizedTime() const
{
    if (!m_Clip || m_Clip->GetDuration() <= 0.f) return 0.f;
    return m_Time / m_Clip->GetDuration();
}

// ============================================================================
// Update
// ============================================================================
void Animator::Update(float dt)
{
    if (!m_Playing || m_Paused || !m_Clip) return;

    auto* go = GetGameObject();
    if (!go) return;
    auto* t = go->GetTransform();
    if (!t) return;

    // --- Advance time -------------------------------------------------------
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

    // --- Handle non-looping end ---------------------------------------------
    if (!m_Loop && m_Clip->GetDuration() > 0.f
                && m_Time >= m_Clip->GetDuration())
    {
        m_Time    = m_Clip->GetDuration();
        m_Playing = false;
        if (m_OnComplete) m_OnComplete();
        // Still apply the final pose below before returning
    }

    // --- Sample current clip ------------------------------------------------
    // Initialise outputs from current Transform so that tracks with no keys
    // leave those axes unchanged.
    XMFLOAT3 pos = t->GetLocalPosition();
    XMFLOAT3 rot = t->GetLocalRotation();
    XMFLOAT3 scl = t->GetLocalScale();

    m_Clip->Sample(m_Time, m_Loop, pos, rot, scl);

    // --- Crossfade blend with "from" clip -----------------------------------
    if (m_Blending && m_FromClip) {
        // Sample the outgoing clip starting from the same base values
        XMFLOAT3 fPos = t->GetLocalPosition();
        XMFLOAT3 fRot = t->GetLocalRotation();
        XMFLOAT3 fScl = t->GetLocalScale();
        m_FromClip->Sample(m_FromTime, /*loop=*/true, fPos, fRot, fScl);

        // alpha: 0 = fully "from" clip, 1 = fully current clip
        const float alpha = m_BlendDur > 0.f
                          ? (m_BlendTimer / m_BlendDur)
                          : 1.f;

        pos = Lerp3(fPos, pos, alpha);
        rot = Lerp3(fRot, rot, alpha);
        scl = Lerp3(fScl, scl, alpha);
    }

    // --- Write back to Transform --------------------------------------------
    t->SetPosition(pos);
    t->SetRotation(rot);
    t->SetScale   (scl);
}

} // namespace VibeEngine
