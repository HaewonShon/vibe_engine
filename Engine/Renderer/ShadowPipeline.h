#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

// ---------------------------------------------------------------------------
// ShadowPipeline — depth-only PSO for directional shadow map generation.
//
// Root signature (minimal):
//   [0] Root CBV b0 — ShadowPerObjectCB { LightMVP } (VS visible only)
//
// No pixel shader: only depth is written to the shadow map DSV.
//
// Rasterizer bias:
//   DepthBias = 1000, SlopeScaledDepthBias = 2.0
//   This pushes shadow-map depths slightly away from the surface to reduce
//   self-shadowing (acne) on faces that have a gentle angle to the light.
//   The values are empirical and can be tuned at runtime via SandboxApp.
//
// Usage:
//   1. ShadowMap::BeginShadowPass()
//   2. ShadowPipeline::Bind(cmdList)
//   3. For each MeshRenderer: MeshRenderer::DrawShadow(cmdList, lightVP)
//   4. ShadowMap::EndShadowPass()
// ---------------------------------------------------------------------------
class ShadowPipeline {
public:
    bool Create(ID3D12Device* device);
    void Destroy();

    // Binds root signature, PSO, and primitive topology.
    // Call once per frame before the shadow draw loop.
    void Bind(ID3D12GraphicsCommandList* cmdList) const;

    ID3D12RootSignature* GetRootSignature() const { return m_RootSignature.Get(); }
    ID3D12PipelineState* GetPSO()          const { return m_PSO.Get(); }

private:
    bool CreateRootSignature(ID3D12Device* device);
    bool CreatePSO(ID3D12Device* device);

    ComPtr<ID3D12RootSignature> m_RootSignature;
    ComPtr<ID3D12PipelineState> m_PSO;
};

} // namespace VibeEngine
