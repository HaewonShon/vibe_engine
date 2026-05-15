#pragma once
#include "../Core/Component.h"
#include <DirectXMath.h>
#include <cstdint>

namespace VibeEngine {

// ============================================================================
// Rigidbody  (Component)
//
// Bridges a GameObject's Transform with a JoltPhysics body.
// Configure shape and properties BEFORE the scene starts (i.e. before the
// first Update call, or in OnInit before the scene runs).
//
// ---- Shape selection --------------------------------------------------------
//   rb->SetBoxHalfExtents({ 0.5f, 0.5f, 0.5f });   // unit box
//   rb->SetSphereRadius(0.5f);
//
// ---- Physics properties -----------------------------------------------------
//   rb->SetStatic(true);          // immovable; Transform is set once and kept
//   rb->SetRestitution(0.4f);     // bounciness  0=dead  1=perfectly elastic
//   rb->SetFriction   (0.6f);     // surface friction
//
// ---- Runtime (dynamic bodies only) -----------------------------------------
//   rb->AddForce   ({ 0, 500, 0 });
//   rb->AddImpulse ({ 0,  10, 0 });
//   rb->SetLinearVelocity({ 2, 0, 0 });
//
// Lifecycle:
//   Start()     — reads Transform for initial position/rotation, creates Jolt body
//   Update(dt)  — for dynamic bodies: reads Jolt position/rotation → writes Transform
//   OnDestroy() — removes and destroys the Jolt body
// ============================================================================
class Rigidbody : public Component {
public:
    Rigidbody()           = default;
    ~Rigidbody() override = default;

    // ---- Lifecycle ----------------------------------------------------------
    void Start()            override;
    void Update(float dt)   override;
    void OnDestroy()        override;

    // ---- Shape --------------------------------------------------------------
    void SetBoxHalfExtents(const DirectX::XMFLOAT3& he)
    {
        m_UseBox      = true;
        m_HalfExtents = he;
    }
    void SetSphereRadius(float r)
    {
        m_UseBox = false;
        m_Radius = r;
    }

    // ---- Properties (call before scene Start) --------------------------------
    void SetStatic     (bool s)   { m_IsStatic    = s; }
    void SetRestitution(float r)  { m_Restitution = r; }
    void SetFriction   (float f)  { m_Friction    = f; }

    // ---- Runtime forces (dynamic bodies only) --------------------------------
    void AddForce          (const DirectX::XMFLOAT3& force);
    void AddImpulse        (const DirectX::XMFLOAT3& impulse);
    void SetLinearVelocity (const DirectX::XMFLOAT3& vel);
    void SetAngularVelocity(const DirectX::XMFLOAT3& vel);

    // ---- State query ---------------------------------------------------------
    bool     IsStatic() const { return m_IsStatic; }
    bool     IsValid()  const { return m_BodyID != 0xFFFFFFFFu; }
    uint32_t GetBodyID()const { return m_BodyID; }

private:
    // Shape
    bool                  m_UseBox      = true;
    DirectX::XMFLOAT3     m_HalfExtents = { 0.5f, 0.5f, 0.5f };
    float                 m_Radius      = 0.5f;

    // Physics properties
    float m_Restitution = 0.2f;
    float m_Friction    = 0.5f;
    bool  m_IsStatic    = false;

    uint32_t m_BodyID   = 0xFFFFFFFFu; // INVALID_PHYSICS_BODY
};

} // namespace VibeEngine
