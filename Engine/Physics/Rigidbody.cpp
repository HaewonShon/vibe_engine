#include "Rigidbody.h"
#include "PhysicsWorld.h"
#include "../Core/GameObject.h"
#include "../Core/Transform.h"

using namespace DirectX;

namespace VibeEngine {

void Rigidbody::Start()
{
    auto& pw = PhysicsWorld::Get();
    if (!pw.IsInitialized()) return;

    auto* t   = GetGameObject()->GetTransform();
    auto  pos = t->GetLocalPosition();
    auto  rot = t->GetRotationQuat();

    if (m_UseBox)
        m_BodyID = pw.CreateBox(m_HalfExtents, pos, rot, m_IsStatic, m_Restitution, m_Friction);
    else
        m_BodyID = pw.CreateSphere(m_Radius, pos, m_IsStatic, m_Restitution, m_Friction);
}

void Rigidbody::Update(float /*dt*/)
{
    // Static bodies never move — no readback needed.
    if (m_IsStatic || m_BodyID == 0xFFFFFFFFu) return;

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
    if (pw.IsInitialized())
        pw.DestroyBody(m_BodyID);
    m_BodyID = 0xFFFFFFFFu;
}

// ---- Forces ----------------------------------------------------------------

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

} // namespace VibeEngine
