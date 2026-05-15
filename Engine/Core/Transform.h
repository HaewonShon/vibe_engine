#pragma once
#include "Component.h"
#include <DirectXMath.h>
#include <vector>

namespace VibeEngine {

// ============================================================================
// Transform
//
// Rotation is stored internally as a unit quaternion (gimbal-lock-free).
// Euler-degree helpers are provided for convenience — they convert to/from
// the quaternion, so accumulating small Euler deltas never drifts.
//
// ---- Position / Scale -------------------------------------------------------
//   SetPosition({ x, y, z })
//   Translate ({ dx, dy, dz })            world-space offset
//   SetScale  ({ x, y, z })
//
// ---- Rotation — Euler interface (degrees, XYZ = pitch/yaw/roll) ------------
//   SetRotation({ pitchDeg, yawDeg, rollDeg })   replaces current rotation
//   Rotate(yawDeg, pitchDeg, rollDeg)            incremental delta (local space)
//   GetLocalRotation() -> XMFLOAT3               extracted Euler (may differ from
//                                                 what was set — quat is canonical)
//
// ---- Rotation — Quaternion interface ----------------------------------------
//   SetRotationQuat(XMFLOAT4)            set directly as (x,y,z,w)
//   GetRotationQuat() -> XMFLOAT4
//   RotateAxisAngle(axis, angleDeg)      rotate around an arbitrary world axis
//
// ---- Orientation helpers ----------------------------------------------------
//   GetForward() -> XMFLOAT3    local +Z in world space
//   GetRight()   -> XMFLOAT3    local +X in world space
//   GetUp()      -> XMFLOAT3    local +Y in world space
//   LookAt(target [, up])       orient so that +Z faces target
//
// ---- Hierarchy --------------------------------------------------------------
//   SetParent(Transform*)
//   GetWorldMatrix()    accumulates parent chain
// ============================================================================
class Transform : public Component {
public:
    Transform();

    // ---- Position -----------------------------------------------------------
    void SetPosition(const DirectX::XMFLOAT3& pos) { m_Position = pos; }
    void Translate  (const DirectX::XMFLOAT3& delta);

    const DirectX::XMFLOAT3& GetLocalPosition() const { return m_Position; }

    // ---- Scale --------------------------------------------------------------
    void SetScale(const DirectX::XMFLOAT3& scale) { m_Scale = scale; }
    const DirectX::XMFLOAT3& GetLocalScale() const { return m_Scale; }

    // ---- Rotation: Euler interface (degrees) --------------------------------

    // Replace the current rotation with the given Euler angles (degrees).
    // Internally converts to quaternion — no gimbal lock accumulation.
    void SetRotation(const DirectX::XMFLOAT3& eulerDegrees);

    // Apply an incremental rotation in local space (degrees).
    // Equivalent to: quat = quat * fromEuler(delta)
    void Rotate(float yawDeg, float pitchDeg, float rollDeg);

    // Extract Euler angles (degrees) from the stored quaternion.
    // Follows the same Pitch-Yaw-Roll (XYZ) convention as SetRotation.
    // Note: conversion is deterministic but not always round-trip identical.
    DirectX::XMFLOAT3 GetLocalRotation() const;

    // ---- Rotation: Quaternion interface -------------------------------------

    // Set rotation directly as a quaternion (x,y,z,w).  Must be unit length.
    void SetRotationQuat(const DirectX::XMFLOAT4& quat) { m_Quaternion = quat; }
    DirectX::XMFLOAT4 GetRotationQuat() const           { return m_Quaternion; }

    // Rotate by angleDeg degrees around the given world-space axis.
    void RotateAxisAngle(const DirectX::XMFLOAT3& axis, float angleDeg);

    // ---- Orientation helpers ------------------------------------------------

    // Local +Z axis in world space (the "look" direction).
    DirectX::XMFLOAT3 GetForward() const;

    // Local +X axis in world space.
    DirectX::XMFLOAT3 GetRight() const;

    // Local +Y axis in world space.
    DirectX::XMFLOAT3 GetUp() const;

    // Orient so that local +Z points from current position toward 'target'.
    // 'up' is the world-space hint for the Y axis (default: world up).
    void LookAt(const DirectX::XMFLOAT3& target,
                const DirectX::XMFLOAT3& up = { 0.f, 1.f, 0.f });

    // ---- Matrices -----------------------------------------------------------
    DirectX::XMMATRIX GetLocalMatrix() const;
    DirectX::XMMATRIX GetWorldMatrix() const;

    // ---- Hierarchy ----------------------------------------------------------
    void       SetParent(Transform* parent);
    Transform* GetParent()                  const { return m_Parent; }
    const std::vector<Transform*>& GetChildren() const { return m_Children; }

private:
    DirectX::XMFLOAT3 m_Position   = { 0.f, 0.f, 0.f };
    DirectX::XMFLOAT4 m_Quaternion = { 0.f, 0.f, 0.f, 1.f }; // identity
    DirectX::XMFLOAT3 m_Scale      = { 1.f, 1.f, 1.f };

    Transform*              m_Parent   = nullptr;
    std::vector<Transform*> m_Children;
};

} // namespace VibeEngine
