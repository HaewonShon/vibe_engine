#pragma once
#include "Scene.h"
#include <memory>
#include <unordered_map>
#include <string>

namespace VibeEngine {

class SceneManager {
public:
    static SceneManager& Get();

    Scene* CreateScene(const std::string& name);
    void   LoadScene(const std::string& name);
    Scene* GetActiveScene() const { return m_ActiveScene; }

private:
    SceneManager() = default;
    std::unordered_map<std::string, std::unique_ptr<Scene>> m_Scenes;
    Scene* m_ActiveScene = nullptr;
};

} // namespace VibeEngine
