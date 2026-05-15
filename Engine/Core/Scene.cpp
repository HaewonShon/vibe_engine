#include "Scene.h"
#include "../Renderer/MeshRenderer.h"
#include "../Renderer/Camera.h"
#include <algorithm>

namespace VibeEngine {

Scene::Scene(const std::string& name) : m_Name(name) {}

GameObject* Scene::CreateGameObject(const std::string& name)
{
    auto go = std::make_unique<GameObject>(name);
    GameObject* raw = go.get();
    m_GameObjects.push_back(std::move(go));
    return raw;
}

void Scene::DestroyGameObject(GameObject* go)
{
    m_GameObjects.erase(
        std::remove_if(m_GameObjects.begin(), m_GameObjects.end(),
            [go](const std::unique_ptr<GameObject>& p) { return p.get() == go; }),
        m_GameObjects.end());
}

void Scene::Awake()
{
    for (auto& go : m_GameObjects) go->Awake();
}

void Scene::Start()
{
    for (auto& go : m_GameObjects) go->Start();
    m_Started = true;
}

void Scene::Update(float dt)
{
    if (!m_Started) {
        Awake();
        Start();
    }
    for (auto& go : m_GameObjects) go->Update(dt);
}

void Scene::Render()
{
    // Find the active camera
    Camera* cam = nullptr;
    for (auto& go : m_GameObjects) {
        cam = go->GetComponent<Camera>();
        if (cam) break;
    }
    if (!cam) return;

    auto vp = cam->GetViewProjectionMatrix();

    // Draw all MeshRenderers
    for (auto& go : m_GameObjects) {
        if (auto* mr = go->GetComponent<MeshRenderer>())
            mr->Draw(vp);
    }
}

// ---- Search -----------------------------------------------------------------

GameObject* Scene::FindByName(const std::string& name) const
{
    for (auto& go : m_GameObjects)
        if (go->IsActive() && go->GetName() == name)
            return go.get();
    return nullptr;
}

GameObject* Scene::FindByTag(const GameplayTag& tag) const
{
    for (auto& go : m_GameObjects)
        if (go->IsActive() && go->HasTag(tag))
            return go.get();
    return nullptr;
}

std::vector<GameObject*> Scene::FindAllByTag(const GameplayTag& tag) const
{
    std::vector<GameObject*> result;
    for (auto& go : m_GameObjects)
        if (go->IsActive() && go->HasTag(tag))
            result.push_back(go.get());
    return result;
}

std::vector<GameObject*> Scene::FindAllByTagExact(const GameplayTag& tag) const
{
    std::vector<GameObject*> result;
    for (auto& go : m_GameObjects)
        if (go->IsActive() && go->HasTagExact(tag))
            result.push_back(go.get());
    return result;
}

} // namespace VibeEngine
