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

    // Load from a compressed image blob in memory (PNG/JPEG/etc.).
    // Used for embedded textures in .glb / GLB-format 3D files.
    bool LoadFromMemory(ID3D12Device* device,
                        ID3D12GraphicsCommandList* cmdList,
                        DX12Context& ctx,
                        const void* data, size_t sizeBytes);

    // Upload raw RGBA8 pixel data directly (no WIC decoding).
    // Useful for small programmatic textures such as a 1x1 flat normal (128,128,255,255).
    bool LoadFromPixels(ID3D12Device* device,
                        ID3D12GraphicsCommandList* cmdList,
                        DX12Context& ctx,
                        const uint8_t* rgba, UINT width, UINT height);

    void ReleaseUploadBuffer();

    bool                        IsLoaded()     const { return m_Resource != nullptr; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle() const { return m_SRVGpuHandle; }

private:
    ComPtr<ID3D12Resource>      m_Resource;
    ComPtr<ID3D12Resource>      m_UploadBuffer;
    D3D12_GPU_DESCRIPTOR_HANDLE m_SRVGpuHandle = {};
};

} // namespace VibeEngine
