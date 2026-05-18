#include "MeshRenderer.h"
#include "DX12Helpers.h"
#include "LightManager.h"
#include "../Core/GameObject.h"
#include "../Core/Transform.h"
#include <stdexcept>

using namespace DirectX;

namespace VibeEngine {

// ---------------------------------------------------------------------------
// PerObjectCB layout must exactly match Basic.hlsl cbuffer PerObjectCB b0.
// ---------------------------------------------------------------------------
struct PerObjectCB {
    XMFLOAT4X4 MVP;       //  0..63  bytes
    XMFLOAT4X4 World;     // 64..127 bytes
    XMFLOAT4X4 LightMVP;  // 128..191 bytes  (world x lightView x lightOrtho)
};

// ---------------------------------------------------------------------------
// ShadowPerObjectCB layout must match Shadow.hlsl cbuffer ShadowPerObjectCB b0.
// ---------------------------------------------------------------------------
struct ShadowPerObjectCB {
    XMFLOAT4X4 LightMVP;  // 0..63 bytes
};

MeshRenderer::MeshRenderer()  = default;
MeshRenderer::~MeshRenderer() = default;

// ---------------------------------------------------------------------------
bool MeshRenderer::CreateConstantBuffer(ID3D12Device* device)
{
    // ---- Main pass CB --------------------------------------------------------
    {
        UINT cbSize = AlignTo256(sizeof(PerObjectCB));
        auto hp     = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
        auto desc   = BufferDesc(cbSize);
        HRESULT hr  = device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_ConstantBuffer));
        if (FAILED(hr)) return false;

        D3D12_RANGE range = { 0, 0 };
        m_ConstantBuffer->Map(0, &range, &m_MappedCB);
    }

    // ---- Shadow pass CB ------------------------------------------------------
    {
        UINT cbSize = AlignTo256(sizeof(ShadowPerObjectCB));
        auto hp     = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
        auto desc   = BufferDesc(cbSize);
        HRESULT hr  = device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_ShadowConstantBuffer));
        if (FAILED(hr)) return false;

        D3D12_RANGE range = { 0, 0 };
        m_ShadowConstantBuffer->Map(0, &range, &m_ShadowMappedCB);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Draw — main render pass.
//
// Root parameter binding (must match BasicPipeline::CreateRootSignature):
//   [0] CBV b0  — PerObjectCB { MVP, World, LightMVP }
//   [1] CBV b1  — LightCB
//   [2] CBV b2  — MaterialCB
//   [3] Table   — t0  albedo texture SRV
//   [4] Table   — t1  shadow map SRV  (bound only if m_ShadowMap != nullptr)
// ---------------------------------------------------------------------------
void MeshRenderer::Draw(const XMMATRIX& viewProj) const
{
    if (!m_Mesh || !m_Material || !m_CmdList || !m_MappedCB) return;

    BasicPipeline* pipeline = m_Material->GetPipeline();
    if (!pipeline) return;

    // ---- World matrix --------------------------------------------------------
    XMMATRIX world = XMMatrixIdentity();
    if (GetGameObject())
        if (auto* t = GetGameObject()->GetTransform())
            world = t->GetWorldMatrix();

    XMMATRIX mvp = world * viewProj;

    // ---- Light MVP (world-space → light clip-space) -------------------------
    XMMATRIX lightMVP = XMMatrixIdentity();
    if (m_ShadowMap)
        lightMVP = world * m_ShadowMap->GetLightSpaceMatrix();

    // ---- Upload per-object CB -----------------------------------------------
    PerObjectCB cb;
    XMStoreFloat4x4(&cb.MVP,      XMMatrixTranspose(mvp));
    XMStoreFloat4x4(&cb.World,    XMMatrixTranspose(world));
    XMStoreFloat4x4(&cb.LightMVP, XMMatrixTranspose(lightMVP));
    memcpy(m_MappedCB, &cb, sizeof(PerObjectCB));

    LightManager::Get().Upload();
    m_Material->Upload();

    // ---- Bind pipeline -------------------------------------------------------
    m_CmdList->SetGraphicsRootSignature(pipeline->GetRootSignature());
    m_CmdList->SetPipelineState(pipeline->GetPSO());
    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_CmdList->IASetVertexBuffers(0, 1, &m_Mesh->GetVBView());
    m_CmdList->IASetIndexBuffer(&m_Mesh->GetIBView());

    // [0] per-object CB (MVP + World + LightMVP)
    m_CmdList->SetGraphicsRootConstantBufferView(
        0, m_ConstantBuffer->GetGPUVirtualAddress());
    // [1] light CB
    m_CmdList->SetGraphicsRootConstantBufferView(
        1, LightManager::Get().GetGPUAddress());
    // [2] material CB
    m_CmdList->SetGraphicsRootConstantBufferView(
        2, m_Material->GetGPUAddress());
    // [3] albedo texture SRV
    Texture* tex = m_Material->GetTexture();
    if (tex && tex->IsLoaded())
        m_CmdList->SetGraphicsRootDescriptorTable(3, tex->GetSRVHandle());
    // [4] shadow map SRV
    if (m_ShadowMap)
        m_CmdList->SetGraphicsRootDescriptorTable(4, m_ShadowMap->GetSRV());

    // [5] IBL irradiance  [6] IBL specular  [7] BRDF LUT
    if (m_IBLMap && m_IBLMap->IsReady()) {
        m_CmdList->SetGraphicsRootDescriptorTable(5, m_IBLMap->GetIrradianceSRV());
        m_CmdList->SetGraphicsRootDescriptorTable(6, m_IBLMap->GetSpecularSRV());
        m_CmdList->SetGraphicsRootDescriptorTable(7, m_IBLMap->GetBRDFLUTSRV());
    }

    // [8] Normal map — use material's normal map if set, otherwise flat fallback
    {
        Texture* normalTex = m_Material->GetNormalMap();
        if (!normalTex) normalTex = m_FlatNormal;
        if (normalTex && normalTex->IsLoaded())
            m_CmdList->SetGraphicsRootDescriptorTable(8, normalTex->GetSRVHandle());
    }

    // [9] SSAO ambient-occlusion map (or 1×1 white fallback on first frame)
    if (m_AOPass)
        m_CmdList->SetGraphicsRootDescriptorTable(9, m_AOPass->GetAOSRV());

    m_CmdList->DrawIndexedInstanced(m_Mesh->GetIndexCount(), 1, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// DrawShadow — depth-only shadow pass.
//
// PRECONDITION: ShadowPipeline::Bind(cmdList) has already been called this
// frame so the correct root signature and PSO are active.
//
// Root parameter binding (must match ShadowPipeline::CreateRootSignature):
//   [0] Root CBV b0 — ShadowPerObjectCB { LightMVP }
// ---------------------------------------------------------------------------
void MeshRenderer::DrawShadow(ID3D12GraphicsCommandList* cmdList,
                               const XMMATRIX&            lightViewProj) const
{
    if (!m_Mesh || !m_ShadowMappedCB) return;

    // ---- World matrix --------------------------------------------------------
    XMMATRIX world = XMMatrixIdentity();
    if (GetGameObject())
        if (auto* t = GetGameObject()->GetTransform())
            world = t->GetWorldMatrix();

    XMMATRIX lightMVP = world * lightViewProj;

    ShadowPerObjectCB cb;
    XMStoreFloat4x4(&cb.LightMVP, XMMatrixTranspose(lightMVP));
    memcpy(m_ShadowMappedCB, &cb, sizeof(ShadowPerObjectCB));

    cmdList->IASetVertexBuffers(0, 1, &m_Mesh->GetVBView());
    cmdList->IASetIndexBuffer(&m_Mesh->GetIBView());
    cmdList->SetGraphicsRootConstantBufferView(
        0, m_ShadowConstantBuffer->GetGPUVirtualAddress());

    cmdList->DrawIndexedInstanced(m_Mesh->GetIndexCount(), 1, 0, 0, 0);
}

} // namespace VibeEngine
