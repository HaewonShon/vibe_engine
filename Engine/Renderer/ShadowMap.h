#pragma once
#include "DX12Context.h"
#include "DX12Helpers.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

// ---------------------------------------------------------------------------
// ShadowMap — 2048x2048 depth-only render target for directional shadow mapping.
//
// Usage per frame:
//   1.  BeginShadowPass(cmdList)  — transition to DEPTH_WRITE, clear, set DSV
//   2.  (draw scene geometry with ShadowPipeline)
//   3.  EndShadowPass(cmdList)    — transition to PIXEL_SHADER_RESOURCE
//   4.  (main render pass — Basic.hlsl samples gShadowMap at t1)
//
// The light-space matrix is rebuilt whenever SetLightDirection() or
// SetSceneBounds() is called.  Call these once during OnInit(); they are
// NOT hot-path.
//
// Resource format: R32_TYPELESS  (allows D32_FLOAT DSV + R32_FLOAT SRV
//                  from the same committed resource — required by DX12).
// ---------------------------------------------------------------------------
class ShadowMap {
public:
    static constexpr UINT RESOLUTION = 2048;

    // Allocates the depth texture and registers an SRV in the engine's shared heap.
    bool Initialize(ID3D12Device* device, DX12Context& ctx);
    void Shutdown();

    // ---- Per-frame ----------------------------------------------------------

    // Transitions to DEPTH_WRITE, sets viewport/scissor, clears the DSV,
    // and binds it as the sole render target (no RTV).
    void BeginShadowPass(ID3D12GraphicsCommandList* cmdList);

    // Transitions back to PIXEL_SHADER_RESOURCE so the main pass can sample it.
    void EndShadowPass(ID3D12GraphicsCommandList* cmdList);

    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const { return m_DSVHandle; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRV() const { return m_SRVHandle; }

    D3D12_VIEWPORT GetViewport() const;
    D3D12_RECT     GetScissor()  const;

    // ---- Light space setup --------------------------------------------------

    // dir  : direction light travels (same convention as LightManager).
    void SetLightDirection(const DirectX::XMFLOAT3& dir);

    // halfExtent : half-width/height of the orthographic frustum in world units.
    // depthRange : total depth (near=0.1 to far=depthRange along the light ray).
    void SetSceneBounds(float halfExtent, float depthRange);

    // Returns lightView * lightOrtho (row-major, NOT yet transposed).
    // MeshRenderer transposes before uploading to the GPU constant buffer.
    DirectX::XMMATRIX GetLightSpaceMatrix() const;

private:
    void RebuildLightSpaceMatrix();

    ComPtr<ID3D12Resource>       m_Texture;
    ComPtr<ID3D12DescriptorHeap> m_DSVHeap;   // private 1-slot DSV heap
    D3D12_CPU_DESCRIPTOR_HANDLE  m_DSVHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE  m_SRVHandle = {};  // slot in the shared SRV heap

    DirectX::XMFLOAT3   m_LightDir         = {  0.577f, 0.577f, 0.577f };
    float               m_HalfExtent       = 30.0f;
    float               m_DepthRange       = 100.0f;
    DirectX::XMFLOAT4X4 m_LightSpaceMat   = {};   // cached result
};

} // namespace VibeEngine
