#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace VibeEngine {

enum class KeyCode : int {
    A = 'A', B = 'B', C = 'C', D = 'D', E = 'E', F = 'F', G = 'G',
    H = 'H', I = 'I', J = 'J', K = 'K', L = 'L', M = 'M', N = 'N',
    O = 'O', P = 'P', Q = 'Q', R = 'R', S = 'S', T = 'T', U = 'U',
    V = 'V', W = 'W', X = 'X', Y = 'Y', Z = 'Z',
    Left   = VK_LEFT,
    Right  = VK_RIGHT,
    Up     = VK_UP,
    Down   = VK_DOWN,
    Escape = VK_ESCAPE,
    Space  = VK_SPACE,
    Enter  = VK_RETURN,
};

class InputManager {
public:
    static InputManager& Get();

    void Update();

    bool IsKeyDown    (KeyCode key) const;
    bool IsKeyPressed (KeyCode key) const; // first frame only
    bool IsKeyReleased(KeyCode key) const; // release frame only

    POINT GetMousePosition() const { return m_MousePos; }
    POINT GetMouseDelta()    const { return m_MouseDelta; }
    bool  IsMouseButtonDown(int button) const; // 0=left, 1=right, 2=middle

private:
    InputManager() = default;

    SHORT m_Current [256] = {};
    SHORT m_Previous[256] = {};
    POINT m_MousePos      = {};
    POINT m_PrevMousePos  = {};
    POINT m_MouseDelta    = {};
};

} // namespace VibeEngine
