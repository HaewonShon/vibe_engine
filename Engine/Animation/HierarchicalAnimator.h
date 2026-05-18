#pragma once
#include "../Core/Component.h"
#include "HierarchicalAnimationClip.h"
#include <DirectXMath.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace VibeEngine {

class GameObject;

// ============================================================================
// HierarchicalAnimator  (Component)
//
// Drives a group of named GameObjects ("bones") from a single
// HierarchicalAnimationClip, enabling skeletal / hierarchical animation.
//
// The component can be placed on any GO (typically the skeleton root).
// Bone GOs can be anywhere in the scene; they are referenced by name.
// Parent-child Transform relationships (SetParent) automatically propagate
// each driven bone's pose down to its visual children.
//
// ---- Setup ------------------------------------------------------------------
//   auto* ctrl = rootGO->AddComponent<HierarchicalAnimator>();
//   ctrl->RegisterBone("Torso",      torsoJoint);
//   ctrl->RegisterBone("L_Shoulder", lShoulderJoint);
//   ctrl->RegisterBone("L_Elbow",    lElbowJoint);
//   // ... register all joints ...
//   ctrl->Play(walkClip, /*loop=*/true);
//
// ---- Crossfade --------------------------------------------------------------
//   ctrl->CrossfadeTo(waveClip, /*blendSeconds=*/0.4f);
//   // Both clips are linearly blended over 0.4 s, then waveClip takes over.
//
// ---- Playback control -------------------------------------------------------
//   ctrl->SetSpeed(0.5f);    // slow motion
//   ctrl->Pause() / Resume() / Stop();
//   ctrl->SetOnComplete([this]{ ... });  // fires when a non-looping clip ends
//
// ---- Notes ------------------------------------------------------------------
//   • Bones absent from the current clip are left untouched each frame.
//   • Crossfade linearly interpolates all per-bone tracks simultaneously.
//   • Compatible with Transform scale hierarchy — keep pivot GOs at scale
//     {1,1,1}; place visual meshes on separate child GOs to avoid scale bleed.
// ============================================================================
class HierarchicalAnimator : public Component {
public:
    HierarchicalAnimator()           = default;
    ~HierarchicalAnimator() override = default;

    // ---- Bone registry ------------------------------------------------------

    // Register a named bone → the pivot GameObject whose Transform is driven.
    // Overwrites any previous registration for the same name.
    void RegisterBone(const std::string& name, GameObject* boneGO);

    // Remove a bone registration (e.g. when the GO is about to be destroyed).
    void UnregisterBone(const std::string& name);

    bool        HasBone(const std::string& name) const;
    int         GetBoneCount()                   const;
    GameObject* GetBone(const std::string& name) const;

    // ---- Lifecycle ----------------------------------------------------------
    void Update(float dt) override;

    // ---- Playback -----------------------------------------------------------

    // Start playing a clip from startTime. Cancels any active crossfade.
    void Play(std::shared_ptr<HierarchicalAnimationClip> clip,
              bool loop = true, float startTime = 0.f);

    // Smoothly blend from the current clip to a new one over blendSeconds.
    void CrossfadeTo(std::shared_ptr<HierarchicalAnimationClip> clip,
                     float blendSeconds = 0.3f, bool loop = true);

    void Stop();    // stop playback, reset time to 0
    void Pause();   // freeze time, preserve last pose
    void Resume();  // continue from paused state

    // ---- Properties ---------------------------------------------------------
    void  SetSpeed(float s)   { m_Speed = s; }
    float GetSpeed()    const { return m_Speed; }
    float GetTime()     const { return m_Time;  }
    float GetNormalizedTime() const;

    bool IsPlaying()  const { return m_Playing && !m_Paused; }
    bool IsPaused()   const { return m_Paused;  }
    bool IsBlending() const { return m_Blending; }

    const HierarchicalAnimationClip* GetCurrentClip() const
    { return m_Clip.get(); }

    // ---- Callback -----------------------------------------------------------
    // Fired once when a non-looping clip reaches its end.
    void SetOnComplete(std::function<void()> fn) { m_OnComplete = std::move(fn); }

private:
    // ---- Helpers ------------------------------------------------------------
    static DirectX::XMFLOAT3 Lerp3(const DirectX::XMFLOAT3& a,
                                    const DirectX::XMFLOAT3& b, float t);

    // ---- Bone registry ------------------------------------------------------
    std::unordered_map<std::string, GameObject*> m_Bones;

    // ---- Current clip -------------------------------------------------------
    std::shared_ptr<HierarchicalAnimationClip> m_Clip;
    float m_Time  = 0.f;
    bool  m_Loop  = true;

    // ---- Crossfade "from" state ---------------------------------------------
    std::shared_ptr<HierarchicalAnimationClip> m_FromClip;
    float m_FromTime   = 0.f;
    float m_BlendDur   = 0.f;
    float m_BlendTimer = 0.f;
    bool  m_Blending   = false;

    // ---- Playback flags -----------------------------------------------------
    bool  m_Playing = false;
    bool  m_Paused  = false;
    float m_Speed   = 1.f;

    std::function<void()> m_OnComplete;
};

} // namespace VibeEngine
