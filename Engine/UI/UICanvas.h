#pragma once
#include "UIElement.h"
#include <vector>
#include <memory>
#include <utility>

namespace VibeEngine {

class UIRenderer;

// ============================================================================
// UICanvas — owns and drives a collection of UIElements
//
// UICanvas is a standalone class (not a Component).
// The owner (e.g. SandboxApp) calls:
//   - canvas.Update(dt)            each frame (handles button clicks, etc.)
//   - UIRenderer::Get().BeginPass(...)
//   - canvas.Draw()
//   - UIRenderer::Get().EndPass()
//
// Typical setup:
//   m_Canvas.AddElement<UIPanel>()->SetRect(10, 10, 300, 80);
//   auto* lbl = m_Canvas.AddElement<UILabel>();
//   lbl->SetRect(20, 20, 0, 0);
//   lbl->text = "Hello";
// ============================================================================
class UICanvas {
public:
    UICanvas()  = default;
    ~UICanvas() = default;

    // Create and register a new element (returns raw pointer for configuration)
    template<typename T, typename... Args>
    T* AddElement(Args&&... args)
    {
        auto elem = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = elem.get();
        m_Elements.push_back(std::move(elem));
        return raw;
    }

    // Remove all elements
    void Clear() { m_Elements.clear(); }

    // Update all visible elements (input handling, animations, …)
    void Update(float dt)
    {
        for (auto& e : m_Elements)
            if (e->visible) e->Update(dt);
    }

    // Draw all visible elements via UIRenderer.
    // Call between UIRenderer::BeginPass() and EndPass().
    void Draw(UIRenderer& renderer)
    {
        for (auto& e : m_Elements)
            if (e->visible) e->Draw(renderer);
    }

    std::size_t ElementCount() const { return m_Elements.size(); }

private:
    std::vector<std::unique_ptr<UIElement>> m_Elements;
};

} // namespace VibeEngine
