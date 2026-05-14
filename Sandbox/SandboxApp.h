#pragma once
#include "Core/Application.h"
#include "Renderer/DX12Context.h"
#include "Renderer/BasicPipeline.h"
#include "Renderer/Mesh.h"
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
    VibeEngine::DX12Context  m_DX12;
    VibeEngine::BasicPipeline m_Pipeline;
    std::shared_ptr<VibeEngine::Mesh> m_Mesh;

    VibeEngine::GameObject* m_Triangle = nullptr;
    bool m_MeshUploaded = false;
};
