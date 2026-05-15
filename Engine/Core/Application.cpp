#include "Application.h"
#include "../Input/InputManager.h"
#include "../Core/SceneManager.h"
#include "../Core/Profiler.h"
#include "../Core/Log.h"

namespace VibeEngine {

Application* Application::s_Instance = nullptr;

Application::Application(const std::string& title, int width, int height)
{
    s_Instance = this;

    Log::Get().Init();           // open log file before anything else
    LOG_INFO("VibeEngine starting — %s (%dx%d)", title.c_str(), width, height);

    WindowProps props{ title, width, height };
    m_Window = std::make_unique<Window>(props);

    // Forward WM_SIZE to the virtual OnResize hook
    m_Window->SetEventCallback([this](UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_SIZE && wp != SIZE_MINIMIZED) {
            int w = static_cast<int>(LOWORD(lp));
            int h = static_cast<int>(HIWORD(lp));
            if (w > 0 && h > 0) OnResize(w, h);
        }
    });

    QueryPerformanceFrequency(&m_Frequency);
    QueryPerformanceCounter(&m_LastTime);
}

Application::~Application()
{
    LOG_INFO("VibeEngine shutting down.");
    Log::Get().Shutdown();
    s_Instance = nullptr;
}

void Application::Run()
{
    OnInit();
    MainLoop();
    OnShutdown();
}

void Application::MainLoop()
{
    float fpsAccum  = 0.0f;
    int   fpsFrames = 0;

    LOG_INFO("Application loop started.");

    while (m_Running) {
        if (!m_Window->PollEvents()) {
            m_Running = false;
            break;
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = static_cast<float>(now.QuadPart - m_LastTime.QuadPart)
                 / static_cast<float>(m_Frequency.QuadPart);
        m_LastTime = now;

        Profiler::Get().BeginFrame();
        if (m_Window->IsFocused())
            InputManager::Get().Update();

        {
            PROFILE_SCOPE("Update");
            auto* scene = SceneManager::Get().GetActiveScene();
            if (scene) scene->Update(dt);
            OnUpdate(dt);
        }

        {
            PROFILE_SCOPE("Render");
            OnRender();
        }

        // FPS + profiler title bar
        fpsAccum += dt;
        ++fpsFrames;
        if (fpsAccum >= 0.5f) {
            float fps      = static_cast<float>(fpsFrames) / fpsAccum;
            float updateMs = Profiler::Get().GetSmoothedMs("Update");
            float renderMs = Profiler::Get().GetSmoothedMs("Render");
            char buf[256];
            snprintf(buf, sizeof(buf),
                "VibeEngine | %.1f FPS | Update: %.2f ms | Render: %.2f ms",
                fps, updateMs, renderMs);
            m_Window->SetTitle(buf);
            fpsAccum  = 0.0f;
            fpsFrames = 0;
        }
    }

    LOG_INFO("Application loop ended.");
}

} // namespace VibeEngine
