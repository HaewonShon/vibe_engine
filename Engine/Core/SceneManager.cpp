#include "SceneManager.h"

namespace VibeEngine {

SceneManager& SceneManager::Get()
{
    static SceneManager instance;
    return instance;
}

Scene* SceneManager::CreateScene(const std::string& name)
{
    auto scene = std::make_unique<Scene>(name);
    Scene* raw = scene.get();
    m_Scenes[name] = std::move(scene);
    return raw;
}

void SceneManager::LoadScene(const std::string& name)
{
    auto it = m_Scenes.find(name);
    if (it != m_Scenes.end())
        m_ActiveScene = it->second.get();
}

} // namespace VibeEngine
