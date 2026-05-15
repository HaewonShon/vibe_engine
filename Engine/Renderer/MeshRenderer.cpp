#include "MeshRenderer.h"
#include "DX12Helpers.h"
#include "LightManager.h"
#include "../Core/GameObject.h"
#include "../Core/Transform.h"
#include <stdexcept>

using namespace DirectX;

namespace VibeEngine {

struct PerObjectCB {
    XMFLOAT4X4 MVP;
    XMFLOAT4X4 World;
};

MeshRenderer::MeshRenderer()  = default;
MeshRenderer::~MeshRenderer() = default;

bool MeshRenderer::CreateConstantBuffer(ID3D12Device* device)
{
    UINT cbSize = AlignTo256(sizeof(PerObjectCB));
    auto hp     = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto desc   = BufferDesc(cbSize);

    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_ConstantBuffer));
    if (FAILED(hr)) return false;

    D3D12_RANGE range = { 0, 0 };
    m_ConstantBuffer->Map(0, &range, &m_MappedCB);
    return true;
}

void MeshRenderer::Draw(const XMMATRIX& viewProj) const
{
    if (!m_Mesh || !m_Material || !m_CmdList || !m_MappedCB) return;

    BasicPipeline* pipeline = m_Material->GetPipeline();
    if (!pipeline) return;

    // Build MVP from transform hierarchy
    XMMATRIX world = XMMatrixIdentity();
    if (GetGameObject()) {
        if (auto* t = GetGameObject()->GetTransform())
            world = t->GetWorldMatrix();
    }
    XMMATRIX mvp = world * viewProj;

    PerObjectCB cb;
    XMStoreFloat4x4(&cb.MVP,   XMMatrixTranspose(mvp));
    XMStoreFloat4x4(&cb.World, XMMatrixTranspose(world));
    memcpy(m_MappedCB, &cb, sizeof(PerObjectCB));

    LightManager::Get().Upload();
    m_Material->Upload();

    m_CmdList->SetGraphicsRootSignature(pipeline->GetRootSignature());
    m_CmdList->SetPipelineState(pipeline->GetPSO());
    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_CmdList->IASetVertexBuffers(0, 1, &m_Mesh->GetVBView());
    m_CmdList->IASetIndexBuffer(&m_Mesh->GetIBView());

    // [0] per-object CB (MVP + World)
    m_CmdList->SetGraphicsRootConstantBufferView(0, m_ConstantBuffer->GetGPUVirtualAddress());
    // [1] light CB
    m_CmdList->SetGraphicsRootConstantBufferView(1, LightManager::Get().GetGPUAddress());
    // [2] material CB (albedo, roughness, metallic, emissive)
    m_CmdList->SetGraphicsRootConstantBufferView(2, m_Material->GetGPUAddress());
    // [3] texture SRV
    Texture* tex = m_Material->GetTexture();
    if (tex && tex->IsLoaded())
        m_CmdList->SetGraphicsRootDescriptorTable(3, tex->GetSRVHandle());

    m_CmdList->DrawIndexedInstanced(m_Mesh->GetIndexCount(), 1, 0, 0, 0);
}

} // namespace VibeEngine
