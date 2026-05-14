#include "MeshRenderer.h"
#include "DX12Helpers.h"
#include "../Core/GameObject.h"
#include "../Core/Transform.h"
#include <stdexcept>

using namespace DirectX;

namespace VibeEngine {

struct PerObjectCB {
    XMFLOAT4X4 MVP;
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
    if (!m_Mesh || !m_Pipeline || !m_CmdList || !m_MappedCB) return;

    // Build MVP
    XMMATRIX world = XMMatrixIdentity();
    if (GetGameObject()) {
        if (auto* t = GetGameObject()->GetTransform())
            world = t->GetWorldMatrix();
    }
    XMMATRIX mvp = world * viewProj;

    PerObjectCB cb;
    XMStoreFloat4x4(&cb.MVP, XMMatrixTranspose(mvp)); // HLSL expects column-major
    memcpy(m_MappedCB, &cb, sizeof(PerObjectCB));

    m_CmdList->SetGraphicsRootSignature(m_Pipeline->GetRootSignature());
    m_CmdList->SetPipelineState(m_Pipeline->GetPSO());
    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_CmdList->IASetVertexBuffers(0, 1, &m_Mesh->GetVBView());
    m_CmdList->IASetIndexBuffer(&m_Mesh->GetIBView());
    m_CmdList->SetGraphicsRootConstantBufferView(0,
        m_ConstantBuffer->GetGPUVirtualAddress());
    if (m_Texture && m_Texture->IsLoaded())
        m_CmdList->SetGraphicsRootDescriptorTable(1, m_Texture->GetSRVHandle());
    m_CmdList->DrawIndexedInstanced(m_Mesh->GetIndexCount(), 1, 0, 0, 0);
}

} // namespace VibeEngine
