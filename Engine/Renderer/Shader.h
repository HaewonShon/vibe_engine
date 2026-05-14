#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

class Shader {
public:
    static ComPtr<ID3DBlob> CompileFromFile(
        const std::wstring& path,
        const std::string&  entryPoint,
        const std::string&  target);
};

} // namespace VibeEngine
