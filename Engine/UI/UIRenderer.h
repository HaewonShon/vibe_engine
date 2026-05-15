#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <cstdint>

namespace VibeEngine {

// ============================================================================
// UIRenderer  (singleton)
//
// Owns:
//   • DX12 UI pipeline (PSO, root signature, dynamic vertex buffer)
//   • Font texture atlas — generated at Initialize() from GDI + Courier New
//
// Usage (typically in SandboxApp):
//
//   // --- Initialization (between BeginFrame / EndFrame) -------------------
//   m_DX12.BeginFrame();
//   UIRenderer::Get().Initialize(
//       m_DX12.GetDevice(), m_DX12.GetCommandList(),
//       m_DX12.GetBackBufferFormat(), 1280, 720);
//   m_DX12.EndFrame();
//   m_DX12.WaitForGPU();
//   UIRenderer::Get().ReleaseUploadBuffer();   // free staging memory
//
//   // --- Every frame (inside OnRender, after scene->Render()) -------------
//   UIRenderer::Get().BeginPass(cmdList, screenW, screenH);
//   UIRenderer::Get().DrawRect(10, 10, 200, 80, {0,0,0,0.6f});
//   UIRenderer::Get().DrawText(20, 20, "Hello!", {1,1,1,1});
//   UIRenderer::Get().EndPass();
// ============================================================================
class UIRenderer {
public:
    static UIRenderer& Get() { static UIRenderer s; return s; }

    // ---- Lifecycle ----------------------------------------------------------
    // uploadCmdList must be open (between BeginFrame / EndFrame).
    bool Initialize(ID3D12Device* device,
                    ID3D12GraphicsCommandList* uploadCmdList,
                    DXGI_FORMAT backBufferFormat,
                    UINT screenW, UINT screenH);
    void ReleaseUploadBuffer();   // call after WaitForGPU()
    void Shutdown();
    bool IsInitialized() const { return m_Initialized; }

    // ---- Per-frame ----------------------------------------------------------
    // Call after scene->Render(); sets UI pipeline state.
    void BeginPass(ID3D12GraphicsCommandList* cmdList, UINT screenW, UINT screenH);
    void EndPass();

    // ---- Draw primitives ----------------------------------------------------
    // All coordinates: screen pixels, top-left origin.
    void DrawRect  (float x, float y, float w, float h,
                    DirectX::XMFLOAT4 color);
    void DrawBorder(float x, float y, float w, float h,
                    float thickness,
                    DirectX::XMFLOAT4 color);
    void DrawText  (float x, float y, const char* text,
                    DirectX::XMFLOAT4 color,
                    float scale = 1.f);

    // ---- Font metrics -------------------------------------------------------
    float GetCharWidth()  const { return static_cast<float>(m_CharW); }
    float GetCharHeight() const { return static_cast<float>(m_CharH); }
    float MeasureText(const char* text, float scale = 1.f) const;

private:
    UIRenderer()  = default;
    ~UIRenderer() = default;
    UIRenderer(const UIRenderer&)            = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    // ---- Internal helpers ---------------------------------------------------
    bool BuildFontAtlas(ID3D12Device* device,
                        ID3D12GraphicsCommandList* cmdList);
    bool BuildPipeline (ID3D12Device* device, DXGI_FORMAT rtFormat);
    void FlushQuad     (float x0, float y0, float x1, float y1,
                        float u0, float v0, float u1, float v1,
                        DirectX::XMFLOAT4 color);

    // ---- DX12 resources -----------------------------------------------------
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_RootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_PSO;

    // Font atlas texture (default heap) + SRV descriptor heap
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_FontTex;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_FontUpload; // freed after GPU upload
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SRVHeap;

    // Dynamic vertex buffer (upload heap, persistently mapped)
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_VB;
    void*                                        m_VBData = nullptr;
    static constexpr UINT                        kMaxVerts = 65536;
    UINT                                         m_VBCursor = 0;

    // ---- Font atlas metadata ------------------------------------------------
    int m_AtlasW  = 0, m_AtlasH  = 0;
    int m_CharW   = 0, m_CharH   = 0;
    int m_AtlasCols = 16;             // chars per row in atlas

    // ---- Frame state --------------------------------------------------------
    ID3D12GraphicsCommandList* m_CmdList  = nullptr;
    UINT m_ScreenW = 0, m_ScreenH = 0;
    bool m_Initialized = false;
    bool m_InPass      = false;

    // Orthographic projection matrix (rebuilt on BeginPass if screen size changes)
    DirectX::XMFLOAT4X4 m_OrthoMatrix = {};
    UINT m_LastScreenW = 0, m_LastScreenH = 0;
};

} // namespace VibeEngine
