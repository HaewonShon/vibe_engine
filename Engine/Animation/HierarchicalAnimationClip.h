#pragma once
#include "AnimationClip.h"   // reuses Float3Track + EasingMode
#include <string>
#include <unordered_map>

namespace VibeEngine {

// ============================================================================
// HierarchicalAnimationClip
//
// A single clip that can drive any number of named "bones" simultaneously.
// Each bone gets its own position / rotation (Euler °) / scale Float3Tracks.
// Bones with no keys for a given track leave that axis of the Transform alone.
//
// ---- Quick setup (fluent builder) -------------------------------------------
//   auto clip = std::make_shared<HierarchicalAnimationClip>("walk");
//   clip->SetDuration(4.f);
//
//   // Hip swing: leg forward(+) / backward(-)  using X rotation
//   clip->AddRotationKey("L_Hip", 0.f, {-25,0,0})
//       .AddRotationKey("L_Hip", 2.f, { 25,0,0}, EasingMode::SineInOut)
//       .AddRotationKey("L_Hip", 4.f, {-25,0,0}, EasingMode::SineInOut);
//
//   clip->AddRotationKey("R_Hip", 0.f, { 25,0,0})
//       .AddRotationKey("R_Hip", 2.f, {-25,0,0}, EasingMode::SineInOut)
//       .AddRotationKey("R_Hip", 4.f, { 25,0,0}, EasingMode::SineInOut);
//
// ---- Notes ------------------------------------------------------------------
//   • Uses the same Float3Track / EasingMode machinery as AnimationClip.
//   • Bone names are arbitrary strings — they must match the names registered
//     in HierarchicalAnimator::RegisterBone().
//   • Bones present in the clip but not registered in the Animator are silently
//     skipped; registered bones absent from the clip are untouched.
// ============================================================================
class HierarchicalAnimationClip {
public:
    // ---- Per-bone track group -----------------------------------------------
    struct BoneTracks {
        Float3Track position;   // local position (metres)
        Float3Track rotation;   // Euler degrees (XYZ = pitch/yaw/roll)
        Float3Track scale;      // local scale
    };

    explicit HierarchicalAnimationClip(std::string name = "clip");

    // ---- Metadata -----------------------------------------------------------
    const std::string& GetName()     const { return m_Name;     }
    float              GetDuration() const { return m_Duration; }
    void               SetDuration(float d) { m_Duration = d;   }

    // ---- Fluent key builders — return *this for chaining -------------------
    HierarchicalAnimationClip& AddPositionKey(const std::string& bone,
                                               float t, DirectX::XMFLOAT3 v,
                                               EasingMode e = EasingMode::Linear);

    HierarchicalAnimationClip& AddRotationKey(const std::string& bone,
                                               float t, DirectX::XMFLOAT3 eulerDeg,
                                               EasingMode e = EasingMode::Linear);

    HierarchicalAnimationClip& AddScaleKey   (const std::string& bone,
                                               float t, DirectX::XMFLOAT3 v,
                                               EasingMode e = EasingMode::Linear);

    // ---- Sampling -----------------------------------------------------------
    // Fills outPos/outRot/outScl for the named bone at playback time t.
    // outPos/outRot/outScl must be pre-initialised (e.g. from the current
    // Transform) — tracks with no keys leave the corresponding output unchanged.
    void SampleBone(const std::string& bone, float t, bool loop,
                    DirectX::XMFLOAT3& outPos,
                    DirectX::XMFLOAT3& outRot,
                    DirectX::XMFLOAT3& outScl) const;

    // Access to the full bone map (used by HierarchicalAnimator for iteration).
    const std::unordered_map<std::string, BoneTracks>& GetAllBoneTracks() const
    { return m_Bones; }

    bool HasBone(const std::string& bone) const
    { return m_Bones.count(bone) > 0; }

private:
    BoneTracks& GetOrCreate(const std::string& bone) { return m_Bones[bone]; }

    std::string m_Name;
    float       m_Duration = 1.f;
    std::unordered_map<std::string, BoneTracks> m_Bones;
};

} // namespace VibeEngine
