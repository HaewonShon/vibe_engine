#pragma once
#include "Window.h"
#include <memory>
#include <string>

namespace VibeEngine {

class Application {
public:
    Application(const std::string& title, int width = 1280, int height = 720);
    virtual ~Application();

    void Run();

    virtual void OnInit()               {}
    virtual void OnPreUpdate(float /*dt*/) {}  // called before scene Update; use for physics step
    virtual void OnUpdate(float /*dt*/) {}
    virtual void OnRender()             {}
    virtual void OnShutdown()           {}
    virtual void OnResize(int /*w*/, int /*h*/) {}

    Window* GetWindow() const { return m_Window.get(); }
    static Application* Get() { return s_Instance; }

protected:
    std::unique_ptr<Window> m_Window;

private:
    void MainLoop();

    bool          m_Running   = true;
    LARGE_INTEGER m_LastTime  = {};
    LARGE_INTEGER m_Frequency = {};

    static Application* s_Instance;
};

} // namespace VibeEngine
