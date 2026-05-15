#pragma once
#include <DirectXMath.h>
#include <memory>
#include <cstdint>

namespace VibeEngine {

static constexpr uint32_t INVALID_PHYSICS_BODY = 0xFFFFFFFFu;

class Rigidbody;   // forward-declare; PhysicsWorld.cpp includes Rigidbody.h

// ============================================================================
// RaycastHit
// Returned by PhysicsWorld::CastRay.  Check 'hit' before reading other fields.
// ============================================================================
struct RaycastHit {
    bool                   hit      = false;
    float                  distance = 0.f;
    uint32_t               bodyID   = INVALID_PHYSICS_BODY;
    DirectX::XMFLOAT3      point    = {};   // world-space contact point
    DirectX::XMFLOAT3      normal   = {};   // outward surface normal at hit point
    Rigidbody*             rigidbody = nullptr; // nullptr if body has no Rigidbody component
};

// ============================================================================
// PhysicsWorld  (singleton)
//
// Thin PIMPL wrapper around JoltPhysics. Jolt headers are kept entirely
// inside PhysicsWorld.cpp — nothing leaks into this header.
//
// Usage:
//   PhysicsWorld::Get().Initialize();          // once at app start
//   PhysicsWorld::Get().Update(dt);            // each frame (before scene update)
//   PhysicsWorld::Get().Shutdown();            // at app exit (or scene restart)
//
// Bodies are identified by uint32_t handles (INVALID_PHYSICS_BODY = none).
// Rigidbody component owns body creation / destruction / registry.
// ============================================================================
class PhysicsWorld {
public:
    static PhysicsWorld& Get();

    // ---- Lifecycle ----------------------------------------------------------
    void Initialize(float gravity = -9.81f);
    void Shutdown();

    // Step the simulation by dt seconds.
    // Contact events collected during this step are dispatched to Rigidbody
    // components at the end of this call (before it returns).
    void Update(float dt, int collisionSteps = 1);

    bool IsInitialized() const { return m_Initialized; }

    // ---- Body factory -------------------------------------------------------

    // Axis-aligned box. halfExtents = half-size per axis.
    // Pass mass > 0 to override the shape-derived mass (dynamic bodies only).
    // isSensor = true creates a trigger volume (no physical response).
    uint32_t CreateBox(
        const DirectX::XMFLOAT3& halfExtents,
        const DirectX::XMFLOAT3& position,
        const DirectX::XMFLOAT4& rotation,   // unit quaternion (x,y,z,w)
        bool  isStatic,
        float mass        = 0.f,
        float restitution = 0.2f,
        float friction    = 0.5f,
        bool  isSensor    = false);

    // Sphere.
    uint32_t CreateSphere(
        float radius,
        const DirectX::XMFLOAT3& position,
        bool  isStatic,
        float mass        = 0.f,
        float restitution = 0.2f,
        float friction    = 0.5f,
        bool  isSensor    = false);

    // Capsule (cylinder with hemi-spherical end-caps).
    // halfHeight = half the cylinder section height (total height = 2*halfHeight + 2*radius).
    uint32_t CreateCapsule(
        float radius,
        float halfHeight,
        const DirectX::XMFLOAT3& position,
        const DirectX::XMFLOAT4& rotation,
        bool  isStatic,
        float mass        = 0.f,
        float restitution = 0.2f,
        float friction    = 0.5f,
        bool  isSensor    = false);

    void DestroyBody(uint32_t id);

    // ---- Motion type --------------------------------------------------------

    // Switch a dynamic body to kinematic (or back).  No-op on static bodies.
    // Kinematic bodies are moved via SetBodyPosition / SetBodyRotation each frame.
    void SetBodyKinematic(uint32_t id, bool kinematic);

    // ---- Transform (runtime) ------------------------------------------------

    // Teleport a body to a new world position / rotation.
    // Works for dynamic and kinematic bodies.  Wakes a sleeping body.
    void SetBodyPosition(uint32_t id, const DirectX::XMFLOAT3& pos);
    void SetBodyRotation(uint32_t id, const DirectX::XMFLOAT4& rot);

    // ---- Transform readback (dynamic / kinematic bodies) --------------------
    DirectX::XMFLOAT3 GetPosition(uint32_t id) const;
    DirectX::XMFLOAT4 GetRotation(uint32_t id) const;  // unit quaternion

    // ---- Velocity -----------------------------------------------------------
    DirectX::XMFLOAT3 GetLinearVelocity (uint32_t id) const;
    DirectX::XMFLOAT3 GetAngularVelocity(uint32_t id) const;

    void AddForce          (uint32_t id, const DirectX::XMFLOAT3& force);
    void AddImpulse        (uint32_t id, const DirectX::XMFLOAT3& impulse);
    void SetLinearVelocity (uint32_t id, const DirectX::XMFLOAT3& vel);
    void SetAngularVelocity(uint32_t id, const DirectX::XMFLOAT3& vel);

    // ---- Raycasting ---------------------------------------------------------

    // Cast a ray from 'origin' in 'direction' (should be normalised) up to
    // maxDistance world units.  Returns a RaycastHit; check hit.hit before use.
    RaycastHit CastRay(
        const DirectX::XMFLOAT3& origin,
        const DirectX::XMFLOAT3& direction,
        float maxDistance = 1000.f) const;

    // ---- Body registry (used by Rigidbody component) ------------------------

    // Rigidbody::Start() calls RegisterBody; OnDestroy() calls UnregisterBody.
    // These let PhysicsWorld dispatch contact events to the right component.
    void RegisterBody  (uint32_t id, Rigidbody* rb);
    void UnregisterBody(uint32_t id);

private:
    PhysicsWorld() = default;

    // Dispatch queued contact events (called internally at end of Update).
    void DispatchContactEvents();

    // Look up a Rigidbody by body ID; returns nullptr if not registered.
    Rigidbody* GetRigidbody(uint32_t id) const;

    bool m_Initialized = false;

    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

} // namespace VibeEngine
