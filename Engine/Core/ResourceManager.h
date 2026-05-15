#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include "../Renderer/Mesh.h"
#include "../Renderer/Texture.h"
#include "../Renderer/OBJLoader.h"

namespace VibeEngine {

class DX12Context;

// Singleton cache for GPU resources.
// Load calls must be issued between DX12Context::BeginFrame / EndFrame.
// Call ReleaseUploadBuffers() once after WaitForGPU().
class ResourceManager {
public:
    static ResourceManager& Get();

    void Initialize(DX12Context* ctx);
    void Shutdown();

    // ---- Mesh ---------------------------------------------------------------
    // Returns cached mesh, or invokes factory() and caches the result.
    std::shared_ptr<Mesh> GetOrLoadMesh(const std::string& name,
                                         std::function<Mesh()> factory);

    // Built-in geometry (cached under "__cube" / "__plane")
    std::shared_ptr<Mesh> GetCube ();
    std::shared_ptr<Mesh> GetPlane();

    bool HasMesh(const std::string& name) const;

    // Load an OBJ file and cache it by path. Returns nullptr on failure.
    // Must be called between BeginFrame/EndFrame.
    std::shared_ptr<Mesh> LoadModel(const std::wstring& path);

    // ---- Texture ------------------------------------------------------------
    // Returns cached texture, or loads from disk and caches.
    std::shared_ptr<Texture> GetOrLoadTexture(const std::wstring& path);

    bool HasTexture(const std::wstring& path) const;

    // ---- Lifecycle ----------------------------------------------------------
    // Release upload heaps — call once after DX12Context::WaitForGPU().
    void ReleaseUploadBuffers();

private:
    ResourceManager() = default;

    DX12Context* m_Ctx = nullptr;

    std::unordered_map<std::string,  std::shared_ptr<Mesh>>    m_Meshes;
    std::unordered_map<std::wstring, std::shared_ptr<Texture>> m_Textures;
};

} // namespace VibeEngine
