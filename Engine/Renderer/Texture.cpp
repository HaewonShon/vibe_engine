#include "Texture.h"
#include "DX12Context.h"
#include "DX12Helpers.h"
#include <wincodec.h>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "ole32.lib")

namespace VibeEngine {

// ---------------------------------------------------------------------------
// UploadFromConverter — shared by LoadFromFile and LoadFromMemory.
// Takes a fully-initialised WIC format converter and uploads the decoded
// pixels to a DX12 texture, creating an SRV in the engine's shared heap.
// ---------------------------------------------------------------------------
static bool UploadFromConverter(ID3D12Device*              device,
                                ID3D12GraphicsCommandList* cmdList,
                                DX12Context&               ctx,
                                IWICFormatConverter*       conv,
                                ComPtr<ID3D12Resource>&    outResource,
                                ComPtr<ID3D12Resource>&    outUpload,
                                D3D12_GPU_DESCRIPTOR_HANDLE& outSRV)
{
    UINT width = 0, height = 0;
    conv->GetSize(&width, &height);

    // Use UNORM_SRGB so the GPU hardware auto-linearizes (sRGB -> linear) on
    // every texture fetch.  This is the correct format for albedo/diffuse maps
    // stored as PNG/JPEG (which are always sRGB-encoded).  The byte layout is
    // identical to UNORM so upload code does not change.
    static constexpr DXGI_FORMAT kAlbedoFmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width              = width;
    texDesc.Height             = height;
    texDesc.DepthOrArraySize   = 1;
    texDesc.MipLevels          = 1;
    texDesc.Format             = kAlbedoFmt;
    texDesc.SampleDesc         = { 1, 0 };

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0,
                                  &footprint, nullptr, nullptr, &totalBytes);

    auto uploadHP   = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = BufferDesc(totalBytes);
    if (FAILED(device->CreateCommittedResource(
            &uploadHP, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&outUpload))))
        return false;

    UINT tightPitch = width * 4;
    std::vector<BYTE> pixels(tightPitch * height);
    conv->CopyPixels(nullptr, tightPitch, tightPitch * height, pixels.data());

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    outUpload->Map(0, &readRange, &mapped);
    for (UINT row = 0; row < height; ++row) {
        memcpy(static_cast<BYTE*>(mapped) + (UINT64)row * footprint.Footprint.RowPitch,
               pixels.data()              + (UINT64)row * tightPitch,
               tightPitch);
    }
    outUpload->Unmap(0, nullptr);

    auto defaultHP = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    if (FAILED(device->CreateCommittedResource(
            &defaultHP, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&outResource))))
        return false;

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource        = outResource.Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource        = outUpload.Get();
    src.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint  = footprint;

    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    auto barrier = TransitionBarrier(outResource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);

    auto alloc = ctx.AllocateSRV();
    outSRV     = alloc.gpu;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                  = kAlbedoFmt;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(outResource.Get(), &srvDesc, alloc.cpu);

    return true;
}

// ---------------------------------------------------------------------------
bool Texture::LoadFromFile(ID3D12Device* device,
                           ID3D12GraphicsCommandList* cmdList,
                           DX12Context& ctx,
                           const std::wstring& path)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return false;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr,
            GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)))
        return false;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
    wic->CreateFormatConverter(&conv);
    conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    return UploadFromConverter(device, cmdList, ctx, conv.Get(),
                               m_Resource, m_UploadBuffer, m_SRVGpuHandle);
}

// ---------------------------------------------------------------------------
// LoadFromMemory — decodes a compressed image blob (PNG / JPEG / etc.) stored
// in memory.  Used for textures embedded inside .glb files by FBXLoader.
// The data pointer must remain valid until this call returns.
// ---------------------------------------------------------------------------
bool Texture::LoadFromMemory(ID3D12Device*              device,
                             ID3D12GraphicsCommandList* cmdList,
                             DX12Context&               ctx,
                             const void*                data,
                             size_t                     sizeBytes)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Wrap the raw byte buffer in a COM IStream via a global memory handle.
    // CreateStreamOnHGlobal(fDeleteOnRelease=TRUE) frees the HGLOBAL when
    // the stream's ref-count drops to zero.
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sizeBytes);
    if (!hg) return false;
    {
        void* dst = GlobalLock(hg);
        if (!dst) { GlobalFree(hg); return false; }
        memcpy(dst, data, sizeBytes);
        GlobalUnlock(hg);
    }

    Microsoft::WRL::ComPtr<IStream> stream;
    {
        IStream* raw = nullptr;
        if (FAILED(CreateStreamOnHGlobal(hg, /*fDeleteOnRelease=*/TRUE, &raw))) {
            GlobalFree(hg);
            return false;
        }
        stream.Attach(raw);   // adopts ownership; GlobalFree handled on Release
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return false;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr,
            WICDecodeMetadataCacheOnLoad, &decoder)))
        return false;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
    wic->CreateFormatConverter(&conv);
    conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    return UploadFromConverter(device, cmdList, ctx, conv.Get(),
                               m_Resource, m_UploadBuffer, m_SRVGpuHandle);
}

// ---------------------------------------------------------------------------
// LoadFromPixels — upload raw RGBA8 pixels without going through WIC.
// Intended for small programmatic textures (e.g. 1×1 flat normal map).
// ---------------------------------------------------------------------------
bool Texture::LoadFromPixels(ID3D12Device*              device,
                             ID3D12GraphicsCommandList* cmdList,
                             DX12Context&               ctx,
                             const uint8_t*             rgba,
                             UINT                       width,
                             UINT                       height)
{
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
    device->GetCopyableFootprints(&texDesc, 0, 1, 0,
                                  &footprint, nullptr, nullptr, &totalBytes);

    auto uploadHP   = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = BufferDesc(totalBytes);
    if (FAILED(device->CreateCommittedResource(
            &uploadHP, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_UploadBuffer))))
        return false;

    UINT tightPitch = width * 4;
    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    m_UploadBuffer->Map(0, &readRange, &mapped);
    for (UINT row = 0; row < height; ++row) {
        memcpy(static_cast<BYTE*>(mapped) + (UINT64)row * footprint.Footprint.RowPitch,
               rgba + (UINT64)row * tightPitch,
               tightPitch);
    }
    m_UploadBuffer->Unmap(0, nullptr);

    auto defaultHP = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    if (FAILED(device->CreateCommittedResource(
            &defaultHP, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_Resource))))
        return false;

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

    auto alloc      = ctx.AllocateSRV();
    m_SRVGpuHandle  = alloc.gpu;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(m_Resource.Get(), &srvDesc, alloc.cpu);

    return true;
}

void Texture::ReleaseUploadBuffer()
{
    m_UploadBuffer.Reset();
}

} // namespace VibeEngine
