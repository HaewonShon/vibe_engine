#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "ShaderWatcher.h"

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

class BasicPipeline {
public:
    bool Create(ID3D12Device* device, DXGI_FORMAT rtvFormat);
    void Destroy();

    // ---------------------------------------------------------------------------
    // Shader hot reload
    //
    // Call HasShaderChanged() before BeginFrame each frame.
    // If it returns true, call WaitForGPU() then HotReload() before recording.
    //
    // HasShaderChanged() — polls the file-watcher flag; resets it to false.
    // HotReload()        — recompiles shaders and replaces the PSO.
    //   PRECONDITION: GPU must be idle (caller must call WaitForGPU first).
    //   Returns true on success; PSO is unchanged on compile / create failure
    //   so the engine continues rendering with the last good shader.
    // ---------------------------------------------------------------------------
    bool HasShaderChanged();
    bool HotReload();

    ID3D12PipelineState* GetPSO()          const { return m_PSO.Get(); }
    ID3D12RootSignature* GetRootSignature() const { return m_RootSignature.Get(); }

private:
    bool CreateRootSignature(ID3D12Device* device);
    bool CreatePSO(ID3D12Device* device, DXGI_FORMAT rtvFormat);

    // Builds a PSO from pre-compiled VS/PS blobs.
    // Shared by CreatePSO() and HotReload() to avoid duplication.
    bool BuildPSO(ID3DBlob* vs, ID3DBlob* ps,
                  ComPtr<ID3D12PipelineState>& outPSO) const;

    ComPtr<ID3D12RootSignature> m_RootSignature;
    ComPtr<ID3D12PipelineState> m_PSO;

    // Hot reload — device/format cached from Create(); ShaderWatcher runs
    // on a background thread while the engine is alive.
    ID3D12Device* m_Device    = nullptr;   // non-owning; device outlives pipeline
    DXGI_FORMAT   m_RTVFormat = DXGI_FORMAT_UNKNOWN;
    ShaderWatcher m_Watcher;
};

} // namespace VibeEngine
