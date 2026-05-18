#include "ResourceManager.h"
#include "Log.h"
#include "../Renderer/DX12Context.h"
#include <filesystem>

namespace VibeEngine {

ResourceManager& ResourceManager::Get()
{
    static ResourceManager inst;
    return inst;
}

void ResourceManager::Initialize(DX12Context* ctx)
{
    m_Ctx = ctx;
    LOG_INFO("ResourceManager initialized.");
}

void ResourceManager::Shutdown()
{
    m_Meshes.clear();
    m_Textures.clear();
    m_Ctx = nullptr;
    LOG_INFO("ResourceManager shut down.");
}

// ---- Mesh -------------------------------------------------------------------

std::shared_ptr<Mesh> ResourceManager::GetOrLoadMesh(
    const std::string& name, std::function<Mesh()> factory)
{
    auto it = m_Meshes.find(name);
    if (it != m_Meshes.end()) {
        LOG_INFO("ResourceManager: mesh cache hit [%s]", name.c_str());
        return it->second;
    }

    auto mesh = std::make_shared<Mesh>(factory());
    m_Meshes[name] = mesh;
    LOG_INFO("ResourceManager: mesh created [%s]", name.c_str());
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::GetCube()
{
    return GetOrLoadMesh("__cube", [this]() {
        return Mesh::CreateCube(m_Ctx->GetDevice(), m_Ctx->GetCommandList());
    });
}

std::shared_ptr<Mesh> ResourceManager::GetPlane()
{
    return GetOrLoadMesh("__plane", [this]() {
        return Mesh::CreatePlane(m_Ctx->GetDevice(), m_Ctx->GetCommandList());
    });
}

std::shared_ptr<Mesh> ResourceManager::GetGrid(int divisions)
{
    std::string key = "__grid_" + std::to_string(divisions);
    return GetOrLoadMesh(key, [this, divisions]() {
        return Mesh::CreateGrid(m_Ctx->GetDevice(), m_Ctx->GetCommandList(), divisions);
    });
}

bool ResourceManager::HasMesh(const std::string& name) const
{
    return m_Meshes.count(name) > 0;
}

std::shared_ptr<Mesh> ResourceManager::LoadModel(const std::wstring& path)
{
    // Use the wstring path as cache key (converted via narrow string key)
    std::string key(path.begin(), path.end());

    auto it = m_Meshes.find(key);
    if (it != m_Meshes.end()) {
        LOG_INFO("ResourceManager: model cache hit [%s]", key.c_str());
        return it->second;
    }

    Mesh loaded;
    std::filesystem::path fsPath(path);
    auto ext = fsPath.extension().wstring();
    // case-insensitive extension check
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));

    if (ext == L".fbx" || ext == L".gltf" || ext == L".glb") {
        loaded = FBXLoader::Load(m_Ctx->GetDevice(), m_Ctx->GetCommandList(), path);
    } else {
        loaded = OBJLoader::Load(m_Ctx->GetDevice(), m_Ctx->GetCommandList(), path);
    }

    if (loaded.GetIndexCount() == 0) {
        LOG_ERROR("ResourceManager: failed to load model [%s]", key.c_str());
        return nullptr;
    }

    auto mesh = std::make_shared<Mesh>(std::move(loaded));
    m_Meshes[key] = mesh;
    LOG_INFO("ResourceManager: model loaded [%s]  verts cached", key.c_str());
    return mesh;
}

ResourceManager::ModelWithMaterial
ResourceManager::LoadModelWithMaterial(const std::wstring& path)
{
    std::string key(path.begin(), path.end());

    // Cache hit — mesh only, material data not stored separately
    auto it = m_Meshes.find(key);
    if (it != m_Meshes.end()) {
        LOG_INFO("ResourceManager: model cache hit [%s]", key.c_str());
        return { it->second, {} };
    }

    // FBX and glTF support material + texture extraction via Assimp.
    std::filesystem::path fsPath(path);
    auto ext = fsPath.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));

    const bool assimp = (ext == L".fbx" || ext == L".gltf" || ext == L".glb");
    if (!assimp) {
        // OBJ and others: geometry-only fallback
        auto mesh = LoadModel(path);
        return { mesh, {} };
    }

    // Pass m_Ctx so FBXLoader can decode embedded textures (.glb blobs).
    FbxAsset asset = FBXLoader::LoadWithMaterial(
        m_Ctx->GetDevice(), m_Ctx->GetCommandList(), m_Ctx, path);

    if (asset.mesh.GetIndexCount() == 0) {
        LOG_ERROR("ResourceManager: failed to load model [%s]", key.c_str());
        return { nullptr, {} };
    }

    auto mesh = std::make_shared<Mesh>(std::move(asset.mesh));
    m_Meshes[key] = mesh;
    LOG_INFO("ResourceManager: model+material loaded [%s]", key.c_str());
    return { mesh, asset.material };
}

// ---- Texture ----------------------------------------------------------------

std::shared_ptr<Texture> ResourceManager::GetOrLoadTexture(const std::wstring& path)
{
    auto it = m_Textures.find(path);
    if (it != m_Textures.end()) {
        LOG_INFO("ResourceManager: texture cache hit");
        return it->second;
    }

    auto tex = std::make_shared<Texture>();
    if (!tex->LoadFromFile(m_Ctx->GetDevice(), m_Ctx->GetCommandList(), *m_Ctx, path)) {
        LOG_ERROR("ResourceManager: failed to load texture");
        return nullptr;
    }
    m_Textures[path] = tex;
    LOG_INFO("ResourceManager: texture loaded");
    return tex;
}

bool ResourceManager::HasTexture(const std::wstring& path) const
{
    return m_Textures.count(path) > 0;
}

// ---- Lifecycle --------------------------------------------------------------

void ResourceManager::ReleaseUploadBuffers()
{
    for (auto& [key, mesh] : m_Meshes)
        mesh->ReleaseUploadBuffers();
    for (auto& [path, tex] : m_Textures)
        tex->ReleaseUploadBuffer();
}

} // namespace VibeEngine
