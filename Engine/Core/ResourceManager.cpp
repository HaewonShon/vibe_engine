#include "ResourceManager.h"
#include "Log.h"
#include "../Renderer/DX12Context.h"

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

bool ResourceManager::HasMesh(const std::string& name) const
{
    return m_Meshes.count(name) > 0;
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
