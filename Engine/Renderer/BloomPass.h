#pragma once
#define NOMINMAX
#include <d3d12.h>
#include <wrl/client.h>
#include "DX12Context.h"

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

// ---------------------------------------------------------------------------
// BloomPass — separable Gaussian bloom with ACES tone mapping.
//
// Pipeline (each step is a full-screen triangle-strip draw):
//   Scene  ->  [HDR RT, full res, R16G16B16A16_FLOAT]
//              |
//              v BrightPass (luminance threshold, soft knee)
//           [Bright, half res]
//              |
//              v BlurH (13-tap Gaussian, horizontal)
//           [BlurH, half res]
//              |
//              v BlurV (13-tap Gaussian, vertical)
//           [BlurV, half res]
//              |
//              v Composite (HDR + bloom * Intensity, ACES, sRGB)
//           [Back buffer, UNORM]
//
// Usage:
//   OnInit:   BloomPass.Initialize(device, ctx, w, h)
//             BasicPipeline.Create(device, BloomPass::HDRFormat)
//   OnRender: BloomPass.BeginCapture(cmdList, dsv, w, h)  // bind HDR RT
//             scene->Render()
//             BloomPass.Apply(cmdList, backBufferRTV, w, h)
//   OnResize: BloomPass.Resize(device, ctx, w, h)
//   OnShutdown: BloomPass.Shutdown()
// ---------------------------------------------------------------------------
class BloomPass {
public:
    // Format the scene must render into (pass to BasicPipeline::Create).
    static constexpr DXGI_FORMAT HDRFormat      = DXGI_FORMAT_R16G16B16A16_FLOAT;
    // Format of the swap chain back buffer (unchanged).
    static constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    // ---------------------------------------------------------------------------
    // Runtime-tweakable parameters
    // ---------------------------------------------------------------------------
    float Threshold = 1.0f;   // luminance above which pixels bloom
    float Intensity = 0.5f;   // bloom additive strength in composite
    float Exposure  = 1.0f;   // pre-tone-map exposure multiplier

    // ---------------------------------------------------------------------------
    bool Initialize(ID3D12Device* device, DX12Context& ctx,
                    UINT width, UINT height);
    void Shutdown();

    // Recreate intermediate textures after a window resize.
    void Resize(ID3D12Device* device, DX12Context& ctx, UINT width, UINT height);

    // Transition HDR RT to RENDER_TARGET, bind it with the provided DSV, clear.
    // Call once per frame immediately after the shadow pass.
    void BeginCapture(ID3D12GraphicsCommandList* cmdList,
                      D3D12_CPU_DESCRIPTOR_HANDLE dsv,
                      UINT width, UINT height);

    // Run BrightPass -> BlurH -> BlurV -> Composite.
    // Outputs to backBufferRTV (UNORM).  After this call the command list has
    // backBufferRTV bound as the current render target (no DSV).
    void Apply(ID3D12GraphicsCommandList* cmdList,
               D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
               UINT width, UINT height);

    bool IsReady() const { return m_Ready; }

private:
    // ---- Root constants layout (b0) -----------------------------------------
    struct BloomConstants {
        float Threshold;
        float Intensity;
        float TexelW;
        float TexelH;
        float Exposure;
        float Pad0, Pad1, Pad2;
    };

    // ---- Per-texture bundle (resource + RTV + SRV) --------------------------
    struct RTex {
        ComPtr<ID3D12Resource>      resource;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};  // in m_RTVHeap
        D3D12_GPU_DESCRIPTOR_HANDLE srv = {};  // in ctx shared SRV heap
    };

    // ---- Helpers ------------------------------------------------------------
    bool CreateTextures (ID3D12Device* device, DX12Context& ctx, UINT w, UINT h);
    void DestroyTextures();
    bool CreateRootSignature(ID3D12Device* device);
    bool CreatePipelines    (ID3D12Device* device);

    // Bind a full-screen pass: set PSO, constants, descriptor tables, viewport, RTV.
    void RunPass(ID3D12GraphicsCommandList* cmdList,
                 D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                 D3D12_GPU_DESCRIPTOR_HANDLE srv0,
                 D3D12_GPU_DESCRIPTOR_HANDLE srv1,
                 ID3D12PipelineState*       pso,
                 const BloomConstants&      cb,
                 UINT viewW, UINT viewH);

    // ---- Root signature & PSOs ----------------------------------------------
    ComPtr<ID3D12RootSignature> m_RootSig;
    ComPtr<ID3D12PipelineState> m_BrightPSO;
    ComPtr<ID3D12PipelineState> m_BlurHPSO;
    ComPtr<ID3D12PipelineState> m_BlurVPSO;
    ComPtr<ID3D12PipelineState> m_CompositePSO;

    // ---- Intermediate textures ----------------------------------------------
    RTex m_HDR;     // full-res HDR scene capture
    RTex m_Bright;  // half-res bright-pass output
    RTex m_BlurH;   // half-res horizontal blur
    RTex m_BlurV;   // half-res vertical blur (= final bloom)

    // ---- Private RTV heap (4 slots: HDR, Bright, BlurH, BlurV) -------------
    ComPtr<ID3D12DescriptorHeap> m_RTVHeap;
    UINT m_RTVSize = 0;

    UINT m_Width  = 0;
    UINT m_Height = 0;
    bool m_Ready  = false;
};

} // namespace VibeEngine
