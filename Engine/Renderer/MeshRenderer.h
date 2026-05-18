#pragma once
#include "../Core/Component.h"
#include "Mesh.h"
#include "BasicPipeline.h"
#include "Texture.h"
#include "Material.h"
#include "ShadowMap.h"
#include "IBLMap.h"
#include "SSAOPass.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

class MeshRenderer : public Component {
public:
    MeshRenderer();
    ~MeshRenderer() override;

    void SetMesh       (std::shared_ptr<Mesh> mesh)       { m_Mesh     = mesh; }
    void SetCommandList(ID3D12GraphicsCommandList* cl)    { m_CmdList  = cl; }
    void SetMaterial   (Material* mat)                    { m_Material = mat; }

    // Optional: provide the active shadow map so Draw() can upload LightMVP
    // and bind the shadow SRV at root parameter [4].
    // Must be set before the first Draw() call; safe to keep null (no shadows).
    void SetShadowMap  (ShadowMap* sm)                    { m_ShadowMap = sm; }

    // Optional: provide the IBL map for environment lighting.
    // Binds irradiance [5], specular [6], BRDF LUT [7] in Draw().
    // Safe to keep null (IBL slots are unbound; shader falls back gracefully).
    void SetIBLMap     (IBLMap*   ibl)                    { m_IBLMap    = ibl; }

    // Flat-normal fallback texture (1x1, (128,128,255,255) = tangent-space up).
    // Used when the material has no normal map assigned.  Must be set before
    // the first Draw() call for correct normal-mapping behaviour.
    void SetFlatNormal (Texture*  tex)                    { m_FlatNormal = tex; }

    // Optional: provide the SSAO pass so Draw() can bind the AO map at [9].
    // GetAOSRV() returns a 1×1 white fallback until the first ComputeAO call.
    void SetAOPass     (SSAOPass* ao)                     { m_AOPass     = ao; }

    // Read-only access for editor Inspector
    Material*             GetMaterial() const { return m_Material; }
    std::shared_ptr<Mesh> GetMesh()     const { return m_Mesh; }

    // Allocates both the main per-object CB and the shadow per-object CB.
    bool CreateConstantBuffer(ID3D12Device* device);

    // Main render pass — binds the BasicPipeline PSO, uploads MVP/World/LightMVP,
    // binds albedo texture [3] and shadow map [4], draws indexed geometry.
    void Draw(const DirectX::XMMATRIX& viewProj) const;

    // Shadow pass — binds only the ShadowPipeline CBV (LightMVP) and draws.
    // The ShadowPipeline root signature + PSO must already be bound via
    // ShadowPipeline::Bind() before calling DrawShadow() on any mesh.
    void DrawShadow(ID3D12GraphicsCommandList* cmdList,
                    const DirectX::XMMATRIX&  lightViewProj) const;

private:
    std::shared_ptr<Mesh>      m_Mesh;
    Material*                  m_Material   = nullptr;
    ShadowMap*                 m_ShadowMap  = nullptr;   // non-owning, optional
    IBLMap*                    m_IBLMap     = nullptr;   // non-owning, optional
    Texture*                   m_FlatNormal = nullptr;   // non-owning, fallback normal map
    SSAOPass*                  m_AOPass     = nullptr;   // non-owning, optional
    ID3D12GraphicsCommandList* m_CmdList    = nullptr;

    // Main pass constant buffer: { MVP, World, LightMVP }
    ComPtr<ID3D12Resource> m_ConstantBuffer;
    void*                  m_MappedCB = nullptr;

    // Shadow pass constant buffer: { LightMVP }  (separate to avoid aliasing)
    ComPtr<ID3D12Resource> m_ShadowConstantBuffer;
    void*                  m_ShadowMappedCB = nullptr;
};

} // namespace VibeEngine
