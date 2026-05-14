#include "Shader.h"
#include <stdexcept>

#pragma comment(lib, "d3dcompiler.lib")

namespace VibeEngine {

ComPtr<ID3DBlob> Shader::CompileFromFile(
    const std::wstring& path,
    const std::string&  entryPoint,
    const std::string&  target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(
        path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(), target.c_str(), flags, 0,
        &code, &errors);

    if (FAILED(hr)) {
        std::string msg = "Shader compile failed";
        (void)path; // wstring-to-char conversion skipped to avoid narrowing
        if (errors)
            msg += "\n" + std::string(static_cast<char*>(errors->GetBufferPointer()),
                                      errors->GetBufferSize());
        OutputDebugStringA(msg.c_str());
        throw std::runtime_error(msg);
    }
    return code;
}

} // namespace VibeEngine
