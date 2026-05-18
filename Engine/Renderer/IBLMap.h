#pragma once
#include "DX12Context.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

// ===========================================================================
// IBLMap — Image-Based Lighting resources for the BasicPipeline.
//
// Manages three GPU textures bound at root parameter slots [5], [6], [7]:
//   [5] t2  Irradiance  TextureCube  16×16/face, 1 mip  — diffuse IBL
//   [6] t3  Specular    TextureCube  128px mip0, 6 mips  — prefiltered specular
//   [7] t4  BRDF LUT    Texture2D    128×128              — split-sum scale/bias
//
// Default environment: procedural gradient sky (CPU-generated, no files needed).
//   Up  (+Y) = sky blue   {0.4, 0.6, 0.9}
//   Down(-Y) = ground     {0.2, 0.15, 0.1}
//   Specular mip levels blend from sharp sky (mip0) to uniform grey (mip5).
//
// Call Initialize() inside a BeginFrame / EndFrame pair so that the upload
// commands execute before the first draw.  Call ReleaseUploadBuffers() after
// the subsequent WaitForGPU() to free the upload heap memory.
//
// Usage (SandboxApp::OnInit):
//   m_IBLMap.Initialize(device, m_DX12, m_DX12.GetCommandList());
//   // ... EndFrame + WaitForGPU ...
//   m_IBLMap.ReleaseUploadBuffers();
// ===========================================================================
class IBLMap {
public:
    // ---- Lifecycle ----------------------------------------------------------
    bool Initialize(ID3D12Device*              device,
                    DX12Context&               ctx,
                    ID3D12GraphicsCommandList* cmdList);
    void Shutdown();
    void ReleaseUploadBuffers();

    // ---- GPU handles (bind to root table slots [5], [6], [7]) --------------
    D3D12_GPU_DESCRIPTOR_HANDLE GetIrradianceSRV() const { return m_IrrSRV; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSpecularSRV()   const { return m_SpecSRV; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetBRDFLUTSRV()    const { return m_LUTSRV;  }

    bool IsReady() const { return m_Ready; }

    // ---- Specular mip count (must match Basic.hlsl IBL_SPEC_MIPS) ----------
    static constexpr UINT kSpecMips = 6;

private:
    // ---- Texture dimensions -------------------------------------------------
    static constexpr UINT kIrrSize  = 16;    // irradiance face px
    static constexpr UINT kSpecSize = 128;   // specular mip-0 face px
    static constexpr UINT kLUTSize  = 128;   // BRDF LUT px

    // ---- GPU resources ------------------------------------------------------
    ComPtr<ID3D12Resource> m_IrrTex,  m_IrrUpload;
    ComPtr<ID3D12Resource> m_SpecTex, m_SpecUpload;
    ComPtr<ID3D12Resource> m_LUTTex,  m_LUTUpload;

    D3D12_GPU_DESCRIPTOR_HANDLE m_IrrSRV  {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_SpecSRV {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_LUTSRV  {};

    bool m_Ready = false;

    // ---- CPU data generators ------------------------------------------------
    // Each returns raw RGBA8 pixel data sized for 6 faces (× mip levels).
    static void GenerateSkyIrradiance(std::vector<uint8_t>& out);
    static void GenerateSkySpecular  (std::vector<uint8_t>& out);
    static void GenerateBRDFLUT      (std::vector<uint8_t>& out);

    // Helper: face-texel (face, x, y, size) → normalised 3D direction
    static void TexelToDir(int face, int x, int y, int size,
                           float& dx, float& dy, float& dz);

    // Helper: GGX BRDF integration for BRDF LUT (Van der Corput + Smith)
    static std::pair<float,float> IntegrateBRDF(float NdotV, float roughness,
                                                uint32_t numSamples);

    // ---- GPU upload helpers -------------------------------------------------
    // UploadCubemap: creates a DEFAULT TextureCube + UPLOAD staging buffer,
    // records copy + barrier commands into cmdList, allocates one SRV.
    //   data  : pixel bytes, layout: face0/mip0, face0/mip1,…, face1/mip0, …
    //   faceSize0 : px width/height of mip-0 face
    bool UploadCubemap(ID3D12Device*              device,
                       ID3D12GraphicsCommandList* cmdList,
                       DX12Context&               ctx,
                       const std::vector<uint8_t>& data,
                       UINT faceSize0, UINT numMips,
                       DXGI_FORMAT fmt,
                       ComPtr<ID3D12Resource>& outTex,
                       ComPtr<ID3D12Resource>& outUpload,
                       D3D12_GPU_DESCRIPTOR_HANDLE& outSRV);

    // Upload2D: same but for a plain Texture2D (BRDF LUT).
    bool Upload2D(ID3D12Device*              device,
                  ID3D12GraphicsCommandList* cmdList,
                  DX12Context&               ctx,
                  const std::vector<uint8_t>& data,
                  UINT width, UINT height,
                  DXGI_FORMAT fmt,
                  ComPtr<ID3D12Resource>& outTex,
                  ComPtr<ID3D12Resource>& outUpload,
                  D3D12_GPU_DESCRIPTOR_HANDLE& outSRV);
};

} // namespace VibeEngine
