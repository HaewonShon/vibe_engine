#include "Material.h"
#include "DX12Helpers.h"

namespace VibeEngine {

bool Material::Create(ID3D12Device* device)
{
    UINT size = AlignTo256(sizeof(MaterialCB));
    auto hp   = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = BufferDesc(size);

    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_Buffer));
    if (FAILED(hr)) return false;

    D3D12_RANGE range = { 0, 0 };
    m_Buffer->Map(0, &range, &m_Mapped);
    Upload();
    return true;
}

void Material::Upload()
{
    if (!m_Mapped || !m_Dirty) return;

    MaterialCB cb;
    cb.Albedo             = m_Albedo;
    cb.Roughness          = m_Roughness;
    cb.Metallic           = m_Metallic;
    cb.EmissiveIntensity  = m_EmissiveIntensity;
    cb.Pad0               = 0.f;
    cb.Emissive           = m_Emissive;
    cb.Pad1               = 0.f;
    memcpy(m_Mapped, &cb, sizeof(MaterialCB));
    m_Dirty = false;
}

D3D12_GPU_VIRTUAL_ADDRESS Material::GetGPUAddress() const
{
    return m_Buffer ? m_Buffer->GetGPUVirtualAddress() : 0;
}

} // namespace VibeEngine
