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
// first Update call, or in your scene-setup code before Start runs).
//
// ---- Shape selection --------------------------------------------------------
//   rb->SetBoxHalfExtents({ 0.5f, 0.5f, 0.5f });   // default — unit box
//   rb->SetSphereRadius(0.5f);
//   rb->SetCapsuleShape(0.3f, 0.7f);                // radius=0.3, halfHeight=0.7
//
// ---- Physics properties (set before Start) -----------------------------------
//   rb->SetStatic(true);          // immovable; Transform is set once at Start
//   rb->SetKinematic(true);       // moved by SetPosition/SetRotation; no gravity
//   rb->SetTrigger(true);         // overlap detection only; no physical response
//   rb->SetMass(10.f);            // custom mass in kg (0 = auto from shape density)
//   rb->SetRestitution(0.4f);     // bounciness  0=dead  1=perfectly elastic
//   rb->SetFriction(0.6f);        // surface friction
//
// ---- Runtime API (dynamic/kinematic bodies) ---------------------------------
//   rb->AddForce({ 0, 500, 0 });
//   rb->AddImpulse({ 0, 10, 0 });
//   rb->SetLinearVelocity({ 2, 0, 0 });
//   rb->GetLinearVelocity();
//   rb->SetPosition({ 0, 5, 0 });     // teleport (also updates Transform)
//
// ---- Collision callbacks (override on any Component of the same GO) ----------
//   void OnCollisionEnter(Rigidbody* other) override { ... }
//   void OnCollisionExit (Rigidbody* other) override { ... }
//   void OnTriggerEnter  (Rigidbody* other) override { ... }
//   void OnTriggerExit   (Rigidbody* other) override { ... }
//
// Lifecycle:
//   Start()     — reads Transform, creates Jolt body, registers with PhysicsWorld
//   Update(dt)  — dynamic bodies: reads Jolt pos/rot → writes Transform
//   OnDestroy() — unregisters from PhysicsWorld, destroys Jolt body
// ============================================================================
class Rigidbody : public Component {
public:
    Rigidbody()           = default;
    ~Rigidbody() override = default;

    // ---- Shape type (public so SceneSerializer can inspect it) --------------
    enum class ShapeType { Box, Sphere, Capsule };

    // ---- Lifecycle ----------------------------------------------------------
    void Start()            override;
    void Update(float dt)   override;
    void OnDestroy()        override;

    // ---- Shape (call before scene Start) ------------------------------------
    void SetBoxHalfExtents(const DirectX::XMFLOAT3& he)
    {
        m_ShapeType   = ShapeType::Box;
        m_HalfExtents = he;
    }
    void SetSphereRadius(float r)
    {
        m_ShapeType = ShapeType::Sphere;
        m_Radius    = r;
    }
    // Capsule: total height = 2 * (halfHeight + radius).
    // Good default for characters: radius=0.3, halfHeight=0.7 → 2 m tall.
    void SetCapsuleShape(float radius, float halfHeight)
    {
        m_ShapeType      = ShapeType::Capsule;
        m_Radius         = radius;
        m_CapsuleHalfHeight = halfHeight;
    }

    // ---- Body properties (call before scene Start) --------------------------
    void SetStatic     (bool s)   { m_IsStatic    = s; }
    void SetKinematic  (bool k)   { m_IsKinematic = k; }
    void SetTrigger    (bool t)   { m_IsTrigger   = t; }
    void SetMass       (float m)  { m_Mass        = m; }
    void SetRestitution(float r)  { m_Restitution = r; }
    void SetFriction   (float f)  { m_Friction    = f; }

    // ---- Runtime forces (dynamic bodies only) --------------------------------
    void AddForce          (const DirectX::XMFLOAT3& force);
    void AddImpulse        (const DirectX::XMFLOAT3& impulse);
    void SetLinearVelocity (const DirectX::XMFLOAT3& vel);
    void SetAngularVelocity(const DirectX::XMFLOAT3& vel);

    // ---- Runtime transform (dynamic / kinematic) ----------------------------
    // Teleports the physics body AND updates the GO's Transform immediately.
    void SetPosition(const DirectX::XMFLOAT3& pos);
    void SetRotation(const DirectX::XMFLOAT4& quatXYZW);

    // ---- Velocity read -------------------------------------------------------
    DirectX::XMFLOAT3 GetLinearVelocity()  const;
    DirectX::XMFLOAT3 GetAngularVelocity() const;

    // ---- State query ---------------------------------------------------------
    bool     IsStatic()    const { return m_IsStatic;    }
    bool     IsKinematic() const { return m_IsKinematic; }
    bool     IsTrigger()   const { return m_IsTrigger;   }
    bool     IsValid()     const { return m_BodyID != 0xFFFFFFFFu; }
    uint32_t GetBodyID()   const { return m_BodyID; }

    // ---- Config read (used by SceneSerializer) --------------------------------
    ShapeType         GetShapeType()         const { return m_ShapeType; }
    DirectX::XMFLOAT3 GetHalfExtents()       const { return m_HalfExtents; }
    float             GetRadius()            const { return m_Radius; }
    float             GetCapsuleHalfHeight() const { return m_CapsuleHalfHeight; }
    float             GetMass()              const { return m_Mass; }
    float             GetRestitution()       const { return m_Restitution; }
    float             GetFriction()          const { return m_Friction; }

    // ---- Contact dispatch (called by PhysicsWorld — not user API) -----------
    void DispatchContactBegin(Rigidbody* other, bool isTrigger);
    void DispatchContactEnd  (Rigidbody* other, bool isTrigger);

private:
    // Shape
    ShapeType             m_ShapeType         = ShapeType::Box;
    DirectX::XMFLOAT3     m_HalfExtents       = { 0.5f, 0.5f, 0.5f };
    float                 m_Radius            = 0.5f;
    float                 m_CapsuleHalfHeight = 0.7f;

    // Body properties
    bool  m_IsStatic    = false;
    bool  m_IsKinematic = false;
    bool  m_IsTrigger   = false;
    float m_Mass        = 0.f;   // 0 = auto from shape density
    float m_Restitution = 0.2f;
    float m_Friction    = 0.5f;

    uint32_t m_BodyID = 0xFFFFFFFFu;  // INVALID_PHYSICS_BODY
};

} // namespace VibeEngine
