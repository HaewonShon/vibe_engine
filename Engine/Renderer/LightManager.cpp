#include "LightManager.h"
#include "DX12Helpers.h"

namespace VibeEngine {

bool LightManager::Initialize(ID3D12Device* device)
{
    auto hp   = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = BufferDesc(AlignTo256(sizeof(LightCB)));
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

void LightManager::Upload()
{
    if (!m_Mapped || !m_Dirty) return;
    LightCB cb;
    cb.Direction  = m_Direction;
    cb.Intensity  = m_Intensity;
    cb.Color      = m_Color;
    cb.Pad0       = 0.f;
    cb.Ambient    = m_Ambient;
    cb.Pad1       = 0.f;
    cb.CameraPos  = m_CameraPos;
    cb.Pad2       = 0.f;
    cb.ScreenSize = m_ScreenSize;
    cb.ScreenPad  = { 0.f, 0.f };
    memcpy(m_Mapped, &cb, sizeof(LightCB));
    m_Dirty = false;
}

D3D12_GPU_VIRTUAL_ADDRESS LightManager::GetGPUAddress() const
{
    return m_Buffer ? m_Buffer->GetGPUVirtualAddress() : 0;
}

} // namespace VibeEngine
