#include "ShadowMap.h"
#include "../Core/Log.h"
#include <cmath>

using namespace DirectX;

namespace VibeEngine {

// ---------------------------------------------------------------------------
bool ShadowMap::Initialize(ID3D12Device* device, DX12Context& ctx)
{
    // ---- Depth texture -------------------------------------------------------
    // R32_TYPELESS: lets us create a D32_FLOAT DSV and an R32_FLOAT SRV from
    // the same resource.  D32_FLOAT alone cannot be used as an SRV format.
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = RESOLUTION;
    texDesc.Height           = RESOLUTION;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1;
    texDesc.Format           = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc       = { 1, 0 };
    texDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format             = DXGI_FORMAT_D32_FLOAT;
    clearVal.DepthStencil.Depth = 1.0f;
    clearVal.DepthStencil.Stencil = 0;

    auto hp = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,  // initial state = SRV-ready
        &clearVal, IID_PPV_ARGS(&m_Texture));
    if (FAILED(hr)) {
        LOG_ERROR("ShadowMap: failed to create depth texture (hr=0x%08X).",
                  static_cast<unsigned>(hr));
        return false;
    }
    m_Texture->SetName(L"ShadowMap_Depth");

    // ---- Private DSV heap (1 slot) ------------------------------------------
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DSVHeap));
    if (FAILED(hr)) {
        LOG_ERROR("ShadowMap: failed to create DSV heap.");
        return false;
    }

    m_DSVHandle = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags         = D3D12_DSV_FLAG_NONE;
    device->CreateDepthStencilView(m_Texture.Get(), &dsvDesc, m_DSVHandle);

    // ---- SRV in the engine's shared SRV heap --------------------------------
    auto alloc  = ctx.AllocateSRV();
    m_SRVHandle = alloc.gpu;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels       = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(m_Texture.Get(), &srvDesc, alloc.cpu);

    RebuildLightSpaceMatrix();

    LOG_INFO("ShadowMap: initialized (%ux%u, R32_TYPELESS).", RESOLUTION, RESOLUTION);
    return true;
}

// ---------------------------------------------------------------------------
void ShadowMap::Shutdown()
{
    m_Texture.Reset();
    m_DSVHeap.Reset();
    m_DSVHandle = {};
    m_SRVHandle = {};
}

// ---------------------------------------------------------------------------
void ShadowMap::BeginShadowPass(ID3D12GraphicsCommandList* cmdList)
{
    // Transition: PIXEL_SHADER_RESOURCE -> DEPTH_WRITE
    auto barrier = TransitionBarrier(m_Texture.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    cmdList->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT vp = GetViewport();
    D3D12_RECT     sc = GetScissor();
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sc);

    cmdList->ClearDepthStencilView(m_DSVHandle,
        D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Depth-only render: no RTV bound
    cmdList->OMSetRenderTargets(0, nullptr, FALSE, &m_DSVHandle);
}

// ---------------------------------------------------------------------------
void ShadowMap::EndShadowPass(ID3D12GraphicsCommandList* cmdList)
{
    // Transition: DEPTH_WRITE -> PIXEL_SHADER_RESOURCE
    auto barrier = TransitionBarrier(m_Texture.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);
}

// ---------------------------------------------------------------------------
D3D12_VIEWPORT ShadowMap::GetViewport() const
{
    return { 0.f, 0.f,
             static_cast<float>(RESOLUTION),
             static_cast<float>(RESOLUTION),
             0.f, 1.f };
}

D3D12_RECT ShadowMap::GetScissor() const
{
    return { 0, 0, static_cast<LONG>(RESOLUTION), static_cast<LONG>(RESOLUTION) };
}

// ---------------------------------------------------------------------------
void ShadowMap::SetLightDirection(const XMFLOAT3& dir)
{
    m_LightDir = dir;
    RebuildLightSpaceMatrix();
}

void ShadowMap::SetSceneBounds(float halfExtent, float depthRange)
{
    m_HalfExtent = halfExtent;
    m_DepthRange = depthRange;
    RebuildLightSpaceMatrix();
}

XMMATRIX ShadowMap::GetLightSpaceMatrix() const
{
    return XMLoadFloat4x4(&m_LightSpaceMat);
}

// ---------------------------------------------------------------------------
// RebuildLightSpaceMatrix
//
// Places a virtual "camera" at (lightDir * depthRange/2) looking toward the
// scene origin.  An orthographic projection large enough to cover the scene
// bounds is applied.
//
// Convention note: the engine uses row-vector math (mul(v, M)) and transposes
// matrices before GPU upload.  This function stores the non-transposed result;
// callers (MeshRenderer, ShadowPipeline) transpose when writing to the CB.
// ---------------------------------------------------------------------------
void ShadowMap::RebuildLightSpaceMatrix()
{
    XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&m_LightDir));

    // Eye is placed behind the scene in the direction light comes FROM.
    // Light travels along +lightDir; eye is pulled back along +lightDir
    // so the scene sits within the frustum.
    XMVECTOR eye    = XMVectorScale(lightDir, m_DepthRange * 0.5f);
    XMVECTOR target = XMVectorZero();   // scene centre = world origin

    // Choose an up vector orthogonal to the light direction
    XMVECTOR worldUp = XMVectorSet(0.f, 1.f, 0.f, 0.f);
    float    absDot  = std::abs(XMVectorGetX(XMVector3Dot(lightDir, worldUp)));
    XMVECTOR up      = (absDot > 0.99f)
                       ? XMVectorSet(0.f, 0.f, 1.f, 0.f)   // avoid gimbal lock
                       : worldUp;

    XMMATRIX lightView = XMMatrixLookAtLH(eye, target, up);
    XMMATRIX lightProj = XMMatrixOrthographicLH(
        m_HalfExtent * 2.f,
        m_HalfExtent * 2.f,
        0.1f,
        m_DepthRange);

    XMStoreFloat4x4(&m_LightSpaceMat, lightView * lightProj);
}

} // namespace VibeEngine
