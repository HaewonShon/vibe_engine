#pragma once
#include "Mesh.h"
#include <string>
#include <d3d12.h>

namespace VibeEngine {

// Loads a Wavefront OBJ file and produces a Mesh.
// Supported face formats: v, v/vt, v//vn, v/vt/vn  (triangles and quads)
// If the file has no normals, flat per-face normals are computed automatically.
// Must be called with an open DX12 command list (between BeginFrame/EndFrame).
class OBJLoader {
public:
    static Mesh Load(ID3D12Device* device,
                     ID3D12GraphicsCommandList* cmdList,
                     const std::wstring& path);
};

} // namespace VibeEngine
