#include "Camera.h"
#include "../Core/Transform.h"
#include "../Core/GameObject.h"
#include "../Input/InputManager.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace VibeEngine {

Camera::Camera()  = default;
Camera::~Camera() = default;

// Build forward and right vectors from current yaw/pitch
static void GetBasisVectors(float yawDeg, float pitchDeg,
    XMVECTOR& outForward, XMVECTOR& outRight)
{
    float y = XMConvertToRadians(yawDeg);
    float p = XMConvertToRadians(pitchDeg);

    // LH: yaw=0 looks down +Z, positive yaw turns right
    outForward = XMVectorSet(
        sinf(y) * cosf(p),
       -sinf(p),
        cosf(y) * cosf(p),
        0.f);

    outRight = XMVectorSet(cosf(y), 0.f, -sinf(y), 0.f);
}

void Camera::Update(float dt)
{
    auto* go = GetGameObject();
    if (!go) return;
    auto* t = go->GetTransform();

    auto& input = InputManager::Get();

    // ---- Mouse look (hold left button + drag) ----
    bool looking = input.IsMouseButtonDown(0);
    if (looking && m_WasLooking) {
        auto delta = input.GetMouseDelta();
        m_Yaw   += delta.x * m_LookSpeed;
        m_Pitch += delta.y * m_LookSpeed;
        m_Pitch  = std::clamp(m_Pitch, -89.0f, 89.0f);
    }
    m_WasLooking = looking;

    // ---- WASD + Q/E movement ----
    XMVECTOR forward, right;
    GetBasisVectors(m_Yaw, m_Pitch, forward, right);
    XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);

    XMVECTOR move = XMVectorZero();
    if (input.IsKeyDown(KeyCode::W)) move = XMVectorAdd     (move, forward);
    if (input.IsKeyDown(KeyCode::S)) move = XMVectorSubtract(move, forward);
    if (input.IsKeyDown(KeyCode::A)) move = XMVectorSubtract(move, right);
    if (input.IsKeyDown(KeyCode::D)) move = XMVectorAdd     (move, right);
    if (input.IsKeyDown(KeyCode::E)) move = XMVectorAdd     (move, up);
    if (input.IsKeyDown(KeyCode::Q)) move = XMVectorSubtract(move, up);

    float lenSq = XMVectorGetX(XMVector3LengthSq(move));
    if (lenSq > 1e-6f) {
        move = XMVectorScale(XMVector3Normalize(move), m_MoveSpeed * dt);
        XMFLOAT3 d;
        XMStoreFloat3(&d, move);
        t->Translate(d);
    }
}

XMMATRIX Camera::GetViewMatrix() const
{
    auto* go = GetGameObject();
    XMFLOAT3 pos = { 0.f, 0.f, -3.f };
    if (go) {
        if (auto* t = go->GetTransform())
            pos = t->GetLocalPosition();
    }

    XMVECTOR forward, right;
    GetBasisVectors(m_Yaw, m_Pitch, forward, right);

    XMVECTOR eye   = XMLoadFloat3(&pos);
    XMVECTOR focus = XMVectorAdd(eye, forward);
    XMVECTOR up    = XMVectorSet(0.f, 1.f, 0.f, 0.f);

    return XMMatrixLookAtLH(eye, focus, up);
}

XMMATRIX Camera::GetProjectionMatrix() const
{
    return XMMatrixPerspectiveFovLH(
        XMConvertToRadians(m_FOV), m_Aspect, m_Near, m_Far);
}

XMMATRIX Camera::GetViewProjectionMatrix() const
{
    return GetViewMatrix() * GetProjectionMatrix();
}

Camera::Ray Camera::ScreenToRay(float ndcX, float ndcY) const
{
    XMMATRIX invVP  = XMMatrixInverse(nullptr, GetViewProjectionMatrix());
    XMVECTOR nearPt = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.f, 1.f), invVP);
    XMVECTOR farPt  = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.f, 1.f), invVP);
    XMVECTOR dir    = XMVector3Normalize(farPt - nearPt);

    Ray ray;
    XMStoreFloat3(&ray.Origin,    nearPt);
    XMStoreFloat3(&ray.Direction, dir);
    return ray;
}

} // namespace VibeEngine
