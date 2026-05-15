#pragma once
#include "../Core/Component.h"
#include "AnimationClip.h"
#include <memory>
#include <functional>

namespace VibeEngine {

// ============================================================================
// Animator  (Component)
//
// Plays AnimationClips on the owning GameObject's Transform.
// Supports looping, speed scaling, clip crossfade (linear blend), and
// an OnComplete callback for non-looping clips.
//
// ---- Basic playback --------------------------------------------------------
//   auto* anim = go->AddComponent<Animator>();
//   anim->Play(clip, /*loop=*/true);
//
// ---- Crossfade -------------------------------------------------------------
//   anim->CrossfadeTo(otherClip, /*blendSeconds=*/0.3f);
//   // Both clips are blended over 0.3 s, then 'otherClip' takes over.
//
// ---- Callbacks -------------------------------------------------------------
//   anim->SetOnComplete([this]{ /* fired when a non-looping clip ends */ });
//
// ---- Notes -----------------------------------------------------------------
// • Animator writes to the owning Transform every Update.
// • If the GameObject also has a physics Rigidbody, mark it kinematic first:
//   rb->SetKinematic(true);  (Animator does NOT call Rigidbody::SetPosition)
// • Tracks with no keyframes leave the corresponding Transform axis unchanged.
// ============================================================================
class Animator : public Component {
public:
    Animator()           = default;
    ~Animator() override = default;

    // ---- Lifecycle ----------------------------------------------------------
    void Update(float dt) override;

    // ---- Playback -----------------------------------------------------------
    // Play a clip from 'startTime'. Interrupts any current crossfade.
    void Play(std::shared_ptr<AnimationClip> clip,
              bool loop = true, float startTime = 0.f);

    // Begin a smooth crossfade: current clip fades out over 'blendSeconds'
    // while 'clip' fades in. Both are sampled and linearly blended.
    void CrossfadeTo(std::shared_ptr<AnimationClip> clip,
                     float blendSeconds = 0.25f, bool loop = true);

    void Stop();    // stops playback and resets time to 0
    void Pause();   // suspends time advance; keeps last pose
    void Resume();  // resumes from paused state

    // ---- Properties ---------------------------------------------------------
    void  SetSpeed(float s) { m_Speed = s; }
    float GetSpeed()        const { return m_Speed; }

    // Elapsed time in the current clip (seconds)
    float GetTime()           const { return m_Time; }

    // 0 = start, 1 = end of clip duration
    float GetNormalizedTime() const;

    bool IsPlaying()  const { return m_Playing && !m_Paused; }
    bool IsPaused()   const { return m_Paused;  }
    bool IsBlending() const { return m_Blending; }

    const AnimationClip* GetCurrentClip() const { return m_Clip.get(); }

    // ---- Callback -----------------------------------------------------------
    void SetOnComplete(std::function<void()> fn) { m_OnComplete = std::move(fn); }

private:
    // ---- Current clip -------------------------------------------------------
    std::shared_ptr<AnimationClip> m_Clip;
    float m_Time  = 0.f;
    bool  m_Loop  = true;

    // ---- Crossfade "from" state ---------------------------------------------
    std::shared_ptr<AnimationClip> m_FromClip;
    float m_FromTime   = 0.f;   // time in the "from" clip
    float m_BlendDur   = 0.f;   // total blend duration
    float m_BlendTimer = 0.f;   // elapsed blend time (0 → BlendDur)
    bool  m_Blending   = false;

    // ---- Playback flags -----------------------------------------------------
    bool  m_Playing = false;
    bool  m_Paused  = false;
    float m_Speed   = 1.f;

    std::function<void()> m_OnComplete;
};

} // namespace VibeEngine
