#pragma once

namespace VibeEngine {

class GameObject;

class Component {
public:
    virtual ~Component() = default;

    virtual void Awake()             {}
    virtual void Start()             {}
    virtual void Update(float /*dt*/) {}
    virtual void OnDestroy()         {}

    GameObject* GetGameObject() const { return m_Owner; }

private:
    friend class GameObject;
    GameObject* m_Owner = nullptr;
};

} // namespace VibeEngine
