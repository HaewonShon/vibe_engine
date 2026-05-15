#pragma once

namespace VibeEngine {

class GameObject;

// ============================================================================
// Component  (base class)
//
// Lifecycle:
//   Awake()     — called once when the scene starts, regardless of enabled state.
//   OnEnable()  — called when the component is first enabled (after Awake),
//                 and again each time SetEnabled(true) is called.
//   Start()     — called before the first Update(), only if enabled.
//   Update(dt)  — called every frame, only if enabled.
//   OnDisable() — called when SetEnabled(false) is called.
//   OnDestroy() — called when the owning GameObject is destroyed.
//
// Enable/disable:
//   comp->SetEnabled(false);   // pauses Update; triggers OnDisable
//   comp->SetEnabled(true);    // resumes Update; triggers OnEnable
//   comp->IsEnabled();
// ============================================================================
class Component {
public:
    virtual ~Component() = default;

    // ---- Lifecycle hooks (override in subclasses) ---------------------------
    virtual void Awake()              {}   // always called, even when disabled
    virtual void OnEnable()           {}   // called when enabled (incl. first activation)
    virtual void Start()              {}   // called before first Update — only if enabled
    virtual void Update(float /*dt*/) {}   // every frame — only if enabled
    virtual void OnDisable()          {}   // called when disabled
    virtual void OnDestroy()          {}   // called on GameObject destruction

    // ---- Enable / disable --------------------------------------------------
    void SetEnabled(bool enabled)
    {
        if (m_Enabled == enabled) return;
        m_Enabled = enabled;
        if (m_Enabled) OnEnable();
        else           OnDisable();
    }
    bool IsEnabled() const { return m_Enabled; }

    // ---- Owner access -------------------------------------------------------
    GameObject* GetGameObject() const { return m_Owner; }

private:
    friend class GameObject;
    GameObject* m_Owner   = nullptr;
    bool        m_Enabled = true;
};

} // namespace VibeEngine
