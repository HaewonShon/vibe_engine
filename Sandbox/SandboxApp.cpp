#include "SandboxApp.h"
#include "Core/SceneManager.h"
#include "Core/Transform.h"
#include "Renderer/Camera.h"
#include "Renderer/MeshRenderer.h"
#include "Input/InputManager.h"

using namespace VibeEngine;

SandboxApp::SandboxApp()
    : Application("VibeEngine Sandbox", 1280, 720)
{}

SandboxApp::~SandboxApp() = default;

void SandboxApp::OnInit()
{
    HWND hwnd = GetWindow()->GetHandle();
    if (!m_DX12.Initialize(hwnd, 1280, 720)) {
        MessageBoxA(hwnd, "DX12 init failed", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Build initial mesh via command list (before first render)
    m_DX12.BeginFrame();
    m_Mesh = std::make_shared<Mesh>(
        Mesh::CreateTriangle(m_DX12.GetDevice(), m_DX12.GetCommandList()));
    m_DX12.EndFrame();
    m_DX12.WaitForGPU();
    m_Mesh->ReleaseUploadBuffers();

    if (!m_Pipeline.Create(m_DX12.GetDevice(), m_DX12.GetBackBufferFormat())) {
        MessageBoxA(hwnd, "Pipeline create failed", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Scene setup
    auto* scene = SceneManager::Get().CreateScene("Main");
    SceneManager::Get().LoadScene("Main");

    auto* camGO = scene->CreateGameObject("Camera");
    camGO->AddComponent<Camera>();
    camGO->GetTransform()->SetPosition({ 0.f, 0.f, -3.f }); // pull back so triangle at z=0 is visible
    auto* cam = camGO->GetComponent<Camera>();
    cam->SetAspect(1280.f / 720.f);

    // Triangle GameObject
    m_Triangle = scene->CreateGameObject("Triangle");
    auto* mr = m_Triangle->AddComponent<MeshRenderer>();
    mr->SetMesh(m_Mesh);
    mr->SetPipeline(&m_Pipeline);
    mr->SetCommandList(m_DX12.GetCommandList());
    mr->CreateConstantBuffer(m_DX12.GetDevice());
}

void SandboxApp::OnUpdate(float dt)
{
    if (!m_Triangle) return;
    auto* t = m_Triangle->GetTransform();
    auto& input = InputManager::Get();

    const float speed = 90.0f; // degrees per second

    if (input.IsKeyDown(KeyCode::Left))  t->Rotate(-speed * dt, 0.f, 0.f);
    if (input.IsKeyDown(KeyCode::Right)) t->Rotate( speed * dt, 0.f, 0.f);
    if (input.IsKeyDown(KeyCode::Up))    t->Rotate(0.f, -speed * dt, 0.f);
    if (input.IsKeyDown(KeyCode::Down))  t->Rotate(0.f,  speed * dt, 0.f);
    if (input.IsKeyDown(KeyCode::Space)) t->Rotate(speed * dt * 0.5f, speed * dt, 0.f);
}

void SandboxApp::OnRender()
{
    m_DX12.BeginFrame();

    auto* scene = SceneManager::Get().GetActiveScene();
    if (scene) scene->Render();

    m_DX12.EndFrame();
}

void SandboxApp::OnShutdown()
{
    m_DX12.WaitForGPU();
    m_Pipeline.Destroy();
    m_Mesh.reset();
    m_DX12.Shutdown();
}
