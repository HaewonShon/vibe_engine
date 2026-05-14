#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

class DX12Context;

class Texture {
public:
    bool LoadFromFile(ID3D12Device* device,
                      ID3D12GraphicsCommandList* cmdList,
                      DX12Context& ctx,
                      const std::wstring& path);

    void ReleaseUploadBuffer();

    bool                        IsLoaded()     const { return m_Resource != nullptr; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle() const { return m_SRVGpuHandle; }

private:
    ComPtr<ID3D12Resource>      m_Resource;
    ComPtr<ID3D12Resource>      m_UploadBuffer;
    D3D12_GPU_DESCRIPTOR_HANDLE m_SRVGpuHandle = {};
};

} // namespace VibeEngine
