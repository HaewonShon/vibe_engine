#pragma once
#include <DirectXMath.h>
#include <memory>
#include <cstdint>

namespace VibeEngine {

static constexpr uint32_t INVALID_PHYSICS_BODY = 0xFFFFFFFFu;

// ============================================================================
// PhysicsWorld  (singleton)
//
// Thin PIMPL wrapper around JoltPhysics. Jolt headers are kept entirely
// inside PhysicsWorld.cpp — nothing leaks into this header.
//
// Usage:
//   PhysicsWorld::Get().Initialize();           // once at app start
//   PhysicsWorld::Get().Update(dt);             // each frame (before scene update)
//   PhysicsWorld::Get().Shutdown();             // at app exit (or scene restart)
//
// Bodies are identified by uint32_t handles (INVALID_PHYSICS_BODY = none).
// Rigidbody component owns body creation / destruction.
// ============================================================================
class PhysicsWorld {
public:
    static PhysicsWorld& Get();

    // ---- Lifecycle ----------------------------------------------------------
    void Initialize(float gravity = -9.81f);
    void Shutdown();
    void Update(float dt, int collisionSteps = 1);

    bool IsInitialized() const { return m_Initialized; }

    // ---- Body factory -------------------------------------------------------

    // Axis-aligned box. halfExtents = half-size per axis.
    uint32_t CreateBox(
        const DirectX::XMFLOAT3& halfExtents,
        const DirectX::XMFLOAT3& position,
        const DirectX::XMFLOAT4& rotation,   // unit quaternion (x,y,z,w)
        bool  isStatic,
        float restitution = 0.2f,
        float friction    = 0.5f);

    // Sphere.
    uint32_t CreateSphere(
        float radius,
        const DirectX::XMFLOAT3& position,
        bool  isStatic,
        float restitution = 0.2f,
        float friction    = 0.5f);

    void DestroyBody(uint32_t id);

    // ---- Transform readback (dynamic bodies only) ---------------------------
    DirectX::XMFLOAT3 GetPosition(uint32_t id) const;
    DirectX::XMFLOAT4 GetRotation(uint32_t id) const;  // unit quaternion

    // ---- Forces (dynamic bodies only) ---------------------------------------
    void AddForce          (uint32_t id, const DirectX::XMFLOAT3& force);
    void AddImpulse        (uint32_t id, const DirectX::XMFLOAT3& impulse);
    void SetLinearVelocity (uint32_t id, const DirectX::XMFLOAT3& vel);
    void SetAngularVelocity(uint32_t id, const DirectX::XMFLOAT3& vel);

private:
    PhysicsWorld() = default;

    bool m_Initialized = false;

    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

} // namespace VibeEngine
