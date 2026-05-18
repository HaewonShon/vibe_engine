#include "ImGuiLayer.h"
#include "../Renderer/DX12Context.h"
#include "../Core/Log.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

namespace VibeEngine {

// ---------------------------------------------------------------------------
ImGuiLayer::~ImGuiLayer()
{
    if (m_Initialized) Shutdown();
}

// ---------------------------------------------------------------------------
// ImGui_ImplDX12_Init() in 1.92.x uses ImGui_ImplDX12_InitInfo and requires:
//   - CommandQueue  (for internal texture upload via ExecuteCommandLists)
//   - SrvDescriptorAllocFn / SrvDescriptorFreeFn callbacks
//     (the backend now manages multiple textures via dynamic allocation)
//
// DX12Context pointer is passed through InitInfo::UserData so the callbacks
// can reach AllocateSRV() without a captured lambda (C function pointer required).
// ---------------------------------------------------------------------------
void ImGuiLayer::Init(DX12Context* ctx, HWND hwnd)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Dark theme tuned for the engine's dark-grey (0.05, 0.05, 0.05) background.
    ImGui::StyleColorsDark();
    ImGuiStyle& style       = ImGui::GetStyle();
    style.WindowRounding    = 4.f;
    style.FrameRounding     = 3.f;
    style.GrabRounding      = 3.f;
    style.WindowBorderSize  = 1.f;
    style.Colors[ImGuiCol_WindowBg]      = ImVec4(0.10f, 0.10f, 0.13f, 0.92f);
    style.Colors[ImGuiCol_Header]        = ImVec4(0.20f, 0.30f, 0.50f, 0.85f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.42f, 0.68f, 0.90f);
    style.Colors[ImGuiCol_HeaderActive]  = ImVec4(0.35f, 0.52f, 0.80f, 1.00f);

    // Cache the SRV heap pointer so End() can rebind it after UIRenderer changes.
    m_SRVHeap = ctx->GetSRVHeap();

    ImGui_ImplWin32_Init(hwnd);

    // ---- New 1.92.x struct-based DX12 init ----------------------------------
    ImGui_ImplDX12_InitInfo initInfo;
    initInfo.Device            = ctx->GetDevice();
    initInfo.CommandQueue      = ctx->GetCommandQueue(); // needed for texture uploads
    initInfo.NumFramesInFlight = static_cast<int>(FRAME_COUNT);
    initInfo.RTVFormat         = ctx->GetBackBufferFormat();
    // DSVFormat: ImGui does not write depth, leave as DXGI_FORMAT_UNKNOWN (0)
    initInfo.SrvDescriptorHeap = ctx->GetSRVHeap();
    initInfo.UserData          = ctx;

    // Alloc callback: delegate to DX12Context::AllocateSRV()
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                                       D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
    {
        auto* c = static_cast<DX12Context*>(info->UserData);
        auto alloc = c->AllocateSRV();
        *outCpu = alloc.cpu;
        *outGpu = alloc.gpu;
    };

    // Free callback: no-op (the SRV heap is a bump allocator without reclaim)
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*,
                                      D3D12_CPU_DESCRIPTOR_HANDLE,
                                      D3D12_GPU_DESCRIPTOR_HANDLE)
    {
        // Intentionally empty — SRV slots are not reclaimed.
    };

    ImGui_ImplDX12_Init(&initInfo);

    m_Initialized = true;
    LOG_INFO("ImGuiLayer: initialized (imgui 1.92, DX12 + Win32 backends).");
}

// ---------------------------------------------------------------------------
void ImGuiLayer::Shutdown()
{
    if (!m_Initialized) return;
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_SRVHeap     = nullptr;
    m_Initialized = false;
    LOG_INFO("ImGuiLayer: shut down.");
}

// ---------------------------------------------------------------------------
void ImGuiLayer::Begin()
{
    // ImGui_ImplDX12_NewFrame() handles font atlas upload internally via the
    // CommandQueue provided in InitInfo (no explicit CreateFontsTexture needed).
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

// ---------------------------------------------------------------------------
void ImGuiLayer::End(ID3D12GraphicsCommandList* cmdList)
{
    ImGui::Render();

    // The UIRenderer may have re-bound its own descriptor heap.
    // Restore the engine SRV heap so ImGui can resolve its font/texture descriptors.
    if (m_SRVHeap) {
        ID3D12DescriptorHeap* heaps[] = { m_SRVHeap };
        cmdList->SetDescriptorHeaps(1, heaps);
    }

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}

// ---------------------------------------------------------------------------
bool ImGuiLayer::HandleWindowMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    extern IMGUI_IMPL_API LRESULT
        ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp) != 0;
}

} // namespace VibeEngine
