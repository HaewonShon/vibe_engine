#pragma once
#include "Easing.h"
#include <DirectXMath.h>
#include <vector>
#include <string>

namespace VibeEngine {

// ============================================================================
// Float3Track — sequence of XMFLOAT3 keyframes with per-key easing
// ============================================================================
struct Float3Track {
    struct Key {
        float                 time   = 0.f;
        DirectX::XMFLOAT3     value  = {};
        EasingMode            easing = EasingMode::Linear;
    };

    std::vector<Key> keys;

    bool HasKeys() const { return !keys.empty(); }

    void AddKey(float time, DirectX::XMFLOAT3 value,
                EasingMode easing = EasingMode::Linear);

    // Sample the track at time t (already clamped / wrapped by AnimationClip).
    DirectX::XMFLOAT3 Sample(float t) const;

    void Sort(); // sort keys ascending by time (called automatically in AddKey)
};

// ============================================================================
// AnimationClip
//
// Holds up to three Float3Tracks:  position, rotation (euler °), scale.
// Tracks with no keys leave the corresponding Transform component unchanged.
//
// Quick setup (fluent builder):
//   auto clip = std::make_shared<AnimationClip>("bob");
//   clip->SetDuration(2.f);
//   clip->AddPositionKey(0.f, {0,1,0})
//       .AddPositionKey(1.f, {0,3,0}, EasingMode::SineInOut)
//       .AddPositionKey(2.f, {0,1,0}, EasingMode::SineInOut);
//   clip->AddRotationKey(0.f, {0,  0, 0})
//       .AddRotationKey(2.f, {0,360,0});
// ============================================================================
class AnimationClip {
public:
    explicit AnimationClip(std::string name = "clip");

    // ---- Metadata -----------------------------------------------------------
    const std::string& GetName()        const { return m_Name;     }
    float              GetDuration()    const { return m_Duration;  }
    void               SetDuration(float d)   { m_Duration = d;    }

    // ---- Tracks (access directly or via fluent helpers) ---------------------
    Float3Track position;   // local position (metres)
    Float3Track rotation;   // Euler angles in degrees (XYZ = pitch / yaw / roll)
    Float3Track scale;      // local scale (1 = original size)

    // Fluent builder — each method returns *this for chaining
    AnimationClip& AddPositionKey(float t, DirectX::XMFLOAT3 v,
                                   EasingMode e = EasingMode::Linear);
    AnimationClip& AddRotationKey(float t, DirectX::XMFLOAT3 eulerDeg,
                                   EasingMode e = EasingMode::Linear);
    AnimationClip& AddScaleKey   (float t, DirectX::XMFLOAT3 v,
                                   EasingMode e = EasingMode::Linear);

    // ---- Sampling -----------------------------------------------------------
    // Fills outPos/outRot/outScl for the given playback time.
    // Tracks with no keys leave the corresponding output UNCHANGED.
    // Caller should initialise outputs from the current Transform before calling.
    void Sample(float t, bool loop,
                DirectX::XMFLOAT3& outPos,
                DirectX::XMFLOAT3& outRot,
                DirectX::XMFLOAT3& outScl) const;

private:
    std::string m_Name;
    float       m_Duration = 1.f;
};

} // namespace VibeEngine
