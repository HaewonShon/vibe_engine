#pragma once
#include <DirectXMath.h>

namespace VibeEngine {

class UIRenderer;

// ============================================================================
// Rect — screen-space rectangle (pixels, origin = top-left)
// ============================================================================
struct Rect {
    float x = 0.f, y = 0.f;
    float w = 0.f, h = 0.f;

    bool Contains(float px, float py) const
    {
        return px >= x && px <= x + w
            && py >= y && py <= y + h;
    }
};

// ============================================================================
// UIElement — abstract base for all UI widgets
//
// Coordinate system: top-left origin, pixels.
// Override Update() for per-frame logic (animations, input response, …).
// Override Draw()   to issue rendering commands to UIRenderer.
// ============================================================================
class UIElement {
public:
    virtual ~UIElement() = default;

    virtual void Update(float /*dt*/) {}
    virtual void Draw(UIRenderer& renderer) = 0;

    // ---- Layout -------------------------------------------------------------
    Rect  rect    = {};
    bool  visible = true;

    void SetRect(float x, float y, float w, float h)
    {
        rect = { x, y, w, h };
    }
};

} // namespace VibeEngine
