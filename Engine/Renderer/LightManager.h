#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

class LightManager {
public:
    static LightManager& Get() { static LightManager inst; return inst; }

    bool Initialize(ID3D12Device* device);

    void SetDirection(const DirectX::XMFLOAT3& dir) { m_Direction = dir; m_Dirty = true; }
    void SetColor    (const DirectX::XMFLOAT3& col) { m_Color     = col; m_Dirty = true; }
    void SetIntensity(float i)                       { m_Intensity = i;   m_Dirty = true; }
    void SetAmbient  (const DirectX::XMFLOAT3& amb) { m_Ambient   = amb; m_Dirty = true; }

    void Upload();

    D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const;

private:
    struct LightCB {
        DirectX::XMFLOAT3 Direction;  // normalized, toward light
        float             Intensity;
        DirectX::XMFLOAT3 Color;
        float             Pad0;
        DirectX::XMFLOAT3 Ambient;
        float             Pad1;
    };

    DirectX::XMFLOAT3 m_Direction = {  0.577f, 0.577f,  0.577f }; // upper-right-front
    DirectX::XMFLOAT3 m_Color     = {  1.0f,   1.0f,    1.0f   };
    float             m_Intensity = 1.0f;
    DirectX::XMFLOAT3 m_Ambient   = {  0.25f,  0.25f,   0.25f  };
    bool              m_Dirty     = true;

    ComPtr<ID3D12Resource> m_Buffer;
    void*                  m_Mapped = nullptr;
};

} // namespace VibeEngine
