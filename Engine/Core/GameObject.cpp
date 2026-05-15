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
        comp->Awake();   // always, regardless of enabled state

    // Fire OnEnable for all initially-enabled components after Awake completes
    for (auto& [type, comp] : m_Components)
        if (comp->IsEnabled()) comp->OnEnable();

    m_AwakeCalled = true;
}

void GameObject::Start()
{
    if (!m_Active) return;

    for (auto& [type, comp] : m_Components)
        if (comp->IsEnabled()) comp->Start();
}

void GameObject::Update(float dt)
{
    if (!m_Active) return;

    for (auto& [type, comp] : m_Components)
        if (comp->IsEnabled()) comp->Update(dt);
}

} // namespace VibeEngine
