#pragma once
#include <d3d12.h>

// Minimal replacements for d3dx12.h helpers (not in Windows SDK 10.0.26100+)

inline D3D12_CPU_DESCRIPTOR_HANDLE OffsetHandle(
    D3D12_CPU_DESCRIPTOR_HANDLE base, int index, UINT descriptorSize)
{
    base.ptr += static_cast<SIZE_T>(index) * descriptorSize;
    return base;
}

inline D3D12_RESOURCE_BARRIER TransitionBarrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource   = resource;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter  = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return b;
}

inline D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = type;
    return hp;
}

inline D3D12_RESOURCE_DESC BufferDesc(UINT64 size)
{
    D3D12_RESOURCE_DESC d = {};
    d.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width              = size;
    d.Height             = 1;
    d.DepthOrArraySize   = 1;
    d.MipLevels          = 1;
    d.SampleDesc.Count   = 1;
    d.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}

inline constexpr UINT AlignTo256(UINT size)
{
    return (size + 255u) & ~255u;
}
