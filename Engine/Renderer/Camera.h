#pragma once
#include "../Core/Component.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

class Camera : public Component {
public:
    Camera();
    ~Camera() override;

    void SetFOV   (float fovDeg)        { m_FOV    = fovDeg; }
    void SetAspect(float aspect)        { m_Aspect = aspect; }
    void SetNearFar(float n, float f)   { m_Near   = n; m_Far = f; }

    DirectX::XMMATRIX GetViewProjectionMatrix() const;

    bool CreateConstantBuffer(ID3D12Device* device);

private:
    float m_FOV    = 60.0f;
    float m_Aspect = 16.0f / 9.0f;
    float m_Near   = 0.1f;
    float m_Far    = 1000.0f;
};

} // namespace VibeEngine
