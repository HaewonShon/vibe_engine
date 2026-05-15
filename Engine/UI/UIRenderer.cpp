#include "UIRenderer.h"
#include "../Core/Log.h"
#include "../Renderer/Shader.h"
#include <d3d12.h>
#include <algorithm>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// Exe-directory helper (same pattern as BasicPipeline.cpp)
static std::wstring GetExeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    return path.substr(0, path.find_last_of(L"\\/") + 1);
}

namespace VibeEngine {

// ============================================================================
// UI vertex layout
// ============================================================================
struct UIVertex {
    float x, y;        // screen pixels
    float u, v;        // atlas UV [0, 1]
    float r, g, b, a;  // linear RGBA
};

static_assert(sizeof(UIVertex) == 32, "UIVertex must be 32 bytes");

// First printable ASCII (space = 0x20)
static constexpr int kFirstChar = 32;
static constexpr int kLastChar  = 126; // '~'
static constexpr int kNumChars  = kLastChar - kFirstChar + 1; // 95

// ============================================================================
// BuildFontAtlas
//
// Uses GDI to render Courier New into a DIB, then extracts luminance as alpha
// into an RGBA texture atlas on the GPU.
//
// Atlas layout: kAtlasCols chars per row, rows as needed.
// The top-left pixel (0,0) is forced to fully opaque white — used as the
// solid-rect sentinel (UV = almost-zero maps to it).
// ============================================================================
bool UIRenderer::BuildFontAtlas(ID3D12Device* device,
                                 ID3D12GraphicsCommandList* cmdList)
{
    const int fontSize  = 16;
    const int cols      = m_AtlasCols;
    const int rows      = (kNumChars + cols - 1) / cols; // ceil

    // ---- Create a GDI DC + font to measure metrics -------------------------
    HDC     hdc    = CreateCompatibleDC(nullptr);
    HFONT   hfont  = CreateFontA(
        fontSize, 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, "Courier New");
    HFONT   oldFont = static_cast<HFONT>(SelectObject(hdc, hfont));

    TEXTMETRICA tm = {};
    GetTextMetricsA(hdc, &tm);
    m_CharW  = tm.tmAveCharWidth;
    m_CharH  = tm.tmHeight;
    m_AtlasW = cols * m_CharW;
    m_AtlasH = rows * m_CharH;

    // ---- Create a 32-bit top-down DIB for rendering ------------------------
    BITMAPINFOHEADER bmi = {};
    bmi.biSize        = sizeof(bmi);
    bmi.biWidth       = m_AtlasW;
    bmi.biHeight      = -m_AtlasH; // top-down (positive = bottom-up in GDI)
    bmi.biPlanes      = 1;
    bmi.biBitCount    = 32;
    bmi.biCompression = BI_RGB;

    void*   bits = nullptr;
    HBITMAP hbm  = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bmi),
                                    DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP oldBm = static_cast<HBITMAP>(SelectObject(hdc, hbm));

    // Zero-fill (all black, alpha will be derived from luminance later)
    memset(bits, 0, static_cast<size_t>(m_AtlasW) * m_AtlasH * 4);

    SetBkMode   (hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    for (int i = 0; i < kNumChars; ++i) {
        char c   = static_cast<char>(kFirstChar + i);
        int  col = i % cols;
        int  row = i / cols;
        TextOutA(hdc, col * m_CharW, row * m_CharH, &c, 1);
    }
    GdiFlush();

    // ---- Convert BGRA → RGBA, luminance → alpha ----------------------------
    const auto* src = static_cast<const uint8_t*>(bits);
    std::vector<uint8_t> rgba(static_cast<size_t>(m_AtlasW) * m_AtlasH * 4);

    for (int p = 0; p < m_AtlasW * m_AtlasH; ++p) {
        uint8_t b   = src[p * 4 + 0];
        uint8_t g   = src[p * 4 + 1];
        uint8_t r   = src[p * 4 + 2];
        uint8_t lum = static_cast<uint8_t>((r + g + b) / 3);
        rgba[p*4+0] = 255;   // white font, tinted by vertex color in PS
        rgba[p*4+1] = 255;
        rgba[p*4+2] = 255;
        rgba[p*4+3] = lum;   // alpha = luminance (0 = transparent, 255 = opaque)
    }
    // Pixel (0,0) = fully opaque white → sentinel for solid quads
    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 255;

    // ---- Cleanup GDI -------------------------------------------------------
    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldBm);
    DeleteObject(hfont);
    DeleteObject(hbm);
    DeleteDC(hdc);

    // ---- Create GPU texture (DXGI_FORMAT_R8G8B8A8_UNORM) ------------------
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = static_cast<UINT>(m_AtlasW);
    texDesc.Height           = static_cast<UINT>(m_AtlasH);
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1;
    texDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc       = { 1, 0 };
    texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeap = { D3D12_HEAP_TYPE_DEFAULT };
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE,
        &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(m_FontTex.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("UIRenderer: failed to create font texture (hr=0x%08X)", hr);
        return false;
    }

    // ---- Create upload buffer for initial data transfer --------------------
    const UINT64 uploadSize =
        static_cast<UINT64>(m_AtlasW) * m_AtlasH * 4; // RGBA8
    // DX12 requires row pitch to be aligned to 256 bytes
    const UINT rowPitch =
        (static_cast<UINT>(m_AtlasW) * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    const UINT64 alignedUploadSize =
        static_cast<UINT64>(rowPitch) * m_AtlasH;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width            = alignedUploadSize;
    uploadDesc.Height           = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels        = 1;
    uploadDesc.Format           = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc       = { 1, 0 };
    uploadDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
    hr = device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE,
        &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(m_FontUpload.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("UIRenderer: failed to create font upload buffer (hr=0x%08X)", hr);
        return false;
    }

    // ---- Copy CPU data into upload buffer ----------------------------------
    void* mapped = nullptr;
    m_FontUpload->Map(0, nullptr, &mapped);
    auto* dst = static_cast<uint8_t*>(mapped);
    for (int row = 0; row < m_AtlasH; ++row) {
        memcpy(dst + static_cast<size_t>(row) * rowPitch,
               rgba.data() + static_cast<size_t>(row) * m_AtlasW * 4,
               static_cast<size_t>(m_AtlasW) * 4);
    }
    m_FontUpload->Unmap(0, nullptr);

    // ---- Record copy commands ----------------------------------------------
    D3D12_TEXTURE_COPY_LOCATION dst_loc = {};
    dst_loc.pResource = m_FontTex.Get();
    dst_loc.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src_loc = {};
    src_loc.pResource       = m_FontUpload.Get();
    src_loc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint.Offset               = 0;
    src_loc.PlacedFootprint.Footprint.Format     = DXGI_FORMAT_R8G8B8A8_UNORM;
    src_loc.PlacedFootprint.Footprint.Width      = static_cast<UINT>(m_AtlasW);
    src_loc.PlacedFootprint.Footprint.Height     = static_cast<UINT>(m_AtlasH);
    src_loc.PlacedFootprint.Footprint.Depth      = 1;
    src_loc.PlacedFootprint.Footprint.RowPitch   = rowPitch;

    cmdList->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);

    // Transition to shader resource
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = m_FontTex.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    // ---- Create SRV descriptor heap (1 descriptor) -------------------------
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&heapDesc,
                                       IID_PPV_ARGS(m_SRVHeap.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("UIRenderer: failed to create SRV heap (hr=0x%08X)", hr);
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels       = 1;
    device->CreateShaderResourceView(m_FontTex.Get(), &srvDesc,
                                     m_SRVHeap->GetCPUDescriptorHandleForHeapStart());

    LOG_INFO("UIRenderer: font atlas %dx%d, char %dx%d",
             m_AtlasW, m_AtlasH, m_CharW, m_CharH);
    return true;
}

// ============================================================================
// BuildPipeline
// ============================================================================
bool UIRenderer::BuildPipeline(ID3D12Device* device, DXGI_FORMAT rtFormat)
{
    // ---- Root signature (raw D3D12 API, no CD3DX12 helpers) ----------------
    // [0] Root constants b0 — 16 floats (ortho matrix), VS-visible
    // [1] Root constant  b1 —  1 int   (useTexture flag), PS-visible
    // [2] Descriptor table  — 1 SRV at t0, PS-visible
    // Static sampler s0 (linear + clamp)

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 1;
    srvRange.BaseShaderRegister                = 0;
    srvRange.RegisterSpace                     = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[3] = {};

    // [0] ortho matrix: 16 x 32-bit root constants at b0
    params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace  = 0;
    params[0].Constants.Num32BitValues = 16;
    params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

    // [1] useTexture flag: 1 x 32-bit root constant at b1
    params[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.ShaderRegister = 1;
    params[1].Constants.RegisterSpace  = 0;
    params[1].Constants.Num32BitValues = 1;
    params[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;

    // [2] font SRV descriptor table at t0
    params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges   = &srvRange;
    params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD           = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister   = 0;
    sampler.RegisterSpace    = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 3;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &sampler;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    HRESULT hr = D3D12SerializeRootSignature(
        &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        rsBlob.GetAddressOf(), rsErr.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("UIRenderer: root sig serialization failed: %s",
                  rsErr ? (char*)rsErr->GetBufferPointer() : "");
        return false;
    }
    hr = device->CreateRootSignature(
        0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
        IID_PPV_ARGS(m_RootSig.GetAddressOf()));
    if (FAILED(hr)) { LOG_ERROR("UIRenderer: CreateRootSignature failed"); return false; }

    // ---- Compile shaders from file (wstring path like BasicPipeline) -------
    std::wstring uiHlsl = GetExeDir() + L"Shaders/UI.hlsl";
    auto vsBlob = Shader::CompileFromFile(uiHlsl, "VS_Main", "vs_5_0");
    auto psBlob = Shader::CompileFromFile(uiHlsl, "PS_Main", "ps_5_0");
    if (!vsBlob || !psBlob) {
        LOG_ERROR("UIRenderer: shader compilation failed");
        return false;
    }

    // ---- PSO ---------------------------------------------------------------
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0,  8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Alpha blend: src * src_a + dst * (1 - src_a)
    D3D12_RENDER_TARGET_BLEND_DESC blendRT = {};
    blendRT.BlendEnable           = TRUE;
    blendRT.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    blendRT.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    blendRT.BlendOp               = D3D12_BLEND_OP_ADD;
    blendRT.SrcBlendAlpha         = D3D12_BLEND_ONE;
    blendRT.DestBlendAlpha        = D3D12_BLEND_ZERO;
    blendRT.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    blendRT.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature    = m_RootSig.Get();
    psoDesc.VS                = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS                = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.InputLayout       = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets  = 1;
    psoDesc.RTVFormats[0]     = rtFormat;
    psoDesc.DSVFormat         = DXGI_FORMAT_UNKNOWN;   // no depth attachment
    psoDesc.SampleDesc        = { 1, 0 };
    psoDesc.SampleMask        = UINT_MAX;
    psoDesc.BlendState.RenderTarget[0] = blendRT;
    // Rasterizer: cull none, solid fill
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    // Depth: disabled
    psoDesc.DepthStencilState.DepthEnable   = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    hr = device->CreateGraphicsPipelineState(&psoDesc,
                                              IID_PPV_ARGS(m_PSO.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("UIRenderer: CreateGraphicsPipelineState failed (hr=0x%08X)", hr);
        return false;
    }
    return true;
}

// ============================================================================
// Initialize
// ============================================================================
bool UIRenderer::Initialize(ID3D12Device* device,
                              ID3D12GraphicsCommandList* uploadCmdList,
                              DXGI_FORMAT backBufferFormat,
                              UINT screenW, UINT screenH)
{
    if (m_Initialized) return true;

    m_LastScreenW = screenW;
    m_LastScreenH = screenH;

    if (!BuildFontAtlas(device, uploadCmdList)) return false;
    if (!BuildPipeline (device, backBufferFormat)) return false;

    // ---- Dynamic vertex buffer (persistently mapped, upload heap) ----------
    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width            = sizeof(UIVertex) * kMaxVerts;
    vbDesc.Height           = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels        = 1;
    vbDesc.Format           = DXGI_FORMAT_UNKNOWN;
    vbDesc.SampleDesc       = { 1, 0 };
    vbDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    vbDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
    HRESULT hr = device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE,
        &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(m_VB.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("UIRenderer: failed to create dynamic VB (hr=0x%08X)", hr);
        return false;
    }
    m_VB->Map(0, nullptr, &m_VBData);

    m_Initialized = true;
    LOG_INFO("UIRenderer: initialized (screen %ux%u)", screenW, screenH);
    return true;
}

void UIRenderer::ReleaseUploadBuffer() { m_FontUpload.Reset(); }

void UIRenderer::Shutdown()
{
    if (!m_Initialized) return;
    if (m_VB && m_VBData) {
        m_VB->Unmap(0, nullptr);
        m_VBData = nullptr;
    }
    m_PSO.Reset();
    m_RootSig.Reset();
    m_FontTex.Reset();
    m_FontUpload.Reset();
    m_SRVHeap.Reset();
    m_VB.Reset();
    m_Initialized = false;
    LOG_INFO("UIRenderer: shutdown.");
}

// ============================================================================
// BeginPass / EndPass
// ============================================================================
void UIRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList,
                            UINT screenW, UINT screenH)
{
    if (!m_Initialized || m_InPass) return;
    m_CmdList  = cmdList;
    m_ScreenW  = screenW;
    m_ScreenH  = screenH;
    m_VBCursor = 0;
    m_InPass   = true;

    // Rebuild ortho matrix only if screen size changed
    if (screenW != m_LastScreenW || screenH != m_LastScreenH) {
        m_LastScreenW = screenW;
        m_LastScreenH = screenH;
    }
    XMMATRIX ortho = XMMatrixOrthographicOffCenterLH(
        0.f, static_cast<float>(screenW),
        static_cast<float>(screenH), 0.f,  // bottom > top → Y=0 at top
        0.f, 1.f);
    XMStoreFloat4x4(&m_OrthoMatrix, XMMatrixTranspose(ortho)); // HLSL uses column-major

    // ---- Set pipeline state ------------------------------------------------
    cmdList->SetGraphicsRootSignature(m_RootSig.Get());
    cmdList->SetPipelineState(m_PSO.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set vertex buffer (full range; draw calls use StartVertexLocation)
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = m_VB->GetGPUVirtualAddress();
    vbView.SizeInBytes    = sizeof(UIVertex) * kMaxVerts;
    vbView.StrideInBytes  = sizeof(UIVertex);
    cmdList->IASetVertexBuffers(0, 1, &vbView);

    // Set descriptor heap + font SRV
    ID3D12DescriptorHeap* heaps[] = { m_SRVHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootDescriptorTable(2, m_SRVHeap->GetGPUDescriptorHandleForHeapStart());

    // Set ortho matrix (16 root constants)
    cmdList->SetGraphicsRoot32BitConstants(0, 16, &m_OrthoMatrix, 0);

    // Viewport & scissor matching full screen
    D3D12_VIEWPORT vp = { 0.f, 0.f,
                           static_cast<float>(screenW), static_cast<float>(screenH),
                           0.f, 1.f };
    D3D12_RECT sr    = { 0, 0, static_cast<LONG>(screenW), static_cast<LONG>(screenH) };
    cmdList->RSSetViewports  (1, &vp);
    cmdList->RSSetScissorRects(1, &sr);
}

void UIRenderer::EndPass()
{
    m_CmdList = nullptr;
    m_InPass  = false;
}

// ============================================================================
// FlushQuad — appends 6 vertices (2 triangles) to the dynamic VB
// ============================================================================
void UIRenderer::FlushQuad(float x0, float y0, float x1, float y1,
                             float u0, float v0, float u1, float v1,
                             XMFLOAT4 color)
{
    if (!m_CmdList) return;
    if (m_VBCursor + 6 > kMaxVerts) {
        LOG_WARN("UIRenderer: dynamic VB full, skipping quad");
        return;
    }

    UIVertex* verts = static_cast<UIVertex*>(m_VBData) + m_VBCursor;

    //  0---1
    //  |  /|
    //  | / |
    //  2---3
    UIVertex tl = { x0, y0, u0, v0, color.x, color.y, color.z, color.w };
    UIVertex tr = { x1, y0, u1, v0, color.x, color.y, color.z, color.w };
    UIVertex bl = { x0, y1, u0, v1, color.x, color.y, color.z, color.w };
    UIVertex br = { x1, y1, u1, v1, color.x, color.y, color.z, color.w };

    verts[0] = tl; verts[1] = tr; verts[2] = bl;  // tri 0
    verts[3] = tr; verts[4] = br; verts[5] = bl;  // tri 1

    UINT first = m_VBCursor;
    m_VBCursor += 6;

    m_CmdList->DrawInstanced(6, 1, first, 0);
}

// ============================================================================
// DrawRect — solid color rectangle
// ============================================================================
void UIRenderer::DrawRect(float x, float y, float w, float h, XMFLOAT4 color)
{
    if (!m_InPass) return;
    // Flag: no texture
    int flag = 0;
    m_CmdList->SetGraphicsRoot32BitConstants(1, 1, &flag, 0);

    FlushQuad(x, y, x + w, y + h,
              0.f, 0.f, 0.f, 0.f,  // UV irrelevant for solid
              color);
}

// ============================================================================
// DrawBorder — hollow rectangle outline
// ============================================================================
void UIRenderer::DrawBorder(float x, float y, float w, float h,
                              float t, XMFLOAT4 color)
{
    if (!m_InPass) return;
    int flag = 0;
    m_CmdList->SetGraphicsRoot32BitConstants(1, 1, &flag, 0);

    // Top, bottom, left, right stripes
    FlushQuad(x,         y,         x + w,     y + t,     0,0,0,0, color); // top
    FlushQuad(x,         y + h - t, x + w,     y + h,     0,0,0,0, color); // bottom
    FlushQuad(x,         y + t,     x + t,     y + h - t, 0,0,0,0, color); // left
    FlushQuad(x + w - t, y + t,     x + w,     y + h - t, 0,0,0,0, color); // right
}

// ============================================================================
// DrawText — render a string using the font atlas
// ============================================================================
void UIRenderer::DrawText(float x, float y, const char* text,
                           XMFLOAT4 color, float scale)
{
    if (!m_InPass || !text) return;

    int flag = 1; // sample texture
    m_CmdList->SetGraphicsRoot32BitConstants(1, 1, &flag, 0);

    const float cw  = m_CharW * scale;
    const float ch  = m_CharH * scale;
    const float rcp_atlasW = 1.f / static_cast<float>(m_AtlasW);
    const float rcp_atlasH = 1.f / static_cast<float>(m_AtlasH);

    float cx = x;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { cx = x; y += ch; continue; }

        int idx = static_cast<unsigned char>(*p) - kFirstChar;
        if (idx < 0 || idx >= kNumChars) { cx += cw; continue; }

        int col = idx % m_AtlasCols;
        int row = idx / m_AtlasCols;

        float u0 = col       * m_CharW * rcp_atlasW;
        float u1 = (col + 1) * m_CharW * rcp_atlasW;
        float v0 = row       * m_CharH * rcp_atlasH;
        float v1 = (row + 1) * m_CharH * rcp_atlasH;

        FlushQuad(cx, y, cx + cw, y + ch, u0, v0, u1, v1, color);
        cx += cw;
    }
}

// ============================================================================
// MeasureText — returns pixel width of a string
// ============================================================================
float UIRenderer::MeasureText(const char* text, float scale) const
{
    if (!text) return 0.f;
    float w = 0.f, maxW = 0.f;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { maxW = (std::max)(maxW, w); w = 0.f; }
        else w += m_CharW * scale;
    }
    return (std::max)(maxW, w);
}

} // namespace VibeEngine
