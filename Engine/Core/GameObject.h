#pragma once
#include "Component.h"
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace VibeEngine {

class Transform;

class GameObject {
public:
    explicit GameObject(const std::string& name = "GameObject");
    ~GameObject();

    template<typename T, typename... Args>
    T* AddComponent(Args&&... args);

    template<typename T>
    T* GetComponent() const;

    template<typename T>
    bool HasComponent() const;

    void Awake();
    void Start();
    void Update(float dt);

    const std::string& GetName()  const { return m_Name; }
    bool               IsActive() const { return m_Active; }
    void               SetActive(bool v) { m_Active = v; }
    Transform*         GetTransform() const { return m_Transform; }

private:
    std::string m_Name;
    bool        m_Active    = true;
    Transform*  m_Transform = nullptr;

    std::unordered_map<std::type_index, std::unique_ptr<Component>> m_Components;
};

// ---- Template implementations ----

template<typename T, typename... Args>
T* GameObject::AddComponent(Args&&... args)
{
    static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
    auto comp     = std::make_unique<T>(std::forward<Args>(args)...);
    comp->m_Owner = this;
    T* raw        = comp.get();
    m_Components[typeid(T)] = std::move(comp);
    return raw;
}

template<typename T>
T* GameObject::GetComponent() const
{
    auto it = m_Components.find(typeid(T));
    if (it != m_Components.end())
        return static_cast<T*>(it->second.get());
    return nullptr;
}

template<typename T>
bool GameObject::HasComponent() const
{
    return m_Components.count(typeid(T)) > 0;
}

} // namespace VibeEngine
