#include "BasicPipeline.h"
#include "Shader.h"
#include "DX12Helpers.h"
#include "../Core/Log.h"
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static std::wstring GetExeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    return path.substr(0, path.find_last_of(L"\\/") + 1);
}

namespace VibeEngine {

bool BasicPipeline::Create(ID3D12Device* device, DXGI_FORMAT rtvFormat)
{
    m_Device    = device;
    m_RTVFormat = rtvFormat;

    if (!CreateRootSignature(device)) return false;
    if (!CreatePSO(device, rtvFormat)) return false;

    // Start the file watcher on the Shaders directory next to the exe.
    // Failure (e.g. directory doesn't exist yet) is non-fatal — hot reload
    // will simply never trigger.
    std::wstring shaderDir = GetExeDir() + L"Shaders";
    m_Watcher.Watch(shaderDir);
    LOG_INFO("BasicPipeline: shader watcher started (%s).",
             std::string(shaderDir.begin(), shaderDir.end()).c_str());

    return true;
}

void BasicPipeline::Destroy()
{
    m_Watcher.Stop();
    m_PSO.Reset();
    m_RootSignature.Reset();
}

bool BasicPipeline::CreateRootSignature(ID3D12Device* device)
{
    // [0] Root CBV b0 — per-object { MVP, World, LightMVP } (vertex shader)
    D3D12_ROOT_PARAMETER params[5] = {};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace  = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    // [1] Root CBV b1 — lighting (pixel shader)
    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace  = 0;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

    // [2] Root CBV b2 — material params (pixel shader)
    params[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].Descriptor.ShaderRegister = 2;
    params[2].Descriptor.RegisterSpace  = 0;
    params[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

    // [3] Descriptor table: 1 SRV at t0 — albedo texture (pixel shader)
    D3D12_DESCRIPTOR_RANGE srvRange0 = {};
    srvRange0.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange0.NumDescriptors                    = 1;
    srvRange0.BaseShaderRegister                = 0;
    srvRange0.RegisterSpace                     = 0;
    srvRange0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges   = &srvRange0;
    params[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // [4] Descriptor table: 1 SRV at t1 — shadow map depth (pixel shader)
    D3D12_DESCRIPTOR_RANGE srvRange1 = {};
    srvRange1.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange1.NumDescriptors                    = 1;
    srvRange1.BaseShaderRegister                = 1;
    srvRange1.RegisterSpace                     = 0;
    srvRange1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params[4].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[4].DescriptorTable.NumDescriptorRanges = 1;
    params[4].DescriptorTable.pDescriptorRanges   = &srvRange1;
    params[4].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // [5] Descriptor table: 1 SRV at t2 — IBL irradiance TextureCube (pixel shader)
    D3D12_DESCRIPTOR_RANGE srvRange2 = {};
    srvRange2.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange2.NumDescriptors                    = 1;
    srvRange2.BaseShaderRegister                = 2;
    srvRange2.RegisterSpace                     = 0;
    srvRange2.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params9[9] = {};
    memcpy(params9, params, sizeof(params));

    params9[5].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params9[5].DescriptorTable.NumDescriptorRanges = 1;
    params9[5].DescriptorTable.pDescriptorRanges   = &srvRange2;
    params9[5].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // [6] Descriptor table: 1 SRV at t3 — IBL specular (prefiltered env) TextureCube
    D3D12_DESCRIPTOR_RANGE srvRange3 = {};
    srvRange3.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange3.NumDescriptors                    = 1;
    srvRange3.BaseShaderRegister                = 3;
    srvRange3.RegisterSpace                     = 0;
    srvRange3.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params9[6].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params9[6].DescriptorTable.NumDescriptorRanges = 1;
    params9[6].DescriptorTable.pDescriptorRanges   = &srvRange3;
    params9[6].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // [7] Descriptor table: 1 SRV at t4 — BRDF LUT Texture2D
    D3D12_DESCRIPTOR_RANGE srvRange4 = {};
    srvRange4.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange4.NumDescriptors                    = 1;
    srvRange4.BaseShaderRegister                = 4;
    srvRange4.RegisterSpace                     = 0;
    srvRange4.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params9[7].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params9[7].DescriptorTable.NumDescriptorRanges = 1;
    params9[7].DescriptorTable.pDescriptorRanges   = &srvRange4;
    params9[7].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // [8] Descriptor table: 1 SRV at t5 — normal map Texture2D (pixel shader)
    D3D12_DESCRIPTOR_RANGE srvRange5 = {};
    srvRange5.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange5.NumDescriptors                    = 1;
    srvRange5.BaseShaderRegister                = 5;
    srvRange5.RegisterSpace                     = 0;
    srvRange5.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params10[10] = {};
    memcpy(params10, params9, sizeof(params9));

    params10[8].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params10[8].DescriptorTable.NumDescriptorRanges = 1;
    params10[8].DescriptorTable.pDescriptorRanges   = &srvRange5;
    params10[8].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // [9] Descriptor table: 1 SRV at t6 — SSAO ambient-occlusion map (pixel shader)
    D3D12_DESCRIPTOR_RANGE srvRange6 = {};
    srvRange6.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange6.NumDescriptors                    = 1;
    srvRange6.BaseShaderRegister                = 6;
    srvRange6.RegisterSpace                     = 0;
    srvRange6.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params10[9].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params10[9].DescriptorTable.NumDescriptorRanges = 1;
    params10[9].DescriptorTable.pDescriptorRanges   = &srvRange6;
    params10[9].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // s0 — linear wrap sampler for albedo texture
    D3D12_STATIC_SAMPLER_DESC samplers[3] = {};
    samplers[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].MaxLOD           = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister   = 0;
    samplers[0].RegisterSpace    = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1 — comparison sampler for PCF shadow sampling (LESS_EQUAL, clamp-to-border)
    samplers[1].Filter           = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE; // fully lit outside frustum
    samplers[1].MinLOD           = 0;
    samplers[1].MaxLOD           = 0;
    samplers[1].ShaderRegister   = 1;
    samplers[1].RegisterSpace    = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s2 — linear clamp sampler for IBL cubemap and BRDF LUT sampling
    samplers[2].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[2].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[2].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[2].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[2].MaxLOD           = D3D12_FLOAT32_MAX;
    samplers[2].ShaderRegister   = 2;
    samplers[2].RegisterSpace    = 0;
    samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters     = 10;
    desc.pParameters       = params10;
    desc.NumStaticSamplers = 3;
    desc.pStaticSamplers   = samplers;
    desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, errors;
    HRESULT hr = D3D12SerializeRootSignature(&desc,
        D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA(static_cast<char*>(errors->GetBufferPointer()));
        return false;
    }
    hr = device->CreateRootSignature(0,
        blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&m_RootSignature));
    return SUCCEEDED(hr);
}

// ---------------------------------------------------------------------------
// BuildPSO — shared by CreatePSO() and HotReload() to avoid code duplication.
// ---------------------------------------------------------------------------
bool BasicPipeline::BuildPSO(ID3DBlob* vs, ID3DBlob* ps,
                              ComPtr<ID3D12PipelineState>& outPSO) const
{
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",     0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 60, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout            = { inputLayout, 6 };
    psoDesc.pRootSignature         = m_RootSignature.Get();
    psoDesc.VS                     = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                     = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable       = TRUE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask             = UINT_MAX;
    psoDesc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets       = 1;
    psoDesc.RTVFormats[0]          = m_RTVFormat;
    psoDesc.SampleDesc             = { 1, 0 };
    psoDesc.DepthStencilState.DepthEnable    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DSVFormat              = DXGI_FORMAT_D32_FLOAT;

    HRESULT hr = m_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&outPSO));
    return SUCCEEDED(hr);
}

// ---------------------------------------------------------------------------
bool BasicPipeline::CreatePSO(ID3D12Device* /*device*/, DXGI_FORMAT /*rtvFormat*/)
{
    ComPtr<ID3DBlob> vs, ps;
    const std::wstring shaderPath = GetExeDir() + L"Shaders/Basic.hlsl";
    try {
        vs = Shader::CompileFromFile(shaderPath, "VSMain", "vs_5_0");
        ps = Shader::CompileFromFile(shaderPath, "PSMain", "ps_5_0");
    } catch (const std::exception& e) {
        OutputDebugStringA(e.what());
        return false;
    }
    return BuildPSO(vs.Get(), ps.Get(), m_PSO);
}

// ---------------------------------------------------------------------------
// HasShaderChanged — polls the background watcher (resets the dirty flag).
// ---------------------------------------------------------------------------
bool BasicPipeline::HasShaderChanged()
{
    return m_Watcher.PollDirty();
}

// ---------------------------------------------------------------------------
// HotReload — recompile Basic.hlsl and swap the PSO atomically.
//
// PRECONDITION: the GPU must be idle before this is called (caller issues
// WaitForGPU() so the old PSO isn't referenced by any in-flight work).
//
// On compile failure the existing PSO is retained and the engine keeps
// rendering; the error is logged so the developer can fix the shader.
// ---------------------------------------------------------------------------
bool BasicPipeline::HotReload()
{
    LOG_INFO("BasicPipeline: recompiling shaders...");

    ComPtr<ID3DBlob> vs, ps;
    const std::wstring shaderPath = GetExeDir() + L"Shaders/Basic.hlsl";
    try {
        vs = Shader::CompileFromFile(shaderPath, "VSMain", "vs_5_0");
        ps = Shader::CompileFromFile(shaderPath, "PSMain", "ps_5_0");
    } catch (const std::exception& e) {
        LOG_WARN("BasicPipeline: shader compile failed — %s  (keeping previous PSO)", e.what());
        return false;
    }

    // Build the new PSO into a temporary before touching m_PSO so that a
    // CreateGraphicsPipelineState failure leaves the existing PSO intact.
    ComPtr<ID3D12PipelineState> newPSO;
    if (!BuildPSO(vs.Get(), ps.Get(), newPSO)) {
        LOG_WARN("BasicPipeline: PSO creation failed during hot reload (keeping previous PSO).");
        return false;
    }

    m_PSO = std::move(newPSO);
    LOG_INFO("BasicPipeline: hot reload succeeded — new PSO active.");
    return true;
}

} // namespace VibeEngine
