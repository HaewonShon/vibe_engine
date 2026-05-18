#pragma once
#include "Mesh.h"
#include "Texture.h"
#include <DirectXMath.h>
#include <memory>
#include <string>
#include <d3d12.h>

namespace VibeEngine {

class DX12Context;  // forward declaration for LoadWithMaterial ctx param

// ---------------------------------------------------------------------------
// FbxMaterialData — material properties extracted from a 3-D model file.
//
// Supports both FBX (Phong / PBR) and glTF 2.0 (metallic-roughness PBR).
//
// Limitation: all sub-meshes are merged into one vertex/index buffer, so only
// ONE material can be applied per file.  When the file has multiple
// materials the properties of the sub-mesh with the most triangles are used.
// ---------------------------------------------------------------------------
struct FbxMaterialData {
    DirectX::XMFLOAT4  albedo    = { 1.f, 1.f, 1.f, 1.f };
    float              roughness = 0.5f;
    float              metallic  = 0.0f;
    DirectX::XMFLOAT3  emissive  = { 0.f, 0.f, 0.f };

    // External texture path (FBX / .gltf with sidecar files).
    // Pass to ResourceManager::GetOrLoadTexture().
    std::wstring diffuseTexturePath;

    // Pre-decoded texture for embedded blobs (glTF .glb).
    // Takes priority over diffuseTexturePath when non-null.
    std::shared_ptr<Texture> embeddedAlbedo;

    bool hasMaterial = false;   // false = file contained no readable material
};

// ---------------------------------------------------------------------------
// FbxAsset — geometry + material data returned by LoadWithMaterial().
// ---------------------------------------------------------------------------
struct FbxAsset {
    Mesh            mesh;
    FbxMaterialData material;
};

// ---------------------------------------------------------------------------
// FBXLoader
// ---------------------------------------------------------------------------
// Loads FBX, glTF 2.0 (.gltf / .glb), and other Assimp-supported formats.
// All sub-meshes are merged into a single vertex/index buffer.
// Node hierarchy transforms are applied (world-space positions).
//
// Coordinate conventions:
//   FBX — Assimp normalises per-file metadata; no extra flag needed.
//   glTF — right-handed Y-up → aiProcess_MakeLeftHanded applied automatically.
//   UVs  — FBX: aiProcess_FlipUVs (OpenGL origin → DX top-left).
//           glTF: no flip (glTF already uses top-left / DX12 convention).
//
// Must be called with an open DX12 command list (between BeginFrame/EndFrame).
class FBXLoader {
public:
    // Geometry only — backward-compatible with existing code.
    static Mesh Load(ID3D12Device*              device,
                     ID3D12GraphicsCommandList* cmdList,
                     const std::wstring&        path);

    // Geometry + material data (albedo/roughness/metallic + diffuse texture).
    // For .glb files, embedded textures are decoded and stored in
    // FbxMaterialData::embeddedAlbedo (requires DX12Context for SRV alloc).
    // ctx may be nullptr only if embedded texture support is not needed.
    static FbxAsset LoadWithMaterial(ID3D12Device*              device,
                                     ID3D12GraphicsCommandList* cmdList,
                                     DX12Context*               ctx,
                                     const std::wstring&        path);
};

} // namespace VibeEngine
