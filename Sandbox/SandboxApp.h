#pragma once
#include "Core/Application.h"
#include "Core/ResourceManager.h"
#include "Renderer/DX12Context.h"
#include "Renderer/BasicPipeline.h"
#include "Renderer/Material.h"
#include "Renderer/Camera.h"
#include "Core/GameObject.h"

class SandboxApp : public VibeEngine::Application {
public:
    SandboxApp();
    ~SandboxApp() override;

    void OnInit()               override;
    void OnUpdate(float dt)     override;
    void OnRender()             override;
    void OnShutdown()           override;
    void OnResize(int w, int h) override;

private:
    VibeEngine::DX12Context   m_DX12;
    VibeEngine::BasicPipeline m_Pipeline;

    VibeEngine::Material m_CubeMaterial;
    VibeEngine::Material m_FloorMaterial;

    VibeEngine::Camera*     m_Camera = nullptr;
    VibeEngine::GameObject* m_Cube   = nullptr;
    VibeEngine::GameObject* m_Floor  = nullptr;
};
