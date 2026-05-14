#pragma once
#include "GameObject.h"
#include <vector>
#include <memory>
#include <string>

namespace VibeEngine {

class Scene {
public:
    explicit Scene(const std::string& name = "Scene");
    ~Scene() = default;

    GameObject* CreateGameObject(const std::string& name = "GameObject");
    void        DestroyGameObject(GameObject* go);

    void Awake();
    void Start();
    void Update(float dt);
    void Render();

    const std::string& GetName() const { return m_Name; }
    const std::vector<std::unique_ptr<GameObject>>& GetGameObjects() const { return m_GameObjects; }

private:
    std::string m_Name;
    std::vector<std::unique_ptr<GameObject>> m_GameObjects;
    bool m_Started = false;
};

} // namespace VibeEngine
