#pragma once
#include "UIElement.h"
#include "UIRenderer.h"
#include "../Input/InputManager.h"
#include <string>
#include <functional>

namespace VibeEngine {

// ============================================================================
// UIButton — clickable panel with a centered label
//
// Responds to left mouse button clicks.
// The window HWND is needed only for cursor-position conversion (screen → client).
// Pass it via SetWindow(), or leave nullptr to use raw GetCursorPos().
//
// Usage:
//   auto* btn = canvas.AddButton();
//   btn->SetRect(100, 200, 160, 40);
//   btn->label = "Restart";
//   btn->SetOnClick([&]{ RestartScene(); });
// ============================================================================
class UIButton : public UIElement {
public:
    std::string         label    = "Button";
    float               scale    = 1.f;
    HWND                hwnd     = nullptr;
    std::function<void()> onClick;

    // Colour states
    DirectX::XMFLOAT4 colorNormal  = { 0.20f, 0.20f, 0.22f, 0.88f };
    DirectX::XMFLOAT4 colorHover   = { 0.35f, 0.35f, 0.40f, 0.95f };
    DirectX::XMFLOAT4 colorPress   = { 0.10f, 0.10f, 0.12f, 1.00f };
    DirectX::XMFLOAT4 colorBorder  = { 0.70f, 0.70f, 0.75f, 1.00f };
    DirectX::XMFLOAT4 colorText    = { 1.00f, 1.00f, 1.00f, 1.00f };

    void SetOnClick(std::function<void()> fn) { onClick = std::move(fn); }

    void Update(float /*dt*/) override
    {
        auto& im = InputManager::Get();

        // Get mouse position in client coordinates
        POINT pt = im.GetMousePosition();
        if (hwnd) ScreenToClient(hwnd, &pt);

        bool wasHovered = m_Hovered;
        m_Hovered = rect.Contains(static_cast<float>(pt.x), static_cast<float>(pt.y));

        bool mouseDown = im.IsMouseButtonDown(0);
        bool mouseClick = wasHovered && m_Hovered
                       && !mouseDown && m_WasDown; // released over button

        m_Pressed = m_Hovered && mouseDown;
        m_WasDown = mouseDown;

        if (mouseClick && onClick)
            onClick();
    }

    void Draw(UIRenderer& r) override
    {
        if (!visible) return;

        DirectX::XMFLOAT4 bg = m_Pressed  ? colorPress
                             : m_Hovered  ? colorHover
                             : colorNormal;
        r.DrawRect  (rect.x, rect.y, rect.w, rect.h, bg);
        r.DrawBorder(rect.x, rect.y, rect.w, rect.h, 1.5f, colorBorder);

        // Centred label
        float tw = r.MeasureText(label.c_str(), scale);
        float ch = r.GetCharHeight() * scale;
        float tx = rect.x + (rect.w - tw) * 0.5f;
        float ty = rect.y + (rect.h - ch) * 0.5f;
        r.DrawText(tx, ty, label.c_str(), colorText, scale);
    }

private:
    bool m_Hovered = false;
    bool m_Pressed = false;
    bool m_WasDown = false;
};

} // namespace VibeEngine
