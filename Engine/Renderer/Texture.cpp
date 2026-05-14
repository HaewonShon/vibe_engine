#include "Texture.h"
#include "DX12Context.h"
#include "DX12Helpers.h"
#include <wincodec.h>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "ole32.lib")

namespace VibeEngine {

bool Texture::LoadFromFile(ID3D12Device* device,
                           ID3D12GraphicsCommandList* cmdList,
                           DX12Context& ctx,
                           const std::wstring& path)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = wic->CreateDecoderFromFilename(path.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
    wic->CreateFormatConverter(&conv);
    conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    UINT width = 0, height = 0;
    conv->GetSize(&width, &height);

    // Build texture resource desc to query copyable footprint
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width              = width;
    texDesc.Height             = height;
    texDesc.DepthOrArraySize   = 1;
    texDesc.MipLevels          = 1;
    texDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc         = { 1, 0 };

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);

    // Upload heap buffer
    auto uploadHP   = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = BufferDesc(totalBytes);
    hr = device->CreateCommittedResource(&uploadHP, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_UploadBuffer));
    if (FAILED(hr)) return false;

    // Copy pixels row-by-row respecting aligned row pitch
    UINT tightPitch = width * 4;
    std::vector<BYTE> pixels(tightPitch * height);
    conv->CopyPixels(nullptr, tightPitch, tightPitch * height, pixels.data());

    void* mapped = nullptr;
    D3D12_RANGE range = { 0, 0 };
    m_UploadBuffer->Map(0, &range, &mapped);
    for (UINT row = 0; row < height; ++row) {
        memcpy(static_cast<BYTE*>(mapped) + (UINT64)row * footprint.Footprint.RowPitch,
               pixels.data() + (UINT64)row * tightPitch,
               tightPitch);
    }
    m_UploadBuffer->Unmap(0, nullptr);

    // Default heap texture
    auto defaultHP = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    hr = device->CreateCommittedResource(&defaultHP, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_Resource));
    if (FAILED(hr)) return false;

    // Copy upload → texture
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource        = m_Resource.Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource        = m_UploadBuffer.Get();
    src.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint  = footprint;

    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    auto barrier = TransitionBarrier(m_Resource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);

    // Create SRV in the shared heap
    auto alloc     = ctx.AllocateSRV();
    m_SRVGpuHandle = alloc.gpu;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels       = 1;
    device->CreateShaderResourceView(m_Resource.Get(), &srvDesc, alloc.cpu);

    return true;
}

void Texture::ReleaseUploadBuffer()
{
    m_UploadBuffer.Reset();
}

} // namespace VibeEngine
