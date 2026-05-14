#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

static constexpr UINT FRAME_COUNT = 2;

class DX12Context {
public:
    DX12Context();
    ~DX12Context();

    bool Initialize(HWND hwnd, UINT width, UINT height);
    void Shutdown();

    void BeginFrame();
    void EndFrame();

    void Resize(UINT width, UINT height);

    ID3D12Device*              GetDevice()       const { return m_Device.Get(); }
    ID3D12GraphicsCommandList* GetCommandList()  const { return m_CommandList.Get(); }
    ID3D12CommandQueue*        GetCommandQueue() const { return m_CommandQueue.Get(); }
    UINT                       GetBackBufferIndex() const { return m_BackBufferIndex; }
    DXGI_FORMAT                GetBackBufferFormat() const { return DXGI_FORMAT_R8G8B8A8_UNORM; }

    void WaitForGPU();

private:
    void CreateDevice();
    void CreateCommandQueue();
    void CreateSwapChain(HWND hwnd, UINT width, UINT height);
    void CreateRTVHeap();
    void CreateBackBuffers();
    void CreateDepthBuffer();
    void CreateCommandAllocators();
    void CreateCommandList();
    void CreateFence();

    void MoveToNextFrame();

    ComPtr<ID3D12Device>              m_Device;
    ComPtr<ID3D12CommandQueue>        m_CommandQueue;
    ComPtr<IDXGISwapChain3>           m_SwapChain;
    ComPtr<ID3D12DescriptorHeap>      m_RTVHeap;
    ComPtr<ID3D12Resource>            m_RenderTargets[FRAME_COUNT];
    ComPtr<ID3D12CommandAllocator>    m_CommandAllocators[FRAME_COUNT];
    ComPtr<ID3D12GraphicsCommandList> m_CommandList;

    ComPtr<ID3D12Resource>        m_DepthBuffer;
    ComPtr<ID3D12DescriptorHeap>  m_DSVHeap;

    ComPtr<ID3D12Fence>  m_Fence;
    UINT64               m_FenceValues[FRAME_COUNT] = {};
    HANDLE               m_FenceEvent = nullptr;

    UINT m_RTVDescriptorSize = 0;
    UINT m_BackBufferIndex   = 0;
    UINT m_Width  = 0;
    UINT m_Height = 0;
};

} // namespace VibeEngine
