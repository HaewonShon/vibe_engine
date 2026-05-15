#pragma once
#include "UIElement.h"
#include "UIRenderer.h"

namespace VibeEngine {

// ============================================================================
// UIPanel — solid colour rectangle with an optional border
// ============================================================================
class UIPanel : public UIElement {
public:
    DirectX::XMFLOAT4 backgroundColor = { 0.f, 0.f, 0.f, 0.7f };
    DirectX::XMFLOAT4 borderColor     = { 1.f, 1.f, 1.f, 1.f  };
    float             borderWidth      = 0.f;

    void Draw(UIRenderer& r) override
    {
        if (!visible) return;
        r.DrawRect(rect.x, rect.y, rect.w, rect.h, backgroundColor);
        if (borderWidth > 0.f)
            r.DrawBorder(rect.x, rect.y, rect.w, rect.h,
                         borderWidth, borderColor);
    }
};

} // namespace VibeEngine
