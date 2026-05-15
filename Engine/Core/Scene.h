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

    // ---- GameObject management ----------------------------------------------
    GameObject* CreateGameObject(const std::string& name = "GameObject");
    void        DestroyGameObject(GameObject* go);

    // ---- Lifecycle ----------------------------------------------------------
    void Awake();
    void Start();
    void Update(float dt);
    void Render();

    // ---- Search -------------------------------------------------------------

    // Find the first active GameObject whose name matches exactly.
    GameObject* FindByName(const std::string& name) const;

    // Find the first active GameObject that HasTag(tag) — hierarchical match.
    //   FindByTag(GameplayTag::FromString("Enemy")) finds "Enemy.Elite" too.
    GameObject* FindByTag(const GameplayTag& tag) const;

    // Find all active GameObjects that HasTag(tag).
    std::vector<GameObject*> FindAllByTag(const GameplayTag& tag) const;

    // Find all active GameObjects that HasTagExact(tag).
    std::vector<GameObject*> FindAllByTagExact(const GameplayTag& tag) const;

    // ---- Accessors ----------------------------------------------------------
    const std::string& GetName() const { return m_Name; }
    const std::vector<std::unique_ptr<GameObject>>& GetGameObjects() const { return m_GameObjects; }

private:
    std::string m_Name;
    std::vector<std::unique_ptr<GameObject>> m_GameObjects;
    bool m_Started = false;
};

} // namespace VibeEngine
