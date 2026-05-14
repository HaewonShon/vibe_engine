#include "Transform.h"
#include <algorithm>

using namespace DirectX;

namespace VibeEngine {

Transform::Transform() = default;

void Transform::Translate(const XMFLOAT3& delta)
{
    m_Position.x += delta.x;
    m_Position.y += delta.y;
    m_Position.z += delta.z;
}

void Transform::Rotate(float yawDeg, float pitchDeg, float rollDeg)
{
    m_Rotation.x += pitchDeg;
    m_Rotation.y += yawDeg;
    m_Rotation.z += rollDeg;
}

XMMATRIX Transform::GetLocalMatrix() const
{
    float pitch = XMConvertToRadians(m_Rotation.x);
    float yaw   = XMConvertToRadians(m_Rotation.y);
    float roll  = XMConvertToRadians(m_Rotation.z);

    XMMATRIX s = XMMatrixScaling(m_Scale.x, m_Scale.y, m_Scale.z);
    XMMATRIX r = XMMatrixRotationRollPitchYaw(pitch, yaw, roll);
    XMMATRIX t = XMMatrixTranslation(m_Position.x, m_Position.y, m_Position.z);
    return s * r * t;
}

XMMATRIX Transform::GetWorldMatrix() const
{
    XMMATRIX local = GetLocalMatrix();
    if (m_Parent)
        return local * m_Parent->GetWorldMatrix();
    return local;
}

void Transform::SetParent(Transform* parent)
{
    if (m_Parent) {
        auto& siblings = m_Parent->m_Children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    }
    m_Parent = parent;
    if (m_Parent)
        m_Parent->m_Children.push_back(this);
}

} // namespace VibeEngine
