#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d12.h>

namespace VibeEngine {

class DX12Context;

// ---------------------------------------------------------------------------
// ImGuiLayer
//
// Thin wrapper around Dear ImGui + DX12/Win32 backends.
// Lifetime: Init() in OnInit(), Shutdown() in OnShutdown() AFTER WaitForGPU().
//
// Per-frame usage:
//   Begin()  — call after DX12Context::BeginFrame(), before submitting ImGui windows
//   ...       ImGui window calls ...
//   End(cmd) — call before DX12Context::EndFrame()
//              re-binds the SRV heap so UIRenderer changes are not a problem
//
// Win32 message forwarding:
//   HandleWindowMessage() — call from Application::OnWindowMessage()
//   Returns true if ImGui consumed the message.
// ---------------------------------------------------------------------------
class ImGuiLayer {
public:
    ImGuiLayer()  = default;
    ~ImGuiLayer();

    void Init(DX12Context* ctx, HWND hwnd);
    void Shutdown();

    // Begin new ImGui frame (call once per frame, before any ImGui:: calls).
    void Begin();

    // Render collected draw data into cmdList.
    // Re-binds the engine SRV heap before rendering (safe to call after UIRenderer).
    void End(ID3D12GraphicsCommandList* cmdList);

    // Forward a Win32 message to ImGui. Returns true if ImGui wants to absorb it.
    bool HandleWindowMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    bool IsInitialized() const { return m_Initialized; }

private:
    bool                  m_Initialized  = false;
    ID3D12DescriptorHeap* m_SRVHeap      = nullptr; // non-owning; rebind before render
};

} // namespace VibeEngine
