#include "BasicPipeline.h"
#include "Shader.h"
#include "DX12Helpers.h"
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
    if (!CreateRootSignature(device)) return false;
    if (!CreatePSO(device, rtvFormat)) return false;
    return true;
}

void BasicPipeline::Destroy()
{
    m_PSO.Reset();
    m_RootSignature.Reset();
}

bool BasicPipeline::CreateRootSignature(ID3D12Device* device)
{
    // One root CBV at b0 (per-object MVP)
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.Descriptor.ShaderRegister = 0;
    param.Descriptor.RegisterSpace  = 0;
    param.ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters   = &param;
    desc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

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

bool BasicPipeline::CreatePSO(ID3D12Device* device, DXGI_FORMAT rtvFormat)
{
    ComPtr<ID3DBlob> vs, ps;
    std::wstring shaderPath = GetExeDir() + L"Shaders/Basic.hlsl";
    try {
        vs = Shader::CompileFromFile(shaderPath, "VSMain", "vs_5_0");
        ps = Shader::CompileFromFile(shaderPath, "PSMain", "ps_5_0");
    } catch (const std::exception& e) {
        OutputDebugStringA(e.what());
        return false;
    }

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout            = { inputLayout, 2 };
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
    psoDesc.RTVFormats[0]          = rtvFormat;
    psoDesc.SampleDesc                      = { 1, 0 };
    psoDesc.DepthStencilState.DepthEnable   = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc     = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DSVFormat                       = DXGI_FORMAT_D32_FLOAT;

    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PSO));
    return SUCCEEDED(hr);
}

} // namespace VibeEngine
