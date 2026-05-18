#include "GPUProfiler.h"
#include "../Core/Log.h"
#include <algorithm>

namespace VibeEngine {

// ---------------------------------------------------------------------------
bool GPUProfiler::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue)
{
    // ---- Timestamp frequency (GPU ticks per second) ----------------------
    if (FAILED(queue->GetTimestampFrequency(&m_TimestampFrequency)) ||
        m_TimestampFrequency == 0)
    {
        LOG_WARN("GPUProfiler: GetTimestampFrequency failed — GPU timing disabled.");
        return false;
    }

    // ---- Query heap ------------------------------------------------------
    D3D12_QUERY_HEAP_DESC qhDesc = {};
    qhDesc.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhDesc.Count = QUERY_COUNT;
    if (FAILED(device->CreateQueryHeap(&qhDesc, IID_PPV_ARGS(&m_QueryHeap)))) {
        LOG_WARN("GPUProfiler: CreateQueryHeap failed.");
        return false;
    }

    // ---- Readback buffers (one per swap-chain frame) --------------------
    // Each buffer holds QUERY_COUNT UINT64 values (one per timestamp slot).
    // Readback heaps are always in COPY_DEST state and cannot transition.
    D3D12_HEAP_PROPERTIES rbHeap = {};
    rbHeap.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC rbDesc = {};
    rbDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    rbDesc.Width              = QUERY_COUNT * sizeof(UINT64);
    rbDesc.Height             = 1;
    rbDesc.DepthOrArraySize   = 1;
    rbDesc.MipLevels          = 1;
    rbDesc.SampleDesc         = { 1, 0 };
    rbDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        if (FAILED(device->CreateCommittedResource(
                &rbHeap, D3D12_HEAP_FLAG_NONE, &rbDesc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&m_ReadbackBuffers[i]))))
        {
            LOG_WARN("GPUProfiler: readback buffer[%u] creation failed.", i);
            return false;
        }
    }

    m_Initialized = true;
    LOG_INFO("GPUProfiler: initialized  freq=%.0f MHz  sections=%u.",
             m_TimestampFrequency / 1e6, SECTION_COUNT);
    return true;
}

// ---------------------------------------------------------------------------
void GPUProfiler::Shutdown()
{
    for (auto& rb : m_ReadbackBuffers) rb.Reset();
    m_QueryHeap.Reset();
    m_Initialized     = false;
    m_ValidFrameCount = 0;
}

// ---------------------------------------------------------------------------
// ReadResults — called after the fence for frameIndex has been waited.
// The readback buffer for that index contains timestamps written FRAME_COUNT
// frames ago and is safe to map from the CPU.
// ---------------------------------------------------------------------------
void GPUProfiler::ReadResults(UINT frameIndex)
{
    if (!m_Initialized) return;

    // Guard: skip the very first FRAME_COUNT frames before any data is valid.
    if (m_ValidFrameCount < FRAME_COUNT) { ++m_ValidFrameCount; return; }

    UINT64* data = nullptr;
    D3D12_RANGE readRange = { 0, QUERY_COUNT * sizeof(UINT64) };
    if (FAILED(m_ReadbackBuffers[frameIndex]->Map(0, &readRange,
            reinterpret_cast<void**>(&data))))
    {
        LOG_WARN("GPUProfiler: readback map failed for frame %u.", frameIndex);
        return;
    }

    for (UINT s = 0; s < SECTION_COUNT; ++s) {
        const UINT64 t0 = data[s * 2];
        const UINT64 t1 = data[s * 2 + 1];
        m_Results[s] = (t1 > t0)
            ? static_cast<float>((t1 - t0) * 1000.0 / m_TimestampFrequency)
            : 0.f;
    }

    D3D12_RANGE writeRange = { 0, 0 };    // CPU never writes
    m_ReadbackBuffers[frameIndex]->Unmap(0, &writeRange);
}

// ---------------------------------------------------------------------------
void GPUProfiler::BeginSection(ID3D12GraphicsCommandList* cmdList, Section section)
{
    if (!m_Initialized) return;
    cmdList->EndQuery(m_QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                      static_cast<UINT>(section) * 2);
}

// ---------------------------------------------------------------------------
void GPUProfiler::EndSection(ID3D12GraphicsCommandList* cmdList, Section section)
{
    if (!m_Initialized) return;
    cmdList->EndQuery(m_QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                      static_cast<UINT>(section) * 2 + 1);
}

// ---------------------------------------------------------------------------
// Resolve — copies all timestamp results from the query heap into the
// readback buffer for the current frame index.  Must be called while the
// command list is still open (before ExecuteCommandLists in EndFrame).
// ---------------------------------------------------------------------------
void GPUProfiler::Resolve(ID3D12GraphicsCommandList* cmdList, UINT frameIndex)
{
    if (!m_Initialized) return;
    cmdList->ResolveQueryData(
        m_QueryHeap.Get(),
        D3D12_QUERY_TYPE_TIMESTAMP,
        0, QUERY_COUNT,
        m_ReadbackBuffers[frameIndex].Get(),
        0);
}

// ---------------------------------------------------------------------------
float GPUProfiler::GetMs(Section section) const
{
    return m_Results[static_cast<UINT>(section)];
}

// ---------------------------------------------------------------------------
const char* GPUProfiler::GetSectionName(Section section)
{
    switch (section) {
        case Section::Scene: return "Scene";
        case Section::UI:    return "UI";
        case Section::ImGui: return "ImGui";
        default:             return "Unknown";
    }
}

} // namespace VibeEngine
