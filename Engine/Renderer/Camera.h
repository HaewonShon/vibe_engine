#pragma once
#include "../Core/Component.h"
#include <DirectXMath.h>

namespace VibeEngine {

class Camera : public Component {
public:
    Camera();
    ~Camera() override;

    void Update(float dt) override;

    void SetFOV      (float fovDeg)        { m_FOV       = fovDeg; }
    void SetAspect   (float aspect)        { m_Aspect    = aspect; }
    void SetNearFar  (float n, float f)    { m_Near = n; m_Far = f; }
    void SetMoveSpeed(float s)             { m_MoveSpeed = s; }
    void SetLookSpeed(float s)             { m_LookSpeed = s; }

    DirectX::XMMATRIX GetViewProjectionMatrix() const;

private:
    // Projection
    float m_FOV    = 60.0f;
    float m_Aspect = 16.0f / 9.0f;
    float m_Near   = 0.1f;
    float m_Far    = 1000.0f;

    // Look angles (degrees)
    float m_Yaw   = 0.0f;   // horizontal, around Y
    float m_Pitch = 0.0f;   // vertical,   around X

    // Control params
    float m_MoveSpeed  = 5.0f;   // units/s
    float m_LookSpeed  = 0.15f;  // degrees/pixel

    // Suppress cursor-jump on first click frame
    bool  m_WasLooking = false;
};

} // namespace VibeEngine
