#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include "Texture.h"
#include "BasicPipeline.h"

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

class Material {
public:
    bool Create(ID3D12Device* device);

    // Parameter setters — mark dirty for next Upload()
    void SetAlbedo   (const DirectX::XMFLOAT4& color)          { m_Albedo             = color;     m_Dirty = true; }
    void SetRoughness(float r)                                  { m_Roughness          = r;         m_Dirty = true; }
    void SetMetallic (float m)                                  { m_Metallic           = m;         m_Dirty = true; }
    void SetEmissive (const DirectX::XMFLOAT3& e, float i = 1.f) { m_Emissive         = e;
                                                                    m_EmissiveIntensity = i;         m_Dirty = true; }
    void SetTexture   (Texture* t)      { m_Texture    = t; }
    void SetNormalMap (Texture* t)      { m_NormalMap  = t; }
    void SetPipeline  (BasicPipeline* p){ m_Pipeline   = p; }

    const DirectX::XMFLOAT4& GetAlbedo()            const { return m_Albedo; }
    float                     GetRoughness()         const { return m_Roughness; }
    float                     GetMetallic()          const { return m_Metallic; }
    const DirectX::XMFLOAT3& GetEmissive()           const { return m_Emissive; }
    float                     GetEmissiveIntensity() const { return m_EmissiveIntensity; }
    Texture*                  GetTexture()           const { return m_Texture; }
    Texture*                  GetNormalMap()         const { return m_NormalMap; }
    BasicPipeline*            GetPipeline()          const { return m_Pipeline; }

    // Upload dirty data to GPU; call once per frame before Draw
    void Upload();

    D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const;

private:
    struct MaterialCB {
        DirectX::XMFLOAT4  Albedo;
        float              Roughness;
        float              Metallic;
        float              EmissiveIntensity;
        float              Pad0;
        DirectX::XMFLOAT3  Emissive;
        float              Pad1;
    };

    DirectX::XMFLOAT4  m_Albedo             = { 1.f, 1.f, 1.f, 1.f };
    float              m_Roughness           = 0.5f;
    float              m_Metallic            = 0.0f;
    DirectX::XMFLOAT3  m_Emissive            = { 0.f, 0.f, 0.f };
    float              m_EmissiveIntensity   = 0.0f;
    bool               m_Dirty              = true;

    Texture*       m_Texture    = nullptr;
    Texture*       m_NormalMap  = nullptr;
    BasicPipeline* m_Pipeline   = nullptr;

    ComPtr<ID3D12Resource> m_Buffer;
    void*                  m_Mapped = nullptr;
};

} // namespace VibeEngine
