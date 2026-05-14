#include "Window.h"
#include <stdexcept>

namespace VibeEngine {

Window::Window(const WindowProps& props)
    : m_Width(props.Width), m_Height(props.Height)
{
    m_HInstance = GetModuleHandleA(nullptr);

    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = m_HInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExA(&wc);

    RECT rc = { 0, 0, m_Width, m_Height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    // Find the rightmost monitor
    struct MonitorSearch { RECT best; int maxLeft; };
    MonitorSearch ms = { {}, INT_MIN };
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto* s = reinterpret_cast<MonitorSearch*>(lp);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoA(hm, &mi);
        if (mi.rcMonitor.left > s->maxLeft) {
            s->maxLeft = mi.rcMonitor.left;
            s->best    = mi.rcWork;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ms));

    int posX = ms.best.left + (ms.best.right  - ms.best.left - winW) / 2;
    int posY = ms.best.top  + (ms.best.bottom - ms.best.top  - winH) / 2;

    m_Hwnd = CreateWindowExA(
        0, CLASS_NAME, props.Title.c_str(),
        WS_OVERLAPPEDWINDOW,
        posX, posY, winW, winH,
        nullptr, nullptr, m_HInstance, this);

    if (!m_Hwnd)
        throw std::runtime_error("Failed to create window");

    ShowWindow(m_Hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_Hwnd);
}

Window::~Window()
{
    if (m_Hwnd) {
        DestroyWindow(m_Hwnd);
        m_Hwnd = nullptr;
    }
    UnregisterClassA(CLASS_NAME, m_HInstance);
}

bool Window::PollEvents()
{
    MSG msg = {};
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_ShouldClose = true;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return !m_ShouldClose;
}

void Window::SetTitle(const std::string& title)
{
    SetWindowTextA(m_Hwnd, title.c_str());
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Window* self = nullptr;

    if (msg == WM_NCCREATE) {
        CREATESTRUCTA* cs = reinterpret_cast<CREATESTRUCTA*>(lp);
        self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    }

    if (self) {
        if (self->m_EventCallback)
            self->m_EventCallback(msg, wp, lp);

        switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            self->m_ShouldClose = true;
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcA(hwnd, msg, wp, lp);
}

} // namespace VibeEngine
