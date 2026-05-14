#include "GameObject.h"
#include "Transform.h"

namespace VibeEngine {

GameObject::GameObject(const std::string& name)
    : m_Name(name)
{
    // Every GameObject always has a Transform
    auto t     = std::make_unique<Transform>();
    t->m_Owner = this;
    m_Transform = t.get();
    m_Components[typeid(Transform)] = std::move(t);
}

GameObject::~GameObject()
{
    for (auto& [type, comp] : m_Components)
        comp->OnDestroy();
}

void GameObject::Awake()
{
    if (!m_Active) return;
    for (auto& [type, comp] : m_Components)
        comp->Awake();
}

void GameObject::Start()
{
    if (!m_Active) return;
    for (auto& [type, comp] : m_Components)
        comp->Start();
}

void GameObject::Update(float dt)
{
    if (!m_Active) return;
    for (auto& [type, comp] : m_Components)
        comp->Update(dt);
}

} // namespace VibeEngine
