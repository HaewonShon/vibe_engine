#include "ShadowPipeline.h"
#include "Shader.h"
#include "DX12Helpers.h"
#include "../Core/Log.h"
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

// ---------------------------------------------------------------------------
bool ShadowPipeline::Create(ID3D12Device* device)
{
    if (!CreateRootSignature(device)) {
        LOG_ERROR("ShadowPipeline: root signature creation failed.");
        return false;
    }
    if (!CreatePSO(device)) {
        LOG_ERROR("ShadowPipeline: PSO creation failed.");
        return false;
    }
    LOG_INFO("ShadowPipeline: created.");
    return true;
}

void ShadowPipeline::Destroy()
{
    m_PSO.Reset();
    m_RootSignature.Reset();
}

void ShadowPipeline::Bind(ID3D12GraphicsCommandList* cmdList) const
{
    cmdList->SetGraphicsRootSignature(m_RootSignature.Get());
    cmdList->SetPipelineState(m_PSO.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

// ---------------------------------------------------------------------------
bool ShadowPipeline::CreateRootSignature(ID3D12Device* device)
{
    // [0] Root CBV b0 — ShadowPerObjectCB { LightMVP } (vertex shader only)
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.Descriptor.ShaderRegister = 0;
    param.Descriptor.RegisterSpace  = 0;
    param.ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters     = 1;
    desc.pParameters       = &param;
    desc.NumStaticSamplers = 0;
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
bool ShadowPipeline::CreatePSO(ID3D12Device* device)
{
    ComPtr<ID3DBlob> vs;
    const std::wstring shaderPath = GetExeDir() + L"Shaders/Shadow.hlsl";
    try {
        vs = Shader::CompileFromFile(shaderPath, "VSMain", "vs_5_0");
    } catch (const std::exception& e) {
        OutputDebugStringA(e.what());
        return false;
    }

    // Input layout must match the engine's standard Vertex struct so existing
    // mesh VBs can be bound without modification.
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout           = { inputLayout, 4 };
    psoDesc.pRootSignature        = m_RootSignature.Get();
    psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    // No pixel shader — only depth is written
    psoDesc.PS                    = { nullptr, 0 };

    // Rasterizer: back-face culling reduces Peter-Pan shadow gap;
    // slope-scale bias reduces self-shadowing (acne) on lit faces.
    psoDesc.RasterizerState.FillMode             = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode             = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.DepthClipEnable      = TRUE;
    psoDesc.RasterizerState.DepthBias            = 1000;
    psoDesc.RasterizerState.DepthBiasClamp       = 0.0f;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 2.0f;

    // No colour writes (depth-only)
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 0;               // depth-only, no RTV
    psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc            = { 1, 0 };
    psoDesc.DepthStencilState.DepthEnable    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PSO));
    if (FAILED(hr)) {
        LOG_ERROR("ShadowPipeline: CreateGraphicsPipelineState failed (hr=0x%08X).",
                  static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

} // namespace VibeEngine
