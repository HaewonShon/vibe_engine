#include "Transform.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace VibeEngine {

Transform::Transform() = default;

// ============================================================================
// Position
// ============================================================================

void Transform::Translate(const XMFLOAT3& delta)
{
    m_Position.x += delta.x;
    m_Position.y += delta.y;
    m_Position.z += delta.z;
}

// ============================================================================
// Rotation — Euler interface
// ============================================================================

void Transform::SetRotation(const XMFLOAT3& eulerDegrees)
{
    float pitch = XMConvertToRadians(eulerDegrees.x);
    float yaw   = XMConvertToRadians(eulerDegrees.y);
    float roll  = XMConvertToRadians(eulerDegrees.z);

    XMVECTOR q = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
    XMStoreFloat4(&m_Quaternion, q);
}

void Transform::Rotate(float yawDeg, float pitchDeg, float rollDeg)
{
    float pitch = XMConvertToRadians(pitchDeg);
    float yaw   = XMConvertToRadians(yawDeg);
    float roll  = XMConvertToRadians(rollDeg);

    XMVECTOR delta   = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
    XMVECTOR current = XMLoadFloat4(&m_Quaternion);

    // Apply delta in local space: new = current * delta
    XMStoreFloat4(&m_Quaternion, XMQuaternionMultiply(current, delta));
}

DirectX::XMFLOAT3 Transform::GetLocalRotation() const
{
    // Convert quaternion → rotation matrix → Euler angles (degrees).
    // Convention matches XMQuaternionRotationRollPitchYaw: Rz*Rx*Ry (roll, pitch, yaw).
    //
    // Matrix layout for M = Rz(roll) * Rx(pitch) * Ry(yaw):
    //   m[2][1] = -sin(pitch)
    //   m[2][0] =  cos(pitch)*sin(yaw)
    //   m[2][2] =  cos(pitch)*cos(yaw)
    //   m[0][1] =  sin(roll)*cos(pitch)
    //   m[1][1] =  cos(roll)*cos(pitch)

    XMVECTOR q = XMLoadFloat4(&m_Quaternion);
    XMMATRIX  m = XMMatrixRotationQuaternion(q);

    XMFLOAT4X4 mat;
    XMStoreFloat4x4(&mat, m);

    float sinPitch = -mat.m[2][1];
    sinPitch = std::clamp(sinPitch, -1.f, 1.f);

    float pitch = asinf(sinPitch);
    float yaw, roll;

    float cosPitch = cosf(pitch);
    if (cosPitch > 1e-6f) {
        yaw  = atan2f(mat.m[2][0], mat.m[2][2]);
        roll = atan2f(mat.m[0][1], mat.m[1][1]);
    } else {
        // Gimbal lock: pitch ≈ ±90° — yaw and roll are coupled; fix yaw = 0.
        yaw  = 0.f;
        roll = atan2f(-mat.m[1][0], mat.m[0][0]);
    }

    return XMFLOAT3{
        XMConvertToDegrees(pitch),
        XMConvertToDegrees(yaw),
        XMConvertToDegrees(roll)
    };
}

// ============================================================================
// Rotation — Quaternion interface
// ============================================================================

void Transform::RotateAxisAngle(const XMFLOAT3& axis, float angleDeg)
{
    XMVECTOR axisV = XMVector3Normalize(XMLoadFloat3(&axis));
    XMVECTOR delta  = XMQuaternionRotationAxis(axisV, XMConvertToRadians(angleDeg));
    XMVECTOR current = XMLoadFloat4(&m_Quaternion);

    // World-space: pre-multiply (delta * current)
    XMStoreFloat4(&m_Quaternion, XMQuaternionMultiply(delta, current));
}

// ============================================================================
// Orientation helpers
// ============================================================================

XMFLOAT3 Transform::GetForward() const
{
    XMVECTOR q   = XMLoadFloat4(&m_Quaternion);
    XMVECTOR fwd = XMVector3Rotate(XMVectorSet(0.f, 0.f, 1.f, 0.f), q);
    XMFLOAT3 result;
    XMStoreFloat3(&result, fwd);
    return result;
}

XMFLOAT3 Transform::GetRight() const
{
    XMVECTOR q     = XMLoadFloat4(&m_Quaternion);
    XMVECTOR right = XMVector3Rotate(XMVectorSet(1.f, 0.f, 0.f, 0.f), q);
    XMFLOAT3 result;
    XMStoreFloat3(&result, right);
    return result;
}

XMFLOAT3 Transform::GetUp() const
{
    XMVECTOR q  = XMLoadFloat4(&m_Quaternion);
    XMVECTOR up = XMVector3Rotate(XMVectorSet(0.f, 1.f, 0.f, 0.f), q);
    XMFLOAT3 result;
    XMStoreFloat3(&result, up);
    return result;
}

void Transform::LookAt(const XMFLOAT3& target, const XMFLOAT3& up)
{
    XMVECTOR pos = XMLoadFloat3(&m_Position);
    XMVECTOR tgt = XMLoadFloat3(&target);
    XMVECTOR upV = XMLoadFloat3(&up);

    XMVECTOR fwd = XMVectorSubtract(tgt, pos);

    // Guard: target == position
    if (XMVector3NearEqual(fwd, XMVectorZero(), XMVectorReplicate(1e-6f)))
        return;

    fwd          = XMVector3Normalize(fwd);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(upV, fwd));
    XMVECTOR newUp = XMVector3Cross(fwd, right);

    // Build rotation matrix (row vectors = basis axes, DirectX row-vector convention)
    XMFLOAT3 r, u, f;
    XMStoreFloat3(&r, right);
    XMStoreFloat3(&u, newUp);
    XMStoreFloat3(&f, fwd);

    XMMATRIX rotMat = XMMatrixSet(
        r.x, r.y, r.z, 0.f,
        u.x, u.y, u.z, 0.f,
        f.x, f.y, f.z, 0.f,
        0.f, 0.f, 0.f, 1.f
    );

    XMVECTOR q = XMQuaternionRotationMatrix(rotMat);
    XMStoreFloat4(&m_Quaternion, XMQuaternionNormalize(q));
}

// ============================================================================
// Matrices
// ============================================================================

XMMATRIX Transform::GetLocalMatrix() const
{
    XMVECTOR q = XMLoadFloat4(&m_Quaternion);

    XMMATRIX s = XMMatrixScaling   (m_Scale.x, m_Scale.y, m_Scale.z);
    XMMATRIX r = XMMatrixRotationQuaternion(q);
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

// ============================================================================
// Hierarchy
// ============================================================================

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
