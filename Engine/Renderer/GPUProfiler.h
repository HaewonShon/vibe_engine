#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "DX12Context.h"   // for FRAME_COUNT

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

// ---------------------------------------------------------------------------
// GPUProfiler
//
// Measures elapsed GPU time for up to Section::Count render passes per frame
// using ID3D12QueryHeap (D3D12_QUERY_HEAP_TYPE_TIMESTAMP).
//
// Frame-buffered: one readback buffer per swap-chain frame so the CPU can
// safely read results after the fence for that frame index is signaled.
//
// Usage (caller = SandboxApp):
//
//   // Init:
//   m_GPUProfiler.Initialize(device, commandQueue);
//
//   // Per frame, after DX12Context::BeginFrame() (fence already waited):
//   m_GPUProfiler.ReadResults(backBufferIndex);           // read old frame
//
//   // Wrap GPU work:
//   m_GPUProfiler.BeginSection(cmdList, GPUProfiler::Section::Scene);
//   scene->Render();
//   m_GPUProfiler.EndSection  (cmdList, GPUProfiler::Section::Scene);
//   // ... same for UI, ImGui ...
//
//   // Before EndFrame (resolve to readback buffer for this frame index):
//   m_GPUProfiler.Resolve(cmdList, backBufferIndex);
//
//   // In ImGui panel:
//   float ms = m_GPUProfiler.GetMs(GPUProfiler::Section::Scene);
// ---------------------------------------------------------------------------
class GPUProfiler {
public:
    enum class Section : UINT { Scene = 0, UI = 1, ImGui = 2, Count = 3 };

    static constexpr UINT SECTION_COUNT = static_cast<UINT>(Section::Count);
    static constexpr UINT QUERY_COUNT   = SECTION_COUNT * 2; // begin + end per section

    // ---- Lifecycle -------------------------------------------------------
    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue);
    void Shutdown();
    bool IsInitialized() const { return m_Initialized; }

    // ---- Per-frame API ---------------------------------------------------

    // Call once per frame AFTER DX12Context::BeginFrame() returns.
    // Reads the completed GPU timestamps from the readback buffer that
    // was written FRAME_COUNT frames ago (safe after fence wait).
    void ReadResults(UINT frameIndex);

    // Insert a start timestamp.
    void BeginSection(ID3D12GraphicsCommandList* cmdList, Section section);

    // Insert an end timestamp.
    void EndSection  (ID3D12GraphicsCommandList* cmdList, Section section);

    // Resolve all timestamp queries into the readback buffer for this frame.
    // Call near end of frame, before DX12Context::EndFrame().
    void Resolve(ID3D12GraphicsCommandList* cmdList, UINT frameIndex);

    // ---- Results ---------------------------------------------------------
    // Returns the most recently read elapsed GPU time in milliseconds.
    float GetMs(Section section) const;

    // Human-readable label for each section.
    static const char* GetSectionName(Section section);

private:
    bool                    m_Initialized = false;

    ComPtr<ID3D12QueryHeap> m_QueryHeap;
    // One readback buffer per swap-chain frame index.
    ComPtr<ID3D12Resource>  m_ReadbackBuffers[FRAME_COUNT];

    UINT64  m_TimestampFrequency = 1;          // ticks/second from command queue
    float   m_Results[SECTION_COUNT] = {};     // latest elapsed ms per section
    UINT    m_ValidFrameCount = 0;             // guard against reading uninit data
};

} // namespace VibeEngine
