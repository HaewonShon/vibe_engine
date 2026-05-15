#include "Rigidbody.h"
#include "PhysicsWorld.h"
#include "../Core/GameObject.h"
#include "../Core/Transform.h"

using namespace DirectX;

namespace VibeEngine {

// ============================================================================
// Lifecycle
// ============================================================================

void Rigidbody::Start()
{
    auto& pw = PhysicsWorld::Get();
    if (!pw.IsInitialized()) return;

    auto* t   = GetGameObject()->GetTransform();
    auto  pos = t->GetLocalPosition();
    auto  rot = t->GetRotationQuat();

    switch (m_ShapeType) {
    case ShapeType::Box:
        m_BodyID = pw.CreateBox(m_HalfExtents, pos, rot,
                                m_IsStatic, m_Mass, m_Restitution, m_Friction, m_IsTrigger);
        break;
    case ShapeType::Sphere:
        m_BodyID = pw.CreateSphere(m_Radius, pos,
                                   m_IsStatic, m_Mass, m_Restitution, m_Friction, m_IsTrigger);
        break;
    case ShapeType::Capsule:
        m_BodyID = pw.CreateCapsule(m_Radius, m_CapsuleHalfHeight, pos, rot,
                                    m_IsStatic, m_Mass, m_Restitution, m_Friction, m_IsTrigger);
        break;
    }

    if (m_BodyID == 0xFFFFFFFFu) return;

    // Apply kinematic motion type post-creation (body was created as dynamic).
    if (m_IsKinematic && !m_IsStatic)
        pw.SetBodyKinematic(m_BodyID, true);

    // Register so PhysicsWorld can dispatch contact events to us.
    pw.RegisterBody(m_BodyID, this);
}

void Rigidbody::Update(float /*dt*/)
{
    // Static and trigger bodies never move — no readback needed.
    if ((m_IsStatic && !m_IsTrigger) || m_BodyID == 0xFFFFFFFFu) return;
    // Kinematic bodies are moved by user code, not by physics readback.
    if (m_IsKinematic) return;

    auto& pw = PhysicsWorld::Get();
    if (!pw.IsInitialized()) return;

    auto* t = GetGameObject()->GetTransform();
    t->SetPosition   (pw.GetPosition(m_BodyID));
    t->SetRotationQuat(pw.GetRotation(m_BodyID));
}

void Rigidbody::OnDestroy()
{
    if (m_BodyID == 0xFFFFFFFFu) return;
    auto& pw = PhysicsWorld::Get();
    if (pw.IsInitialized()) {
        pw.UnregisterBody(m_BodyID);   // unregister BEFORE destroying the body
        pw.DestroyBody(m_BodyID);
    }
    m_BodyID = 0xFFFFFFFFu;
}

// ============================================================================
// Forces (dynamic bodies only)
// ============================================================================

void Rigidbody::AddForce(const XMFLOAT3& force)
{
    PhysicsWorld::Get().AddForce(m_BodyID, force);
}
void Rigidbody::AddImpulse(const XMFLOAT3& impulse)
{
    PhysicsWorld::Get().AddImpulse(m_BodyID, impulse);
}
void Rigidbody::SetLinearVelocity(const XMFLOAT3& vel)
{
    PhysicsWorld::Get().SetLinearVelocity(m_BodyID, vel);
}
void Rigidbody::SetAngularVelocity(const XMFLOAT3& vel)
{
    PhysicsWorld::Get().SetAngularVelocity(m_BodyID, vel);
}

// ============================================================================
// Velocity read
// ============================================================================

XMFLOAT3 Rigidbody::GetLinearVelocity() const
{
    return PhysicsWorld::Get().GetLinearVelocity(m_BodyID);
}
XMFLOAT3 Rigidbody::GetAngularVelocity() const
{
    return PhysicsWorld::Get().GetAngularVelocity(m_BodyID);
}

// ============================================================================
// Runtime transform (teleport)
// ============================================================================

void Rigidbody::SetPosition(const XMFLOAT3& pos)
{
    PhysicsWorld::Get().SetBodyPosition(m_BodyID, pos);
    // Keep the GO Transform in sync immediately.
    if (auto* t = GetGameObject()->GetTransform())
        t->SetPosition(pos);
}

void Rigidbody::SetRotation(const XMFLOAT4& quatXYZW)
{
    PhysicsWorld::Get().SetBodyRotation(m_BodyID, quatXYZW);
    if (auto* t = GetGameObject()->GetTransform())
        t->SetRotationQuat(quatXYZW);
}

// ============================================================================
// Contact dispatch  (called by PhysicsWorld::DispatchContactEvents)
// ============================================================================

void Rigidbody::DispatchContactBegin(Rigidbody* other, bool isTrigger)
{
    for (auto& [_, comp] : GetGameObject()->GetAllComponents()) {
        if (!comp->IsEnabled()) continue;
        if (isTrigger) comp->OnTriggerEnter(other);
        else           comp->OnCollisionEnter(other);
    }
}

void Rigidbody::DispatchContactEnd(Rigidbody* other, bool isTrigger)
{
    for (auto& [_, comp] : GetGameObject()->GetAllComponents()) {
        if (!comp->IsEnabled()) continue;
        if (isTrigger) comp->OnTriggerExit(other);
        else           comp->OnCollisionExit(other);
    }
}

} // namespace VibeEngine
