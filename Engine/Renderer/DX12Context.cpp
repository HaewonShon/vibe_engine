#include "DX12Context.h"
#include "DX12Helpers.h"
#include <stdexcept>
#include <string>

#ifdef _DEBUG
#include <d3d12sdklayers.h>
#include <dxgidebug.h>
#endif

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace VibeEngine {

static void ThrowIfFailed(HRESULT hr, const char* msg = "DX12 call failed")
{
    if (FAILED(hr)) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s (HRESULT 0x%08X)", msg, static_cast<unsigned>(hr));
        throw std::runtime_error(buf);
    }
}

DX12Context::DX12Context() = default;

DX12Context::~DX12Context()
{
    Shutdown();
}

bool DX12Context::Initialize(HWND hwnd, UINT width, UINT height)
{
    m_Width  = width;
    m_Height = height;

    try {
        CreateDevice();
        CreateCommandQueue();
        CreateSwapChain(hwnd, width, height);
        CreateRTVHeap();
        CreateSRVHeap();
        CreateBackBuffers();
        CreateDepthBuffer();
        CreateCommandAllocators();
        CreateCommandList();
        CreateFence();
    } catch (const std::exception& e) {
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");
        return false;
    }
    return true;
}

void DX12Context::Shutdown()
{
    WaitForGPU();
    if (m_FenceEvent) {
        CloseHandle(m_FenceEvent);
        m_FenceEvent = nullptr;
    }
}

void DX12Context::CreateDevice()
{
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        debug->EnableDebugLayer();
#endif

    UINT factoryFlags = 0;
#ifdef _DEBUG
    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    ComPtr<IDXGIFactory6> factory;
    ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)),
                  "CreateDXGIFactory2");

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0;
         factory->EnumAdapterByGpuPreference(i,
             DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
             IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
         ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(),
                D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Device))))
            break;
    }
    if (!m_Device)
        ThrowIfFailed(E_FAIL, "No suitable DX12 adapter found");

#ifdef _DEBUG
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(m_Device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
    }
#endif
}

void DX12Context::CreateCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(m_Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_CommandQueue)),
                  "CreateCommandQueue");
}

void DX12Context::CreateSwapChain(HWND hwnd, UINT width, UINT height)
{
    UINT factoryFlags = 0;
#ifdef _DEBUG
    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    ComPtr<IDXGIFactory6> factory;
    ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)));

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width       = width;
    desc.Height      = height;
    desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc  = { 1, 0 };
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = FRAME_COUNT;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_CommandQueue.Get(), hwnd, &desc, nullptr, nullptr, &sc1),
        "CreateSwapChainForHwnd");

    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    sc1.As(&m_SwapChain);
    m_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
}

void DX12Context::CreateRTVHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = FRAME_COUNT;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_RTVHeap)),
                  "CreateDescriptorHeap RTV");
    m_RTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

void DX12Context::CreateBackBuffers()
{
    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        ThrowIfFailed(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_RenderTargets[i])));
        auto handle = OffsetHandle(
            m_RTVHeap->GetCPUDescriptorHandleForHeapStart(), i, m_RTVDescriptorSize);
        m_Device->CreateRenderTargetView(m_RenderTargets[i].Get(), nullptr, handle);
    }
}

void DX12Context::CreateDepthBuffer()
{
    // DSV descriptor heap (1 descriptor, non-shader-visible)
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_DSVHeap)),
                  "CreateDescriptorHeap DSV");

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width              = m_Width;
    depthDesc.Height             = m_Height;
    depthDesc.DepthOrArraySize   = 1;
    depthDesc.MipLevels          = 1;
    depthDesc.Format             = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc         = { 1, 0 };
    depthDesc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format               = DXGI_FORMAT_D32_FLOAT;
    clearVal.DepthStencil.Depth   = 1.0f;
    clearVal.DepthStencil.Stencil = 0;

    auto hp = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_Device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal,
        IID_PPV_ARGS(&m_DepthBuffer)),
        "CreateCommittedResource DepthBuffer");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_Device->CreateDepthStencilView(
        m_DepthBuffer.Get(), &dsvDesc,
        m_DSVHeap->GetCPUDescriptorHandleForHeapStart());
}

void DX12Context::CreateSRVHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = SRV_HEAP_CAPACITY;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_SRVHeap)),
                  "CreateDescriptorHeap SRV");
    m_SRVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

DX12Context::SRVAllocation DX12Context::AllocateSRV()
{
    UINT idx = m_SRVAllocCount++;
    SRVAllocation alloc;
    alloc.cpu     = OffsetHandle(m_SRVHeap->GetCPUDescriptorHandleForHeapStart(),
                                 static_cast<int>(idx), m_SRVDescriptorSize);
    alloc.gpu.ptr = m_SRVHeap->GetGPUDescriptorHandleForHeapStart().ptr
                  + static_cast<UINT64>(idx) * m_SRVDescriptorSize;
    return alloc;
}

void DX12Context::CreateCommandAllocators()
{
    for (UINT i = 0; i < FRAME_COUNT; ++i)
        ThrowIfFailed(m_Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_CommandAllocators[i])));
}

void DX12Context::CreateCommandList()
{
    ThrowIfFailed(m_Device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_CommandAllocators[0].Get(), nullptr,
        IID_PPV_ARGS(&m_CommandList)));
    m_CommandList->Close();
}

void DX12Context::CreateFence()
{
    ThrowIfFailed(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)));
    for (UINT i = 0; i < FRAME_COUNT; ++i) m_FenceValues[i] = 1;
    m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_FenceEvent) ThrowIfFailed(E_FAIL, "CreateEvent failed");
    WaitForGPU();
}

void DX12Context::BeginFrame()
{
    auto& alloc = m_CommandAllocators[m_BackBufferIndex];
    ThrowIfFailed(alloc->Reset());
    ThrowIfFailed(m_CommandList->Reset(alloc.Get(), nullptr));

    auto barrier = TransitionBarrier(m_RenderTargets[m_BackBufferIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_CommandList->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT vp = { 0.f, 0.f,
        static_cast<float>(m_Width), static_cast<float>(m_Height), 0.f, 1.f };
    D3D12_RECT     sc = { 0, 0, static_cast<LONG>(m_Width), static_cast<LONG>(m_Height) };
    m_CommandList->RSSetViewports(1, &vp);
    m_CommandList->RSSetScissorRects(1, &sc);

    ID3D12DescriptorHeap* heaps[] = { m_SRVHeap.Get() };
    m_CommandList->SetDescriptorHeaps(1, heaps);

    auto rtv = OffsetHandle(m_RTVHeap->GetCPUDescriptorHandleForHeapStart(),
                            m_BackBufferIndex, m_RTVDescriptorSize);
    auto dsv = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();
    m_CommandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    static constexpr float CLEAR_COLOR[] = { 0.05f, 0.05f, 0.05f, 1.0f }; // near black
    m_CommandList->ClearRenderTargetView(rtv, CLEAR_COLOR, 0, nullptr);
    m_CommandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void DX12Context::EndFrame()
{
    auto barrier = TransitionBarrier(m_RenderTargets[m_BackBufferIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_CommandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(m_CommandList->Close());

    ID3D12CommandList* lists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(1, lists);

    ThrowIfFailed(m_SwapChain->Present(1, 0)); // vsync ON
    MoveToNextFrame();
}

void DX12Context::WaitForGPU()
{
    if (!m_CommandQueue || !m_Fence || !m_FenceEvent) return;

    UINT64 signal = m_FenceValues[m_BackBufferIndex];
    m_CommandQueue->Signal(m_Fence.Get(), signal);
    m_Fence->SetEventOnCompletion(signal, m_FenceEvent);
    WaitForSingleObjectEx(m_FenceEvent, INFINITE, FALSE);
    m_FenceValues[m_BackBufferIndex]++;
}

void DX12Context::MoveToNextFrame()
{
    UINT64 currentFenceVal = m_FenceValues[m_BackBufferIndex];
    m_CommandQueue->Signal(m_Fence.Get(), currentFenceVal);

    m_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

    if (m_Fence->GetCompletedValue() < m_FenceValues[m_BackBufferIndex]) {
        m_Fence->SetEventOnCompletion(m_FenceValues[m_BackBufferIndex], m_FenceEvent);
        WaitForSingleObjectEx(m_FenceEvent, INFINITE, FALSE);
    }
    m_FenceValues[m_BackBufferIndex] = currentFenceVal + 1;
}

void DX12Context::Resize(UINT width, UINT height)
{
    if (width == 0 || height == 0) return;
    WaitForGPU();

    for (UINT i = 0; i < FRAME_COUNT; ++i)
        m_RenderTargets[i].Reset();
    m_DepthBuffer.Reset();

    m_Width  = width;
    m_Height = height;
    m_SwapChain->ResizeBuffers(FRAME_COUNT, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    m_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
    CreateBackBuffers();
    CreateDepthBuffer();
}

} // namespace VibeEngine
