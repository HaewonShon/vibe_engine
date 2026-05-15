#pragma once
#include "Core/Application.h"
#include "Core/ResourceManager.h"
#include "Core/Scene.h"
#include "Renderer/DX12Context.h"
#include "Renderer/BasicPipeline.h"
#include "Renderer/Material.h"
#include "Renderer/Camera.h"
#include "Renderer/Mesh.h"
#include "Core/GameObject.h"
#include "UI/UICanvas.h"
#include "UI/UILabel.h"
#include <memory>

class SandboxApp : public VibeEngine::Application {
public:
    SandboxApp();
    ~SandboxApp() override;

    void OnInit()                override;
    void OnPreUpdate(float dt)   override;   // steps Jolt physics
    void OnUpdate(float dt)      override;   // input / scene restart (R)
    void OnRender()              override;
    void OnShutdown()            override;
    void OnResize(int w, int h)  override;

private:
    // Build the physics scene from scratch into an already-cleared scene.
    void SetupScene(VibeEngine::Scene* scene);

    // Null raw pointers → Clear scene (triggers Rigidbody::OnDestroy) →
    // Shutdown + re-Initialize physics → SetupScene.
    void RestartScene();

    // ---- Graphics -----------------------------------------------------------
    VibeEngine::DX12Context   m_DX12;
    VibeEngine::BasicPipeline m_Pipeline;

    VibeEngine::Material m_BoxMaterial;
    VibeEngine::Material m_FloorMaterial;

    // Cached mesh — lifetime tied to ResourceManager
    std::shared_ptr<VibeEngine::Mesh> m_CubeMesh;

    // ---- UI -----------------------------------------------------------------
    VibeEngine::UICanvas m_Canvas;
    VibeEngine::UILabel* m_FpsLabel  = nullptr;   // updated each frame
    void SetupHUD();

    // ---- Live scene state ---------------------------------------------------
    VibeEngine::Camera* m_Camera = nullptr;   // raw ptr; nulled before Clear
};
