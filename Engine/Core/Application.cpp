#include "Application.h"
#include "../Input/InputManager.h"
#include "../Core/SceneManager.h"

namespace VibeEngine {

Application* Application::s_Instance = nullptr;

Application::Application(const std::string& title, int width, int height)
{
    s_Instance = this;

    WindowProps props{ title, width, height };
    m_Window = std::make_unique<Window>(props);

    QueryPerformanceFrequency(&m_Frequency);
    QueryPerformanceCounter(&m_LastTime);
}

Application::~Application()
{
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

        InputManager::Get().Update();

        // Update active scene
        auto* scene = SceneManager::Get().GetActiveScene();
        if (scene) scene->Update(dt);

        OnUpdate(dt);
        OnRender();

        // FPS counter
        fpsAccum += dt;
        ++fpsFrames;
        if (fpsAccum >= 0.5f) {
            float fps = static_cast<float>(fpsFrames) / fpsAccum;
            char buf[128];
            snprintf(buf, sizeof(buf), "VibeEngine | %.1f FPS", fps);
            m_Window->SetTitle(buf);
            fpsAccum  = 0.0f;
            fpsFrames = 0;
        }
    }
}

} // namespace VibeEngine
