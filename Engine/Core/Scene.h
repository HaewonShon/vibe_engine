#pragma once
#include "GameObject.h"
#include "Layer.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <unordered_set>

namespace VibeEngine {

// ============================================================================
// Scene
//
// Manages GameObjects and exposes a rich query API.
//
// ---- Name -------------------------------------------------------------------
//   FindByName("Player")               first active GO with that exact name
//   FindAllByName("Bullet")            all active GOs with that name
//
// ---- Tag (hierarchical) -----------------------------------------------------
//   FindByTag(tag)                     first active GO whose tags match tag
//   FindAllByTag(tag)                  all matching (hierarchical)
//   FindAllByTagExact(tag)             all matching (exact)
//
// ---- Component --------------------------------------------------------------
//   FindComponentOfType<Camera>()      first Camera* in any active GO
//   FindComponentsOfType<MeshRenderer>() all MeshRenderer* in active GOs
//
// ---- Predicate --------------------------------------------------------------
//   FindByPredicate([](const GameObject* go){ return go->GetName().starts_with("E"); })
//   FindAllByPredicate(fn)
//
// ---- Bulk / stats -----------------------------------------------------------
//   GetActiveGameObjects()             all active GO pointers (snapshot)
//   CountGameObjects()                 total (active + inactive)
//   CountActiveGameObjects()
//
// ---- Deferred destroy -------------------------------------------------------
//   MarkForDestroy(go)                 safe to call during Update / component code;
//                                      GO is removed at the top of the next Update.
// ============================================================================
class Scene {
public:
    using Predicate = std::function<bool(const GameObject*)>;

    explicit Scene(const std::string& name = "Scene");
    ~Scene() = default;

    // ---- GameObject management ----------------------------------------------
    GameObject* CreateGameObject(const std::string& name = "GameObject");

    // Immediate removal — avoid during Update iteration; use MarkForDestroy instead.
    void DestroyGameObject(GameObject* go);

    // Queue for removal at the start of the next Update — safe from any context.
    void MarkForDestroy(GameObject* go);

    // ---- Lifecycle ----------------------------------------------------------
    void Awake();
    void Start();
    void Update(float dt);
    void Render();

    // ---- Search: name -------------------------------------------------------

    // First active GO whose name matches exactly.
    GameObject* FindByName(const std::string& name) const;

    // All active GOs whose name matches (multiple GOs can share a name).
    std::vector<GameObject*> FindAllByName(const std::string& name) const;

    // ---- Search: tag --------------------------------------------------------

    // First active GO that HasTag(tag) — hierarchical: "Enemy" matches "Enemy.Skill.Attack1".
    GameObject* FindByTag(const GameplayTag& tag) const;

    // All active GOs that HasTag(tag) — hierarchical.
    std::vector<GameObject*> FindAllByTag(const GameplayTag& tag) const;

    // All active GOs that HasTagExact(tag).
    std::vector<GameObject*> FindAllByTagExact(const GameplayTag& tag) const;

    // ---- Search: component (templates) --------------------------------------

    // First enabled component of type T across all active GOs.
    // Returns the component pointer (call GetGameObject() to reach the owner).
    template<typename T>
    T* FindComponentOfType() const
    {
        for (auto& go : m_GameObjects) {
            if (!go->IsActive()) continue;
            if (auto* c = go->GetComponent<T>())
                if (c->IsEnabled()) return c;
        }
        return nullptr;
    }

    // All enabled components of type T across all active GOs.
    template<typename T>
    std::vector<T*> FindComponentsOfType() const
    {
        std::vector<T*> result;
        for (auto& go : m_GameObjects) {
            if (!go->IsActive()) continue;
            if (auto* c = go->GetComponent<T>())
                if (c->IsEnabled()) result.push_back(c);
        }
        return result;
    }

    // First active GO that has an enabled component of type T.
    template<typename T>
    GameObject* FindByComponent() const
    {
        for (auto& go : m_GameObjects) {
            if (!go->IsActive()) continue;
            if (auto* c = go->GetComponent<T>())
                if (c->IsEnabled()) return go.get();
        }
        return nullptr;
    }

    // All active GOs that have an enabled component of type T.
    template<typename T>
    std::vector<GameObject*> FindAllByComponent() const
    {
        std::vector<GameObject*> result;
        for (auto& go : m_GameObjects) {
            if (!go->IsActive()) continue;
            if (auto* c = go->GetComponent<T>())
                if (c->IsEnabled()) result.push_back(go.get());
        }
        return result;
    }

    // ---- Search: layer ------------------------------------------------------

    // First active GO on exactly this layer.
    GameObject* FindByLayer(int layer) const;

    // All active GOs on exactly this layer.
    std::vector<GameObject*> FindAllByLayer(int layer) const;

    // All active GOs whose layer bit is set in the mask.
    //   LayerMask mask = LayerMask::From(Layer::Enemy) | LayerMask::From(Layer::Player);
    //   auto gos = scene->FindAllByLayerMask(mask);
    std::vector<GameObject*> FindAllByLayerMask(LayerMask mask) const;

    // ---- Search: predicate --------------------------------------------------

    // First active GO for which fn returns true.
    GameObject* FindByPredicate(const Predicate& fn) const;

    // All active GOs for which fn returns true.
    std::vector<GameObject*> FindAllByPredicate(const Predicate& fn) const;

    // ---- Bulk / stats -------------------------------------------------------

    // Snapshot of all currently-active GO raw pointers.
    std::vector<GameObject*> GetActiveGameObjects() const;

    // Total number of GOs (active + inactive), excluding pending-destroy.
    int CountGameObjects() const { return static_cast<int>(m_GameObjects.size()); }

    // Number of active GOs.
    int CountActiveGameObjects() const;

    // ---- Accessors ----------------------------------------------------------
    const std::string& GetName() const { return m_Name; }
    const std::vector<std::unique_ptr<GameObject>>& GetGameObjects() const { return m_GameObjects; }

private:
    void FlushPendingDestroys();

    std::string m_Name;
    std::vector<std::unique_ptr<GameObject>> m_GameObjects;
    std::unordered_set<GameObject*>          m_PendingDestroy;
    bool m_Started = false;
};

} // namespace VibeEngine
