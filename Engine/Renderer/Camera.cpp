#include "Camera.h"
#include "../Core/Transform.h"
#include "../Core/GameObject.h"

using namespace DirectX;

namespace VibeEngine {

Camera::Camera()  = default;
Camera::~Camera() = default;

XMMATRIX Camera::GetViewProjectionMatrix() const
{
    XMVECTOR eye   = XMVectorSet(0.f, 0.f, -3.f, 1.f);
    XMVECTOR focus = XMVectorZero();
    XMVECTOR up    = XMVectorSet(0.f, 1.f, 0.f, 0.f);

    // If we have a transform, use it for the camera position
    if (GetGameObject()) {
        if (auto* t = GetGameObject()->GetTransform()) {
            auto pos = t->GetWorldMatrix().r[3];
            eye = pos;
            // Look along -Z in local space
            XMVECTOR fwd = XMVector3TransformNormal(
                XMVectorSet(0.f, 0.f, 1.f, 0.f), t->GetWorldMatrix());
            focus = XMVectorAdd(eye, fwd);
        }
    }

    XMMATRIX view = XMMatrixLookAtLH(eye, focus, up);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(m_FOV), m_Aspect, m_Near, m_Far);
    return view * proj;
}

bool Camera::CreateConstantBuffer(ID3D12Device* /*device*/)
{
    return true; // Camera VP is passed per-draw; no separate CB needed
}

} // namespace VibeEngine
