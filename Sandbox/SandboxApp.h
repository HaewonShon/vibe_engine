#pragma once
#include "Core/Application.h"
#include "Renderer/DX12Context.h"
#include "Renderer/BasicPipeline.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Renderer/Material.h"
#include "Core/GameObject.h"
#include <memory>

class SandboxApp : public VibeEngine::Application {
public:
    SandboxApp();
    ~SandboxApp() override;

    void OnInit()           override;
    void OnUpdate(float dt) override;
    void OnRender()         override;
    void OnShutdown()       override;

private:
    VibeEngine::DX12Context   m_DX12;
    VibeEngine::BasicPipeline m_Pipeline;
    std::shared_ptr<VibeEngine::Mesh> m_Mesh;
    std::shared_ptr<VibeEngine::Mesh> m_PlaneMesh;
    VibeEngine::Texture       m_Texture;

    VibeEngine::Material m_CubeMaterial;
    VibeEngine::Material m_FloorMaterial;

    VibeEngine::GameObject* m_Cube  = nullptr;
    VibeEngine::GameObject* m_Floor = nullptr;
};
