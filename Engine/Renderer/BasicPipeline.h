#pragma once
#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

class BasicPipeline {
public:
    bool Create(ID3D12Device* device, DXGI_FORMAT rtvFormat);
    void Destroy();

    ID3D12PipelineState* GetPSO()          const { return m_PSO.Get(); }
    ID3D12RootSignature* GetRootSignature() const { return m_RootSignature.Get(); }

private:
    bool CreateRootSignature(ID3D12Device* device);
    bool CreatePSO(ID3D12Device* device, DXGI_FORMAT rtvFormat);

    ComPtr<ID3D12RootSignature> m_RootSignature;
    ComPtr<ID3D12PipelineState> m_PSO;
};

} // namespace VibeEngine
