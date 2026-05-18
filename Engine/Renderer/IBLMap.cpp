#define NOMINMAX   // prevent Windows.h from defining min/max macros
#include "IBLMap.h"
#include "DX12Helpers.h"
#include <cmath>
#include <algorithm>
#include <cassert>

namespace VibeEngine {

// ===========================================================================
// Public interface
// ===========================================================================

bool IBLMap::Initialize(ID3D12Device*              device,
                        DX12Context&               ctx,
                        ID3D12GraphicsCommandList* cmdList)
{
    std::vector<uint8_t> buf;

    // ---- Irradiance cubemap -------------------------------------------------
    GenerateSkyIrradiance(buf);
    if (!UploadCubemap(device, cmdList, ctx, buf,
                       kIrrSize, 1,             // 1 mip level
                       DXGI_FORMAT_R8G8B8A8_UNORM,
                       m_IrrTex, m_IrrUpload, m_IrrSRV))
        return false;

    // ---- Specular cubemap (kSpecMips mip levels) ----------------------------
    GenerateSkySpecular(buf);
    if (!UploadCubemap(device, cmdList, ctx, buf,
                       kSpecSize, kSpecMips,
                       DXGI_FORMAT_R8G8B8A8_UNORM,
                       m_SpecTex, m_SpecUpload, m_SpecSRV))
        return false;

    // ---- BRDF LUT (2D) -------------------------------------------------------
    GenerateBRDFLUT(buf);
    if (!Upload2D(device, cmdList, ctx, buf,
                  kLUTSize, kLUTSize,
                  DXGI_FORMAT_R8G8B8A8_UNORM,   // R=scale G=bias (BA unused)
                  m_LUTTex, m_LUTUpload, m_LUTSRV))
        return false;

    m_Ready = true;
    return true;
}

void IBLMap::Shutdown()
{
    m_Ready = false;
    ReleaseUploadBuffers();
    m_LUTTex.Reset();
    m_SpecTex.Reset();
    m_IrrTex.Reset();
}

void IBLMap::ReleaseUploadBuffers()
{
    m_IrrUpload.Reset();
    m_SpecUpload.Reset();
    m_LUTUpload.Reset();
}

// ===========================================================================
// CPU data generators
// ===========================================================================

// ---------------------------------------------------------------------------
// TexelToDir — converts a cube-face texel to a normalised 3D direction.
//
// Face layout matches DX12 TextureCube convention:
//   0=+X  1=-X  2=+Y  3=-Y  4=+Z  5=-Z
// ---------------------------------------------------------------------------
void IBLMap::TexelToDir(int face, int x, int y, int size,
                         float& dx, float& dy, float& dz)
{
    float u = (float(x) + 0.5f) / float(size) * 2.0f - 1.0f; // [-1,+1]
    float v = (float(y) + 0.5f) / float(size) * 2.0f - 1.0f; // [-1,+1]
    switch (face) {
        case 0: dx =  1.f; dy = -v; dz = -u; break; // +X
        case 1: dx = -1.f; dy = -v; dz =  u; break; // -X
        case 2: dx =  u;   dy =  1.f; dz =  v; break; // +Y
        case 3: dx =  u;   dy = -1.f; dz = -v; break; // -Y
        case 4: dx =  u;   dy = -v; dz =  1.f; break; // +Z
        case 5: dx = -u;   dy = -v; dz = -1.f; break; // -Z
        default: dx = dy = dz = 0.f; return;
    }
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len > 0.f) { dx /= len; dy /= len; dz /= len; }
}

// ---------------------------------------------------------------------------
// SkyColor — gradient: +Y = sky blue, -Y = ground brown.
//   roughness: 0=sharp, 1=flat grey (for specular mip blending)
// ---------------------------------------------------------------------------
static void SkyColor(float dy, float roughness,
                     uint8_t& r, uint8_t& g, uint8_t& b)
{
    // t = 0 (ground), t = 1 (sky)
    float t = dy * 0.5f + 0.5f;

    // As roughness increases, blend toward the mean of sky+ground (~grey)
    t = t + (0.5f - t) * roughness;

    const float skyR = 0.38f, skyG = 0.58f, skyB = 0.88f;
    const float gndR = 0.20f, gndG = 0.15f, gndB = 0.10f;

    float cr = gndR + (skyR - gndR) * t;
    float cg = gndG + (skyG - gndG) * t;
    float cb = gndB + (skyB - gndB) * t;

    // Clamp + convert to U8
    r = static_cast<uint8_t>(std::min(cr, 1.f) * 255.f + 0.5f);
    g = static_cast<uint8_t>(std::min(cg, 1.f) * 255.f + 0.5f);
    b = static_cast<uint8_t>(std::min(cb, 1.f) * 255.f + 0.5f);
}

// ---------------------------------------------------------------------------
// GenerateSkyIrradiance — 6 faces × 16×16, 1 mip.
//   Layout: face0 | face1 | … | face5  (each face = S×S×4 bytes)
// ---------------------------------------------------------------------------
void IBLMap::GenerateSkyIrradiance(std::vector<uint8_t>& out)
{
    const UINT S    = kIrrSize;
    const UINT face = S * S * 4;
    out.assign(6 * face, 0xFF);

    for (int f = 0; f < 6; ++f) {
        uint8_t* dst = out.data() + f * face;
        for (UINT y = 0; y < S; ++y) {
            for (UINT x = 0; x < S; ++x) {
                float dx, dy, dz;
                TexelToDir(f, int(x), int(y), int(S), dx, dy, dz);
                uint8_t r, g, b;
                SkyColor(dy, 0.0f, r, g, b);
                UINT i = (y * S + x) * 4;
                dst[i+0] = r;
                dst[i+1] = g;
                dst[i+2] = b;
                dst[i+3] = 255;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// GenerateSkySpecular — 6 faces × kSpecMips mip levels.
//   Layout: [face0/mip0][face0/mip1]…[face1/mip0]… (face-major order)
//   At each mip level the roughness = mip / (kSpecMips-1) is used to
//   progressively blend toward a uniform grey (simulating GGX prefilter).
// ---------------------------------------------------------------------------
void IBLMap::GenerateSkySpecular(std::vector<uint8_t>& out)
{
    // Compute total size first
    size_t total = 0;
    for (UINT m = 0; m < kSpecMips; ++m) {
        UINT S = std::max(1u, kSpecSize >> m);
        total += size_t(6) * S * S * 4;
    }
    out.assign(total, 0xFF);

    uint8_t* dst = out.data();
    for (int f = 0; f < 6; ++f) {
        for (UINT m = 0; m < kSpecMips; ++m) {
            UINT S         = std::max(1u, kSpecSize >> m);
            float roughness = (kSpecMips > 1)
                             ? float(m) / float(kSpecMips - 1)
                             : 0.f;

            for (UINT y = 0; y < S; ++y) {
                for (UINT x = 0; x < S; ++x) {
                    float dx, dy, dz;
                    TexelToDir(f, int(x), int(y), int(S), dx, dy, dz);
                    uint8_t r, g, b;
                    SkyColor(dy, roughness, r, g, b);
                    *dst++ = r;
                    *dst++ = g;
                    *dst++ = b;
                    *dst++ = 255;
                }
            }
        }
    }
}

// ===========================================================================
// BRDF LUT generation (split-sum, GGX)
//
// For each texel (x, y):
//   NdotV     = (x + 0.5) / kLUTSize   [0→1 left→right]
//   roughness = (y + 0.5) / kLUTSize   [0→1 top→bottom]
//   (scale, bias) = IntegrateBRDF(NdotV, roughness, 1024)
//   output R = scale×255, G = bias×255
// ===========================================================================

static float RadicalInverseVdC(uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f; // / 2^32
}

static std::pair<float,float> Hammersley(uint32_t i, uint32_t N)
{
    return { float(i) / float(N), RadicalInverseVdC(i) };
}

// GGX importance sample — returns H in tangent space (N=Z)
static void ImportanceSampleGGX(float u, float v, float roughness,
                                 float& Hx, float& Hy, float& Hz)
{
    const float PI = 3.14159265f;
    float a   = roughness * roughness;
    float phi = 2.f * PI * u;

    float cosTheta, sinTheta;
    // Karis 2013 mapping: cos²θ = (1-v) / (1 + (a²-1)*v)
    float denom = 1.f + (a*a - 1.f) * v;
    cosTheta = (denom > 1e-6f) ? sqrtf((1.f - v) / denom) : 1.f;
    sinTheta = sqrtf(std::max(0.f, 1.f - cosTheta * cosTheta));

    Hx = cosf(phi) * sinTheta;
    Hy = sinf(phi) * sinTheta;
    Hz = cosTheta;
}

// Smith-Schlick G for IBL (k = roughness²/2)
static float G_SchlickIBL(float NdotX, float roughness)
{
    float a = roughness;
    float k = (a * a) * 0.5f;
    float d = NdotX * (1.f - k) + k;
    return (d > 1e-6f) ? NdotX / d : 0.f;
}

std::pair<float,float> IBLMap::IntegrateBRDF(float NdotV, float roughness,
                                              uint32_t numSamples)
{
    // View direction in tangent space (N=Z): V = (sinθ, 0, cosθ)
    float sinV = sqrtf(std::max(0.f, 1.f - NdotV * NdotV));
    float Vx = sinV, Vy = 0.f, Vz = NdotV;

    float scale = 0.f, bias = 0.f;
    float alpha = roughness;

    for (uint32_t i = 0; i < numSamples; ++i) {
        auto [u, v] = Hammersley(i, numSamples);
        float Hx, Hy, Hz;
        ImportanceSampleGGX(u, v, alpha, Hx, Hy, Hz);

        // Reflect V about H to get L
        float VdotH = Vx*Hx + Vy*Hy + Vz*Hz;
        float Lx = 2.f*VdotH*Hx - Vx;
        float Ly = 2.f*VdotH*Hy - Vy;
        float Lz = 2.f*VdotH*Hz - Vz;

        float NdotL = std::max(0.f, Lz);   // N = (0,0,1) in tangent space
        float NdotH = std::max(0.f, Hz);
        float cVdotH = std::max(0.f, VdotH);

        if (NdotL > 0.f && NdotV > 1e-4f) {
            float G     = G_SchlickIBL(NdotV, roughness)
                        * G_SchlickIBL(NdotL, roughness);
            float G_Vis = (NdotH > 1e-6f)
                        ? G * cVdotH / (NdotH * NdotV)
                        : 0.f;
            float Fc    = powf(1.f - cVdotH, 5.f);
            scale += (1.f - Fc) * G_Vis;
            bias  +=  Fc        * G_Vis;
        }
    }
    scale /= float(numSamples);
    bias  /= float(numSamples);
    return { scale, bias };
}

void IBLMap::GenerateBRDFLUT(std::vector<uint8_t>& out)
{
    const UINT S = kLUTSize;
    out.assign(S * S * 4, 0);

    for (UINT y = 0; y < S; ++y) {
        float roughness = (float(y) + 0.5f) / float(S);
        // Clamp roughness to avoid degenerate singularity at 0
        roughness = std::max(roughness, 0.02f);

        for (UINT x = 0; x < S; ++x) {
            float NdotV = (float(x) + 0.5f) / float(S);
            NdotV = std::max(NdotV, 0.01f);

            auto [scl, bia] = IntegrateBRDF(NdotV, roughness, 512);

            UINT i = (y * S + x) * 4;
            out[i+0] = static_cast<uint8_t>(
                std::min(std::max(scl, 0.f), 1.f) * 255.f + 0.5f);
            out[i+1] = static_cast<uint8_t>(
                std::min(std::max(bia, 0.f), 1.f) * 255.f + 0.5f);
            out[i+2] = 0;
            out[i+3] = 255;
        }
    }
}

// ===========================================================================
// GPU upload helpers
// ===========================================================================

bool IBLMap::UploadCubemap(ID3D12Device*              device,
                            ID3D12GraphicsCommandList* cmdList,
                            DX12Context&               ctx,
                            const std::vector<uint8_t>& data,
                            UINT faceSize0, UINT numMips,
                            DXGI_FORMAT fmt,
                            ComPtr<ID3D12Resource>& outTex,
                            ComPtr<ID3D12Resource>& outUpload,
                            D3D12_GPU_DESCRIPTOR_HANDLE& outSRV)
{
    const UINT totalSubresources = 6 * numMips;

    // ---- Create DEFAULT texture resource ------------------------------------
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width              = faceSize0;
    texDesc.Height             = faceSize0;
    texDesc.DepthOrArraySize   = 6;
    texDesc.MipLevels          = static_cast<UINT16>(numMips);
    texDesc.Format             = fmt;
    texDesc.SampleDesc         = { 1, 0 };
    texDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    auto defaultHP = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(
        &defaultHP, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&outTex));
    if (FAILED(hr)) return false;

    // ---- Query footprints for all subresources ------------------------------
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(totalSubresources);
    std::vector<UINT>   numRows(totalSubresources);
    std::vector<UINT64> rowBytes(totalSubresources);
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&texDesc, 0, totalSubresources, 0,
                                  footprints.data(), numRows.data(),
                                  rowBytes.data(), &totalBytes);

    // ---- Create UPLOAD buffer -----------------------------------------------
    auto uploadHP   = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = BufferDesc(totalBytes);
    hr = device->CreateCommittedResource(
        &uploadHP, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&outUpload));
    if (FAILED(hr)) return false;

    // ---- Map and fill upload buffer -----------------------------------------
    void* mappedVoid = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    outUpload->Map(0, &readRange, &mappedVoid);
    uint8_t* mapped = static_cast<uint8_t*>(mappedVoid);

    // Accumulate source byte offset as we step through faces and mip levels.
    // Layout in data[]: face0/mip0, face0/mip1, ..., face1/mip0, face1/mip1, ...
    size_t srcOffset = 0;

    for (int f = 0; f < 6; ++f) {
        for (UINT m = 0; m < numMips; ++m) {
            UINT subIdx     = static_cast<UINT>(f) * numMips + m;
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fprint = footprints[subIdx];
            UINT S          = std::max(1u, faceSize0 >> m);
            UINT tightPitch = S * 4u; // RGBA8: 4 bytes per pixel, tight

            for (UINT row = 0; row < numRows[subIdx]; ++row) {
                uint8_t*       pDst = mapped
                                    + fprint.Offset
                                    + static_cast<size_t>(row) * fprint.Footprint.RowPitch;
                const uint8_t* pSrc = data.data()
                                    + srcOffset
                                    + static_cast<size_t>(row) * tightPitch;
                memcpy(pDst, pSrc, tightPitch);
            }
            srcOffset += static_cast<size_t>(S) * S * 4u; // advance by mip-face size
        }
    }
    outUpload->Unmap(0, nullptr);

    // ---- Copy each subresource ----------------------------------------------
    for (UINT sub = 0; sub < totalSubresources; ++sub) {
        D3D12_TEXTURE_COPY_LOCATION dst_loc = {};
        dst_loc.pResource        = outTex.Get();
        dst_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_loc.SubresourceIndex = sub;

        D3D12_TEXTURE_COPY_LOCATION src_loc = {};
        src_loc.pResource       = outUpload.Get();
        src_loc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src_loc.PlacedFootprint = footprints[sub];

        cmdList->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);
    }

    // ---- Transition to SRV --------------------------------------------------
    auto barrier = TransitionBarrier(outTex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);

    // ---- Create TextureCube SRV ---------------------------------------------
    auto alloc = ctx.AllocateSRV();
    outSRV = alloc.gpu;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                        = fmt;
    srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MostDetailedMip   = 0;
    srvDesc.TextureCube.MipLevels         = numMips;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.f;
    device->CreateShaderResourceView(outTex.Get(), &srvDesc, alloc.cpu);

    return true;
}

bool IBLMap::Upload2D(ID3D12Device*              device,
                      ID3D12GraphicsCommandList* cmdList,
                      DX12Context&               ctx,
                      const std::vector<uint8_t>& data,
                      UINT width, UINT height,
                      DXGI_FORMAT fmt,
                      ComPtr<ID3D12Resource>& outTex,
                      ComPtr<ID3D12Resource>& outUpload,
                      D3D12_GPU_DESCRIPTOR_HANDLE& outSRV)
{
    // ---- Texture2D resource -------------------------------------------------
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = width;
    texDesc.Height           = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1;
    texDesc.Format           = fmt;
    texDesc.SampleDesc       = { 1, 0 };

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0,
                                  &footprint, nullptr, nullptr, &totalBytes);

    auto defaultHP = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(
        &defaultHP, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&outTex));
    if (FAILED(hr)) return false;

    auto uploadHP   = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = BufferDesc(totalBytes);
    hr = device->CreateCommittedResource(
        &uploadHP, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&outUpload));
    if (FAILED(hr)) return false;

    // ---- Fill upload buffer -------------------------------------------------
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    outUpload->Map(0, &readRange, reinterpret_cast<void**>(&mapped));

    UINT tightPitch = width * 4;
    for (UINT row = 0; row < height; ++row) {
        memcpy(mapped + UINT64(row) * footprint.Footprint.RowPitch,
               data.data() + UINT64(row) * tightPitch,
               tightPitch);
    }
    outUpload->Unmap(0, nullptr);

    // ---- Copy + barrier -----------------------------------------------------
    D3D12_TEXTURE_COPY_LOCATION dst_loc = {};
    dst_loc.pResource        = outTex.Get();
    dst_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src_loc = {};
    src_loc.pResource       = outUpload.Get();
    src_loc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint = footprint;

    cmdList->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);

    auto barrier = TransitionBarrier(outTex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);

    // ---- Create Texture2D SRV -----------------------------------------------
    auto alloc = ctx.AllocateSRV();
    outSRV = alloc.gpu;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = fmt;
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels       = 1;
    device->CreateShaderResourceView(outTex.Get(), &srvDesc, alloc.cpu);

    return true;
}

} // namespace VibeEngine
