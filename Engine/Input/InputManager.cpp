#include "InputManager.h"

namespace VibeEngine {

InputManager& InputManager::Get()
{
    static InputManager instance;
    return instance;
}

void InputManager::Update()
{
    memcpy(m_Previous, m_Current, sizeof(m_Current));
    for (int i = 0; i < 256; ++i)
        m_Current[i] = GetAsyncKeyState(i);
    GetCursorPos(&m_MousePos);
}

bool InputManager::IsKeyDown(KeyCode key) const
{
    return (m_Current[static_cast<int>(key)] & 0x8000) != 0;
}

bool InputManager::IsKeyPressed(KeyCode key) const
{
    int k = static_cast<int>(key);
    return (m_Current[k] & 0x8000) != 0 && (m_Previous[k] & 0x8000) == 0;
}

bool InputManager::IsKeyReleased(KeyCode key) const
{
    int k = static_cast<int>(key);
    return (m_Current[k] & 0x8000) == 0 && (m_Previous[k] & 0x8000) != 0;
}

bool InputManager::IsMouseButtonDown(int button) const
{
    static const int vk[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
    if (button < 0 || button > 2) return false;
    return (m_Current[vk[button]] & 0x8000) != 0;
}

} // namespace VibeEngine
