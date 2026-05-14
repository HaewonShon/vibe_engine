#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>
#include <functional>

namespace VibeEngine {

struct WindowProps {
    std::string Title;
    int Width;
    int Height;
};

class Window {
public:
    using EventCallback = std::function<void(UINT, WPARAM, LPARAM)>;

    explicit Window(const WindowProps& props);
    ~Window();

    bool PollEvents();
    void SetTitle(const std::string& title);
    void SetEventCallback(EventCallback cb) { m_EventCallback = cb; }

    HWND GetHandle()    const { return m_Hwnd; }
    int  GetWidth()     const { return m_Width; }
    int  GetHeight()    const { return m_Height; }
    bool ShouldClose()  const { return m_ShouldClose; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND          m_Hwnd        = nullptr;
    HINSTANCE     m_HInstance   = nullptr;
    int           m_Width       = 0;
    int           m_Height      = 0;
    bool          m_ShouldClose = false;
    EventCallback m_EventCallback;

    static constexpr const char* CLASS_NAME = "VibeEngineWindow";
};

} // namespace VibeEngine
