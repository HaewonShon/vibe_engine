#define NOMINMAX
#include "BloomPass.h"
#include "Shader.h"
#include "DX12Helpers.h"
#include "../Core/Log.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdexcept>
#include <algorithm>

namespace VibeEngine {

// ---------------------------------------------------------------------------
// Helper: resolve the exe-adjacent Shaders/ directory at runtime.
// ---------------------------------------------------------------------------
static std::wstring GetShaderPath(const wchar_t* name)
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    path = path.substr(0, path.find_last_of(L"\\/") + 1);
    return path + L"Shaders/" + name;
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------
bool BloomPass::Initialize(ID3D12Device* device, DX12Context& ctx,
                           UINT width, UINT height)
{
    m_Width  = width;
    m_Height = height;

    // ---- Private RTV heap (4 slots) -----------------------------------------
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = 4;   // HDR, Bright, BlurH, BlurV
        desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_RTVHeap)))) {
            LOG_ERROR("BloomPass: failed to create RTV heap");
            return false;
        }
        m_RTVSize = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    if (!CreateTextures(device, ctx, width, height))  return false;
    if (!CreateRootSignature(device))                  return false;
    if (!CreatePipelines(device))                      return false;

    m_Ready = true;
    LOG_INFO("BloomPass: initialized (%ux%u HDR, %ux%u bloom)",
             width, height, width / 2, height / 2);
    return true;
}

// ---------------------------------------------------------------------------
void BloomPass::Shutdown()
{
    DestroyTextures();
    m_BrightPSO.Reset();
    m_BlurHPSO.Reset();
    m_BlurVPSO.Reset();
    m_CompositePSO.Reset();
    m_RootSig.Reset();
    m_RTVHeap.Reset();
    m_Ready = false;
}

// ---------------------------------------------------------------------------
void BloomPass::Resize(ID3D12Device* device, DX12Context& ctx,
                       UINT width, UINT height)
{
    if (width == 0 || height == 0) return;
    m_Width  = width;
    m_Height = height;
    DestroyTextures();
    CreateTextures(device, ctx, width, height);
}

// ---------------------------------------------------------------------------
// CreateTextures — allocate 4 intermediate render targets.
//   Slot 0: HDR full-res
//   Slot 1: Bright half-res
//   Slot 2: BlurH half-res
//   Slot 3: BlurV half-res
// ---------------------------------------------------------------------------
bool BloomPass::CreateTextures(ID3D12Device* device, DX12Context& ctx,
                               UINT w, UINT h)
{
    const UINT hw = std::max(1u, w / 2);
    const UINT hh = std::max(1u, h / 2);

    struct TexSpec { UINT w, h; RTex* dst; const char* name; };
    TexSpec specs[] = {
        { w,  h,  &m_HDR,    "HDR"    },
        { hw, hh, &m_Bright, "Bright" },
        { hw, hh, &m_BlurH,  "BlurH"  },
        { hw, hh, &m_BlurV,  "BlurV"  },
    };

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format   = HDRFormat;
    clearVal.Color[0] = clearVal.Color[1] = clearVal.Color[2] = 0.f;
    clearVal.Color[3] = 1.f;

    auto hp = HeapProps(D3D12_HEAP_TYPE_DEFAULT);

    for (int i = 0; i < 4; ++i) {
        auto& s = specs[i];

        // Resource
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width              = s.w;
        texDesc.Height             = s.h;
        texDesc.DepthOrArraySize   = 1;
        texDesc.MipLevels          = 1;
        texDesc.Format             = HDRFormat;
        texDesc.SampleDesc         = { 1, 0 };
        texDesc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        HRESULT hr = device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearVal,
            IID_PPV_ARGS(&s.dst->resource));
        if (FAILED(hr)) {
            LOG_ERROR("BloomPass: failed to create %s texture (0x%08X)",
                      s.name, static_cast<unsigned>(hr));
            return false;
        }

        // RTV (slot i in m_RTVHeap)
        s.dst->rtv = OffsetHandle(m_RTVHeap->GetCPUDescriptorHandleForHeapStart(),
                                  i, m_RTVSize);
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format        = HDRFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(s.dst->resource.Get(), &rtvDesc, s.dst->rtv);

        // SRV (from shared engine heap)
        auto alloc   = ctx.AllocateSRV();
        s.dst->srv   = alloc.gpu;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                  = HDRFormat;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels     = 1;
        device->CreateShaderResourceView(s.dst->resource.Get(), &srvDesc, alloc.cpu);
    }

    return true;
}

// ---------------------------------------------------------------------------
void BloomPass::DestroyTextures()
{
    m_HDR.resource.Reset();
    m_Bright.resource.Reset();
    m_BlurH.resource.Reset();
    m_BlurV.resource.Reset();
    // SRV handles are indices into the shared heap — they're not released here
    // (the engine SRV heap is a simple linear allocator).
}

// ---------------------------------------------------------------------------
// CreateRootSignature
//
//   [0] Root 32-bit constants (8 DWORDs) b0 — BloomConstants
//   [1] Descriptor table: 1 SRV t0
//   [2] Descriptor table: 1 SRV t1
//   s0  — LINEAR_CLAMP
// ---------------------------------------------------------------------------
bool BloomPass::CreateRootSignature(ID3D12Device* device)
{
    D3D12_ROOT_PARAMETER params[3] = {};

    // [0] Root constants
    params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace  = 0;
    params[0].Constants.Num32BitValues = 8;  // sizeof(BloomConstants)/4
    params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;

    // [1] t0
    D3D12_DESCRIPTOR_RANGE r0 = {};
    r0.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    r0.NumDescriptors                    = 1;
    r0.BaseShaderRegister                = 0;
    r0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &r0;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // [2] t1
    D3D12_DESCRIPTOR_RANGE r1 = {};
    r1.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    r1.NumDescriptors                    = 1;
    r1.BaseShaderRegister                = 1;
    r1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges   = &r1;
    params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // s0 — linear clamp for all bloom sampling
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD           = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister   = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters     = 3;
    desc.pParameters       = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers   = &sampler;
    desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob, errors;
    HRESULT hr = D3D12SerializeRootSignature(&desc,
        D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (FAILED(hr)) {
        if (errors)
            LOG_ERROR("BloomPass root sig: %s",
                      static_cast<char*>(errors->GetBufferPointer()));
        return false;
    }
    hr = device->CreateRootSignature(0,
        blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&m_RootSig));
    return SUCCEEDED(hr);
}

// ---------------------------------------------------------------------------
// CreatePipelines — compile Bloom.hlsl and build 4 PSOs.
// ---------------------------------------------------------------------------
bool BloomPass::CreatePipelines(ID3D12Device* device)
{
    const std::wstring path = GetShaderPath(L"Bloom.hlsl");

    ComPtr<ID3DBlob> vs, psBright, psBlurH, psBlurV, psComp;
    try {
        vs       = Shader::CompileFromFile(path, "VSFullscreen", "vs_5_0");
        psBright = Shader::CompileFromFile(path, "PSBrightPass", "ps_5_0");
        psBlurH  = Shader::CompileFromFile(path, "PSBlurH",      "ps_5_0");
        psBlurV  = Shader::CompileFromFile(path, "PSBlurV",      "ps_5_0");
        psComp   = Shader::CompileFromFile(path, "PSComposite",  "ps_5_0");
    } catch (const std::exception& e) {
        LOG_ERROR("BloomPass: shader compile failed — %s", e.what());
        return false;
    }

    // Base PSO desc shared by BrightPass / BlurH / BlurV (HDR output)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC base = {};
    base.pRootSignature                           = m_RootSig.Get();
    base.VS                                       = { vs->GetBufferPointer(), vs->GetBufferSize() };
    base.RasterizerState.FillMode                 = D3D12_FILL_MODE_SOLID;
    base.RasterizerState.CullMode                 = D3D12_CULL_MODE_NONE;
    base.RasterizerState.DepthClipEnable          = TRUE;
    base.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    base.SampleMask                               = UINT_MAX;
    base.PrimitiveTopologyType                    = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    base.NumRenderTargets                         = 1;
    base.RTVFormats[0]                            = HDRFormat;
    base.SampleDesc                               = { 1, 0 };
    base.DepthStencilState.DepthEnable            = FALSE;  // no depth test for post-process
    base.InputLayout                              = { nullptr, 0 };  // no vertex buffer

    // ---- BrightPass ---------------------------------------------------------
    {
        auto d  = base;
        d.PS    = { psBright->GetBufferPointer(), psBright->GetBufferSize() };
        if (FAILED(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&m_BrightPSO)))) {
            LOG_ERROR("BloomPass: BrightPSO creation failed");
            return false;
        }
    }

    // ---- BlurH --------------------------------------------------------------
    {
        auto d  = base;
        d.PS    = { psBlurH->GetBufferPointer(), psBlurH->GetBufferSize() };
        if (FAILED(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&m_BlurHPSO)))) {
            LOG_ERROR("BloomPass: BlurHPSO creation failed");
            return false;
        }
    }

    // ---- BlurV --------------------------------------------------------------
    {
        auto d  = base;
        d.PS    = { psBlurV->GetBufferPointer(), psBlurV->GetBufferSize() };
        if (FAILED(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&m_BlurVPSO)))) {
            LOG_ERROR("BloomPass: BlurVPSO creation failed");
            return false;
        }
    }

    // ---- Composite (outputs to UNORM back buffer) ---------------------------
    {
        auto d           = base;
        d.PS             = { psComp->GetBufferPointer(), psComp->GetBufferSize() };
        d.RTVFormats[0]  = BackBufferFormat;
        if (FAILED(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&m_CompositePSO)))) {
            LOG_ERROR("BloomPass: CompositePSO creation failed");
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// BeginCapture — transition HDR RT to RENDER_TARGET, bind it.
// Clear the HDR render target (depth is already cleared by DX12Context::BeginFrame).
// ---------------------------------------------------------------------------
void BloomPass::BeginCapture(ID3D12GraphicsCommandList* cmdList,
                             D3D12_CPU_DESCRIPTOR_HANDLE dsv,
                             UINT width, UINT height)
{
    // Transition HDR: PSR -> RT
    auto barrier = TransitionBarrier(m_HDR.resource.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT vp = { 0.f, 0.f,
        static_cast<float>(width), static_cast<float>(height), 0.f, 1.f };
    D3D12_RECT sc = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sc);

    cmdList->OMSetRenderTargets(1, &m_HDR.rtv, FALSE, &dsv);

    static constexpr float kClear[] = { 0.05f, 0.05f, 0.05f, 1.f };
    cmdList->ClearRenderTargetView(m_HDR.rtv, kClear, 0, nullptr);
}

// ---------------------------------------------------------------------------
// Apply — run bloom passes and composite to the back buffer.
// ---------------------------------------------------------------------------
void BloomPass::Apply(ID3D12GraphicsCommandList* cmdList,
                      D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
                      UINT width, UINT height)
{
    // Half-res dimensions
    const UINT hw = std::max(1u, width  / 2);
    const UINT hh = std::max(1u, height / 2);

    // Texel sizes for the half-res blur targets
    const float tw = 1.0f / static_cast<float>(hw);
    const float th = 1.0f / static_cast<float>(hh);

    // 1. HDR: RT -> PSR (scene finished writing to it)
    {
        auto b = TransitionBarrier(m_HDR.resource.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->ResourceBarrier(1, &b);
    }

    // Common: bind root signature once (shared by all passes)
    cmdList->SetGraphicsRootSignature(m_RootSig.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->IASetVertexBuffers(0, 0, nullptr);
    cmdList->IASetIndexBuffer(nullptr);

    // 2. BrightPass: HDR -> Bright (half res)
    {
        auto b = TransitionBarrier(m_Bright.resource.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->ResourceBarrier(1, &b);

        BloomConstants cb = { Threshold, Intensity,
                              1.f / static_cast<float>(width),
                              1.f / static_cast<float>(height),
                              Exposure };
        RunPass(cmdList, m_Bright.rtv,
                m_HDR.srv, m_HDR.srv,  // t1 unused — bind same as t0
                m_BrightPSO.Get(), cb, hw, hh);

        auto b2 = TransitionBarrier(m_Bright.resource.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->ResourceBarrier(1, &b2);
    }

    // 3. BlurH: Bright -> BlurH (half res, horizontal)
    {
        auto b = TransitionBarrier(m_BlurH.resource.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->ResourceBarrier(1, &b);

        BloomConstants cb = { Threshold, Intensity, tw, th, Exposure };
        RunPass(cmdList, m_BlurH.rtv,
                m_Bright.srv, m_Bright.srv,
                m_BlurHPSO.Get(), cb, hw, hh);

        auto b2 = TransitionBarrier(m_BlurH.resource.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->ResourceBarrier(1, &b2);
    }

    // 4. BlurV: BlurH -> BlurV (half res, vertical)
    {
        auto b = TransitionBarrier(m_BlurV.resource.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->ResourceBarrier(1, &b);

        BloomConstants cb = { Threshold, Intensity, tw, th, Exposure };
        RunPass(cmdList, m_BlurV.rtv,
                m_BlurH.srv, m_BlurH.srv,
                m_BlurVPSO.Get(), cb, hw, hh);

        auto b2 = TransitionBarrier(m_BlurV.resource.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->ResourceBarrier(1, &b2);
    }

    // 5. Composite: HDR + BlurV -> back buffer (full res)
    {
        BloomConstants cb = { Threshold, Intensity,
                              1.f / static_cast<float>(width),
                              1.f / static_cast<float>(height),
                              Exposure };
        RunPass(cmdList, backBufferRTV,
                m_HDR.srv, m_BlurV.srv,
                m_CompositePSO.Get(), cb, width, height);
    }
}

// ---------------------------------------------------------------------------
// RunPass — set viewport, RTV, PSO, constants, SRVs, draw.
// Root signature must already be bound before the first call.
// ---------------------------------------------------------------------------
void BloomPass::RunPass(ID3D12GraphicsCommandList* cmdList,
                        D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                        D3D12_GPU_DESCRIPTOR_HANDLE srv0,
                        D3D12_GPU_DESCRIPTOR_HANDLE srv1,
                        ID3D12PipelineState*       pso,
                        const BloomConstants&      cb,
                        UINT viewW, UINT viewH)
{
    D3D12_VIEWPORT vp = { 0.f, 0.f,
        static_cast<float>(viewW), static_cast<float>(viewH), 0.f, 1.f };
    D3D12_RECT sc = { 0, 0, static_cast<LONG>(viewW), static_cast<LONG>(viewH) };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sc);

    cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    cmdList->SetPipelineState(pso);

    // Root constants at [0] — update per-pass (safe: part of command stream)
    cmdList->SetGraphicsRoot32BitConstants(0, 8, &cb, 0);
    cmdList->SetGraphicsRootDescriptorTable(1, srv0);
    cmdList->SetGraphicsRootDescriptorTable(2, srv1);

    cmdList->DrawInstanced(4, 1, 0, 0);
}

} // namespace VibeEngine
