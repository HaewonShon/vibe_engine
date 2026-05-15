#pragma once
#include "UIElement.h"
#include "UIRenderer.h"
#include <string>

namespace VibeEngine {

// ============================================================================
// UILabel — single- or multi-line text element
//
// Alignment applies horizontally within rect.w.
// If rect.w == 0, left-aligned text is drawn from rect.x without clipping.
// ============================================================================
class UILabel : public UIElement {
public:
    enum class Align { Left, Center, Right };

    std::string          text    = {};
    DirectX::XMFLOAT4   color   = { 1.f, 1.f, 1.f, 1.f };
    float                scale   = 1.f;
    Align                align   = Align::Left;

    void Draw(UIRenderer& r) override
    {
        if (!visible || text.empty()) return;

        float tx = rect.x;
        if (rect.w > 0.f && align != Align::Left) {
            float tw = r.MeasureText(text.c_str(), scale);
            if (align == Align::Center)
                tx = rect.x + (rect.w - tw) * 0.5f;
            else  // Right
                tx = rect.x + rect.w - tw;
        }
        r.DrawText(tx, rect.y, text.c_str(), color, scale);
    }
};

} // namespace VibeEngine
