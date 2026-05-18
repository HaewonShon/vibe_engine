#include "SSAOPass.h"
#include "Shader.h"
#include "DX12Helpers.h"
#include "../Core/Log.h"
#include <stdexcept>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using namespace DirectX;

static std::wstring GetExeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    return path.substr(0, path.find_last_of(L"\\/") + 1);
}

namespace VibeEngine {

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------
bool SSAOPass::Initialize(ID3D12Device* device, DX12Context& ctx,
                           ID3D12GraphicsCommandList* cmdList,
                           UINT width, UINT height)
{
    m_Width  = width;
    m_Height = height;

    if (!CreateRootSignature(device)) {
        LOG_WARN("SSAOPass: CreateRootSignature failed");
        return false;
    }
    if (!CreatePipelines(device)) {
        LOG_WARN("SSAOPass: CreatePipelines failed");
        return false;
    }
    if (!CreateNoiseTex(device, ctx, cmdList)) {
        LOG_WARN("SSAOPass: CreateNoiseTex failed");
        return false;
    }
    if (!CreateFallback(device, ctx, cmdList)) {
        LOG_WARN("SSAOPass: CreateFallback failed");
        return false;
    }
    if (!CreateTextures(device, ctx, width, height)) {
        LOG_WARN("SSAOPass: CreateTextures failed");
        return false;
    }

    m_Ready      = true;
    m_HasValidAO = false;
    LOG_INFO("SSAOPass: initialized (%u×%u)", width, height);
    return true;
}

// ---------------------------------------------------------------------------
void SSAOPass::Shutdown()
{
    m_Ready      = false;
    m_HasValidAO = false;
    DestroyTextures();
    m_NoiseTex.Reset();
    m_NoiseUpload.Reset();
    m_FallbackTex.Reset();
    m_FallbackUpload.Reset();
    m_AOPassPSO.Reset();
    m_BlurPSO.Reset();
    m_RootSig.Reset();
    m_RTVHeap.Reset();
}

// ---------------------------------------------------------------------------
void SSAOPass::ReleaseUploadBuffers()
{
    m_NoiseUpload.Reset();
    m_FallbackUpload.Reset();
}

// ---------------------------------------------------------------------------
void SSAOPass::Resize(ID3D12Device* device, DX12Context& ctx,
                       UINT width, UINT height)
{
    m_Width     = width;
    m_Height    = height;
    m_HasValidAO = false;   // new textures; no valid AO yet
    DestroyTextures();
    CreateTextures(device, ctx, width, height);
}

// ---------------------------------------------------------------------------
// GetAOSRV
// ---------------------------------------------------------------------------
D3D12_GPU_DESCRIPTOR_HANDLE SSAOPass::GetAOSRV() const
{
    if (!m_Ready || !m_HasValidAO)
        return m_FallbackSRV;
    return m_AOblur.srv;
}

// ---------------------------------------------------------------------------
// ComputeAO
// ---------------------------------------------------------------------------
void SSAOPass::ComputeAO(ID3D12GraphicsCommandList* cmdList,
                          D3D12_GPU_DESCRIPTOR_HANDLE depthSRV,
                          const XMMATRIX& proj,
                          const XMMATRIX& invProj,
                          UINT width, UINT height)
{
    if (!m_Ready) return;

    // Build constants
    SSAOConstants cb = {};
    XMStoreFloat4x4(&cb.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&cb.Proj,    XMMatrixTranspose(proj));
    cb.NoiseScaleX = static_cast<float>(width)  / 4.0f;
    cb.NoiseScaleY = static_cast<float>(height) / 4.0f;
    cb.Radius    = Radius;
    cb.Bias      = Bias;
    cb.Intensity = Intensity;

    static_assert(sizeof(SSAOConstants) == 160,
                  "SSAOConstants must be exactly 40 DWORDs (160 bytes)");

    // ---- Pass 1: Compute AO into m_AOraw -----------------------------------
    {
        // Ensure AOraw is in RENDER_TARGET state
        if (m_AOrawState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            auto b = TransitionBarrier(m_AOraw.resource.Get(),
                                       m_AOrawState,
                                       D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmdList->ResourceBarrier(1, &b);
            m_AOrawState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        RunFullscreenPass(cmdList, m_AOraw.rtv,
                          depthSRV,       // t0 = depth
                          m_NoiseSRV,     // t1 = noise
                          m_AOPassPSO.Get(), cb, width, height);

        // Transition AOraw to PSR so the blur pass can read it
        auto b = TransitionBarrier(m_AOraw.resource.Get(),
                                   D3D12_RESOURCE_STATE_RENDER_TARGET,
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->ResourceBarrier(1, &b);
        m_AOrawState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // ---- Pass 2: Blur AOraw into m_AOblur ----------------------------------
    {
        // Ensure AOblur is in RENDER_TARGET state
        if (m_AOblurState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            auto b = TransitionBarrier(m_AOblur.resource.Get(),
                                       m_AOblurState,
                                       D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmdList->ResourceBarrier(1, &b);
            m_AOblurState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        // Repurpose NoiseScale as texel size for the blur pass
        SSAOConstants blurCb = cb;
        blurCb.NoiseScaleX = 1.0f / static_cast<float>(width);
        blurCb.NoiseScaleY = 1.0f / static_cast<float>(height);

        // t1 is unused in the blur pass; re-use the noise SRV (harmless)
        RunFullscreenPass(cmdList, m_AOblur.rtv,
                          m_AOraw.srv,   // t0 = raw AO
                          m_NoiseSRV,    // t1 = unused
                          m_BlurPSO.Get(), blurCb, width, height);

        // Transition AOblur to PSR so Basic.hlsl can sample it next frame
        auto b = TransitionBarrier(m_AOblur.resource.Get(),
                                   D3D12_RESOURCE_STATE_RENDER_TARGET,
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->ResourceBarrier(1, &b);
        m_AOblurState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    m_HasValidAO = true;
}

// ---------------------------------------------------------------------------
// RunFullscreenPass — binds root sig, PSO, constants, SRVs, viewport, RTV
// ---------------------------------------------------------------------------
void SSAOPass::RunFullscreenPass(ID3D12GraphicsCommandList* cmdList,
                                  D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                                  D3D12_GPU_DESCRIPTOR_HANDLE srv0,
                                  D3D12_GPU_DESCRIPTOR_HANDLE srv1,
                                  ID3D12PipelineState*        pso,
                                  const SSAOConstants&        cb,
                                  UINT w, UINT h)
{
    cmdList->SetGraphicsRootSignature(m_RootSig.Get());
    cmdList->SetPipelineState(pso);

    // Root constants (40 DWORDs at param [0])
    cmdList->SetGraphicsRoot32BitConstants(
        0, sizeof(SSAOConstants) / 4, &cb, 0);
    // t0
    cmdList->SetGraphicsRootDescriptorTable(1, srv0);
    // t1
    cmdList->SetGraphicsRootDescriptorTable(2, srv1);

    D3D12_VIEWPORT vp = { 0.f, 0.f,
        static_cast<float>(w), static_cast<float>(h), 0.f, 1.f };
    D3D12_RECT sc = { 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sc);

    cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);   // full-screen triangle (no VB)
}

// ---------------------------------------------------------------------------
// CreateTextures — AOraw + AOblur (R8_UNORM, full resolution)
// ---------------------------------------------------------------------------
bool SSAOPass::CreateTextures(ID3D12Device* device, DX12Context& ctx,
                               UINT w, UINT h)
{
    // ---- Private RTV heap (2 slots) -----------------------------------------
    if (!m_RTVHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = 2;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_RTVHeap))))
            return false;
        m_RTVSize = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    auto createTex = [&](RTex& tex, UINT slot) -> bool
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = w;
        desc.Height           = h;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc       = { 1, 0 };
        desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE cv = {};
        cv.Format   = DXGI_FORMAT_R8_UNORM;
        cv.Color[0] = 1.0f;   // clear to white (no occlusion)

        auto hp = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
        if (FAILED(device->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
                IID_PPV_ARGS(&tex.resource))))
            return false;

        // RTV
        tex.rtv = OffsetHandle(
            m_RTVHeap->GetCPUDescriptorHandleForHeapStart(), slot, m_RTVSize);
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format        = DXGI_FORMAT_R8_UNORM;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(tex.resource.Get(), &rtvDesc, tex.rtv);

        // SRV (in shared heap)
        auto alloc = ctx.AllocateSRV();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R8_UNORM;
        srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels       = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        device->CreateShaderResourceView(tex.resource.Get(), &srvDesc, alloc.cpu);
        tex.srv = alloc.gpu;

        return true;
    };

    if (!createTex(m_AOraw,  0)) return false;
    if (!createTex(m_AOblur, 1)) return false;

    m_AOrawState  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_AOblurState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    return true;
}

// ---------------------------------------------------------------------------
void SSAOPass::DestroyTextures()
{
    m_AOraw.resource.Reset();
    m_AOblur.resource.Reset();
    // SRV handles remain in the shared heap (slots are never reclaimed),
    // but the descriptors will be overwritten on the next CreateTextures call.
}

// ---------------------------------------------------------------------------
// CreateNoiseTex — 4×4 R8G8B8A8 texture of random normalized 2D vectors.
// ---------------------------------------------------------------------------
bool SSAOPass::CreateNoiseTex(ID3D12Device* device, DX12Context& ctx,
                               ID3D12GraphicsCommandList* cmdList)
{
    // 16 random 2D unit vectors encoded as RGBA8 [0,255]
    // Channel R = (cos θ + 1) / 2 × 255,  G = (sin θ + 1) / 2 × 255
    // Generated from golden-angle distribution (θ_i = i × 137.508°).
    static const uint8_t kNoise[16][4] = {
        { 255, 128, 0, 0 },   // i=0   0°
        {  34, 214, 0, 0 },   // i=1   137.5°
        { 139,   0, 0, 0 },   // i=2   275°
        { 205, 229, 0, 0 },   // i=3   52.5°
        {   2, 106, 0, 0 },   // i=4   190°
        { 234,  59, 0, 0 },   // i=5   327.5°
        {  95, 250, 0, 0 },   // i=6   105°
        {  69,  14, 0, 0 },   // i=7   242.5°
        { 248, 172, 0, 0 },   // i=8   20°
        {  10, 178, 0, 0 },   // i=9   157.5°
        { 182,  12, 0, 0 },   // i=10  295°
        { 166, 250, 0, 0 },   // i=11  72.5°
        {  17,  64, 0, 0 },   // i=12  210°
        { 252, 100, 0, 0 },   // i=13  347.5°
        {  54, 232, 0, 0 },   // i=14  125°
        { 111,   1, 0, 0 },   // i=15  262.5°
    };

    // ---- Create default-heap resource (4×4, R8G8B8A8_UNORM) ----------------
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = 4;
    desc.Height           = 4;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc       = { 1, 0 };

    auto hp = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    if (FAILED(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_NoiseTex))))
        return false;

    // ---- Upload heap — row pitch must be 256-byte aligned ------------------
    // Each row: 4 pixels × 4 bytes = 16 bytes.  D3D12 requires ≥ 256.
    constexpr UINT kRowPitch   = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT; // 256
    constexpr UINT kUploadSize = 4 * kRowPitch;                      // 4 rows

    D3D12_RESOURCE_DESC upDesc = {};
    upDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    upDesc.Width            = kUploadSize;
    upDesc.Height           = 1;
    upDesc.DepthOrArraySize = 1;
    upDesc.MipLevels        = 1;
    upDesc.SampleDesc       = { 1, 0 };
    upDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    auto upHp = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    if (FAILED(device->CreateCommittedResource(
            &upHp, D3D12_HEAP_FLAG_NONE, &upDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_NoiseUpload))))
        return false;

    // Map and fill row by row (with 256-byte row padding)
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    m_NoiseUpload->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    for (UINT row = 0; row < 4; ++row) {
        memcpy(mapped + row * kRowPitch,
               kNoise + row * 4,
               4 * 4);   // 4 pixels × 4 bytes
    }
    m_NoiseUpload->Unmap(0, nullptr);

    // Copy upload → default
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource        = m_NoiseTex.Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource                          = m_NoiseUpload.Get();
    src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset             = 0;
    src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width    = 4;
    src.PlacedFootprint.Footprint.Height   = 4;
    src.PlacedFootprint.Footprint.Depth    = 1;
    src.PlacedFootprint.Footprint.RowPitch = kRowPitch;

    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    auto b = TransitionBarrier(m_NoiseTex.Get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &b);

    // Allocate SRV in the shared heap
    auto alloc = ctx.AllocateSRV();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels       = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(m_NoiseTex.Get(), &srvDesc, alloc.cpu);
    m_NoiseSRV = alloc.gpu;

    return true;
}

// ---------------------------------------------------------------------------
// CreateFallback — 1×1 R8_UNORM white texture (AO=1, no occlusion)
// ---------------------------------------------------------------------------
bool SSAOPass::CreateFallback(ID3D12Device* device, DX12Context& ctx,
                               ID3D12GraphicsCommandList* cmdList)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = 1;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc       = { 1, 0 };

    auto hp = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    if (FAILED(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_FallbackTex))))
        return false;

    // Upload heap (minimum 512 bytes, but we only write 1 byte)
    D3D12_RESOURCE_DESC upDesc = {};
    upDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    upDesc.Width            = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;  // 256
    upDesc.Height           = 1;
    upDesc.DepthOrArraySize = 1;
    upDesc.MipLevels        = 1;
    upDesc.SampleDesc       = { 1, 0 };
    upDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    auto upHp = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    if (FAILED(device->CreateCommittedResource(
            &upHp, D3D12_HEAP_FLAG_NONE, &upDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_FallbackUpload))))
        return false;

    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    m_FallbackUpload->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    mapped[0] = 255;   // R8_UNORM 255 = 1.0 (fully lit)
    m_FallbackUpload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource        = m_FallbackTex.Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource                          = m_FallbackUpload.Get();
    src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8_UNORM;
    src.PlacedFootprint.Footprint.Width    = 1;
    src.PlacedFootprint.Footprint.Height   = 1;
    src.PlacedFootprint.Footprint.Depth    = 1;
    src.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    auto b = TransitionBarrier(m_FallbackTex.Get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &b);

    auto alloc = ctx.AllocateSRV();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R8_UNORM;
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels       = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(m_FallbackTex.Get(), &srvDesc, alloc.cpu);
    m_FallbackSRV = alloc.gpu;

    return true;
}

// ---------------------------------------------------------------------------
// CreateRootSignature
// ---------------------------------------------------------------------------
bool SSAOPass::CreateRootSignature(ID3D12Device* device)
{
    // [0] 40 root 32-bit constants (b0) — SSAO + blur parameters
    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace  = 0;
    params[0].Constants.Num32BitValues = 40;
    params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;

    // [1] Descriptor table: 1 SRV at t0 (depth or AO-raw)
    D3D12_DESCRIPTOR_RANGE range0 = {};
    range0.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range0.NumDescriptors                    = 1;
    range0.BaseShaderRegister                = 0;
    range0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &range0;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // [2] Descriptor table: 1 SRV at t1 (noise)
    D3D12_DESCRIPTOR_RANGE range1 = {};
    range1.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range1.NumDescriptors                    = 1;
    range1.BaseShaderRegister                = 1;
    range1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges   = &range1;
    params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // s0 — linear clamp (depth and AO sampling)
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    samplers[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MaxLOD           = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister   = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1 — point wrap (noise tiling)
    samplers[1].Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[1].MaxLOD           = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister   = 1;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 3;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = 2;
    rsDesc.pStaticSamplers   = samplers;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob, errors;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc,
        D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA(static_cast<char*>(errors->GetBufferPointer()));
        return false;
    }
    return SUCCEEDED(device->CreateRootSignature(0,
        blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&m_RootSig)));
}

// ---------------------------------------------------------------------------
// CreatePipelines — compile SSAO.hlsl and build two PSOs
// ---------------------------------------------------------------------------
bool SSAOPass::CreatePipelines(ID3D12Device* device)
{
    const std::wstring shaderPath = GetExeDir() + L"Shaders/SSAO.hlsl";

    ComPtr<ID3DBlob> vs, psAO, psBlur;
    try {
        vs     = Shader::CompileFromFile(shaderPath, "VSFullscreen", "vs_5_0");
        psAO   = Shader::CompileFromFile(shaderPath, "PSComputeAO",  "ps_5_0");
        psBlur = Shader::CompileFromFile(shaderPath, "PSBlur",        "ps_5_0");
    } catch (const std::exception& e) {
        OutputDebugStringA(e.what());
        return false;
    }

    // Common PSO desc (no depth, no input layout — full-screen triangle)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = m_RootSig.Get();
    psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.RasterizerState.FillMode          = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode          = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable   = TRUE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8_UNORM;
    psoDesc.SampleDesc            = { 1, 0 };
    psoDesc.DepthStencilState.DepthEnable = FALSE;

    psoDesc.PS = { psAO->GetBufferPointer(), psAO->GetBufferSize() };
    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_AOPassPSO))))
        return false;

    psoDesc.PS = { psBlur->GetBufferPointer(), psBlur->GetBufferSize() };
    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_BlurPSO))))
        return false;

    return true;
}

} // namespace VibeEngine
