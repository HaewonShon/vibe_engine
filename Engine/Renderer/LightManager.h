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

    void SetDirection (const DirectX::XMFLOAT3& dir) { m_Direction  = dir; m_Dirty = true; }
    void SetColor     (const DirectX::XMFLOAT3& col) { m_Color      = col; m_Dirty = true; }
    void SetIntensity (float i)                       { m_Intensity  = i;   m_Dirty = true; }
    void SetAmbient   (const DirectX::XMFLOAT3& amb) { m_Ambient    = amb; m_Dirty = true; }
    void SetCameraPos (const DirectX::XMFLOAT3& pos) { m_CameraPos  = pos; m_Dirty = true; }
    void SetScreenSize(const DirectX::XMFLOAT2& sz)  { m_ScreenSize = sz;  m_Dirty = true; }

    const DirectX::XMFLOAT3& GetDirection() const { return m_Direction; }
    const DirectX::XMFLOAT3& GetColor()     const { return m_Color; }
    float                     GetIntensity() const { return m_Intensity; }
    const DirectX::XMFLOAT3& GetAmbient()   const { return m_Ambient; }

    void Upload();

    D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const;

private:
    struct LightCB {
        DirectX::XMFLOAT3 Direction;   // normalized, toward light
        float             Intensity;
        DirectX::XMFLOAT3 Color;
        float             Pad0;
        DirectX::XMFLOAT3 Ambient;
        float             Pad1;
        DirectX::XMFLOAT3 CameraPos;  // world-space camera position (PBR view vector)
        float             Pad2;
        DirectX::XMFLOAT2 ScreenSize;  // viewport dimensions (for SSAO screen UV)
        DirectX::XMFLOAT2 ScreenPad;
    };

    DirectX::XMFLOAT3 m_Direction  = {  0.577f, 0.577f,  0.577f }; // upper-right-front
    DirectX::XMFLOAT3 m_Color      = {  1.0f,   1.0f,    1.0f   };
    float             m_Intensity  = 1.0f;
    DirectX::XMFLOAT3 m_Ambient    = {  0.25f,  0.25f,   0.25f  };
    DirectX::XMFLOAT3 m_CameraPos  = {  0.0f,   0.0f,    0.0f   };
    DirectX::XMFLOAT2 m_ScreenSize = { 1280.f, 720.f };
    bool              m_Dirty      = true;

    ComPtr<ID3D12Resource> m_Buffer;
    void*                  m_Mapped = nullptr;
};

} // namespace VibeEngine
