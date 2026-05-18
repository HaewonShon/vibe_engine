#pragma once
#define NOMINMAX
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include "DX12Context.h"

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

// ---------------------------------------------------------------------------
// SSAOPass — Screen-Space Ambient Occlusion
//
// Pipeline (each step is a full-screen triangle draw):
//   depth buffer (R32_FLOAT SRV)
//       |
//       v  PSComputeAO — 16-sample hemisphere, noise-rotated TBN
//   [AOraw,   R8_UNORM, full res]
//       |
//       v  PSBlur — 5-tap cross filter
//   [AOblur,  R8_UNORM, full res]  ← bound at t6 in Basic.hlsl
//
// Usage:
//   OnInit:   SSAOPass.Initialize(device, ctx, cmdList, w, h)
//             (cmdList must be open — piggyback on the IBL upload frame)
//             SSAOPass.ReleaseUploadBuffers()  // after WaitForGPU
//   OnRender: (inside the open command list, after scene render)
//             barrier depth: DEPTH_WRITE → PSR
//             SSAOPass.ComputeAO(cmdList, ctx.GetDepthSRV(), proj, invProj, w, h)
//             barrier depth: PSR → DEPTH_WRITE
//   OnResize: SSAOPass.Resize(device, ctx, w, h)
//   OnShutdown: SSAOPass.Shutdown()
//
//   MeshRenderer binds GetAOSRV() at root param [9].
//   GetAOSRV() returns a 1×1 white fallback until the first ComputeAO call.
// ---------------------------------------------------------------------------
class SSAOPass {
public:
    // Runtime-tweakable parameters
    float Radius    = 0.5f;    // world-space hemisphere radius
    float Bias      = 0.025f;  // depth bias to prevent self-occlusion
    float Intensity = 1.5f;    // AO strength multiplier

    // Initialize — creates all GPU resources and uploads the 4×4 noise texture.
    // cmdList must be in recording state (caller wraps with BeginFrame/EndFrame).
    bool Initialize(ID3D12Device* device, DX12Context& ctx,
                    ID3D12GraphicsCommandList* cmdList,
                    UINT width, UINT height);

    void Shutdown();

    // Release upload heaps after the GPU has consumed the upload commands.
    void ReleaseUploadBuffers();

    // Recreate intermediate textures after a window resize.
    // The noise texture and pipelines are reused; only AOraw/AOblur are rebuilt.
    void Resize(ID3D12Device* device, DX12Context& ctx, UINT width, UINT height);

    // Compute SSAO into AOraw, then blur AOraw into AOblur.
    // depthSRV  — GPU handle to the R32_FLOAT depth SRV.
    // proj      — camera projection matrix (DirectXMath row-major).
    // invProj   — inverse of proj (DirectXMath row-major).
    // Matrices are transposed internally before upload (row-vector GPU convention).
    void ComputeAO(ID3D12GraphicsCommandList* cmdList,
                   D3D12_GPU_DESCRIPTOR_HANDLE depthSRV,
                   const DirectX::XMMATRIX& proj,
                   const DirectX::XMMATRIX& invProj,
                   UINT width, UINT height);

    // Returns AOblur SRV (from previous frame) or a 1×1 white fallback.
    D3D12_GPU_DESCRIPTOR_HANDLE GetAOSRV() const;

    bool IsReady() const { return m_Ready; }

private:
    // ---- Root constants layout (40 DWORDs = 160 bytes, b0) ------------------
    struct SSAOConstants {
        DirectX::XMFLOAT4X4 InvProj;      // 64 bytes
        DirectX::XMFLOAT4X4 Proj;         // 64 bytes
        float NoiseScaleX, NoiseScaleY;   //  8 bytes
        float Radius, Bias, Intensity;    // 12 bytes
        float Pad0, Pad1, Pad2;           // 12 bytes
    };                                    // = 160 bytes = 40 DWORDs

    // ---- Per-texture bundle (resource + RTV + SRV) --------------------------
    struct RTex {
        ComPtr<ID3D12Resource>      resource;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};  // in m_RTVHeap
        D3D12_GPU_DESCRIPTOR_HANDLE srv = {};  // in shared SRV heap
    };

    // ---- Helpers ------------------------------------------------------------
    bool CreateTextures  (ID3D12Device* device, DX12Context& ctx, UINT w, UINT h);
    void DestroyTextures ();
    bool CreateNoiseTex  (ID3D12Device* device, DX12Context& ctx,
                          ID3D12GraphicsCommandList* cmdList);
    bool CreateFallback  (ID3D12Device* device, DX12Context& ctx,
                          ID3D12GraphicsCommandList* cmdList);
    bool CreateRootSignature(ID3D12Device* device);
    bool CreatePipelines    (ID3D12Device* device);

    void RunFullscreenPass(ID3D12GraphicsCommandList* cmdList,
                           D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                           D3D12_GPU_DESCRIPTOR_HANDLE srv0,
                           D3D12_GPU_DESCRIPTOR_HANDLE srv1,
                           ID3D12PipelineState*        pso,
                           const SSAOConstants&        cb,
                           UINT w, UINT h);

    // ---- Root signature & PSOs ----------------------------------------------
    ComPtr<ID3D12RootSignature> m_RootSig;
    ComPtr<ID3D12PipelineState> m_AOPassPSO;
    ComPtr<ID3D12PipelineState> m_BlurPSO;

    // ---- Intermediate textures ----------------------------------------------
    RTex m_AOraw;    // raw SSAO output
    RTex m_AOblur;   // blurred result (bound at t6 next frame)

    // ---- Noise texture (4×4 R8G8B8A8, random 2D unit vectors) --------------
    ComPtr<ID3D12Resource>      m_NoiseTex;
    ComPtr<ID3D12Resource>      m_NoiseUpload;   // released after first WaitForGPU
    D3D12_GPU_DESCRIPTOR_HANDLE m_NoiseSRV = {};

    // ---- Fallback 1×1 white texture (used until first ComputeAO) -----------
    ComPtr<ID3D12Resource>      m_FallbackTex;
    ComPtr<ID3D12Resource>      m_FallbackUpload;
    D3D12_GPU_DESCRIPTOR_HANDLE m_FallbackSRV = {};

    // ---- Private RTV heap (2 slots: AOraw, AOblur) -------------------------
    ComPtr<ID3D12DescriptorHeap> m_RTVHeap;
    UINT m_RTVSize = 0;

    // ---- Resource state tracking (for barrier management) ------------------
    D3D12_RESOURCE_STATES m_AOrawState  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_AOblurState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    UINT m_Width  = 0;
    UINT m_Height = 0;
    bool m_Ready       = false;
    bool m_HasValidAO  = false;   // false until first ComputeAO completes
};

} // namespace VibeEngine
