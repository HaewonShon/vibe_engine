#include "SandboxApp.h"
#include "Core/SceneManager.h"
#include "Core/Transform.h"
#include "Renderer/Camera.h"
#include "Renderer/MeshRenderer.h"
#include "Renderer/LightManager.h"
#include "Input/InputManager.h"
#include <string>

using namespace VibeEngine;

static std::wstring GetExeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    return path.substr(0, path.find_last_of(L"\\/") + 1);
}

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

    m_DX12.BeginFrame();
    m_Mesh = std::make_shared<Mesh>(
        Mesh::CreateCube(m_DX12.GetDevice(), m_DX12.GetCommandList()));
    m_Texture.LoadFromFile(m_DX12.GetDevice(), m_DX12.GetCommandList(), m_DX12,
        GetExeDir() + L"Textures/checkerboard.png");
    m_DX12.EndFrame();
    m_DX12.WaitForGPU();
    m_Mesh->ReleaseUploadBuffers();
    m_Texture.ReleaseUploadBuffer();

    LightManager::Get().Initialize(m_DX12.GetDevice());

    if (!m_Pipeline.Create(m_DX12.GetDevice(), m_DX12.GetBackBufferFormat())) {
        MessageBoxA(hwnd, "Pipeline create failed", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    auto* scene = SceneManager::Get().CreateScene("Main");
    SceneManager::Get().LoadScene("Main");

    // Camera: start at (0, 0, -3), looking toward +Z (at the cube)
    auto* camGO = scene->CreateGameObject("Camera");
    camGO->AddComponent<Camera>();
    camGO->GetTransform()->SetPosition({ 0.f, 0.f, -3.f });
    camGO->GetComponent<Camera>()->SetAspect(1280.f / 720.f);

    // Cube
    m_Cube = scene->CreateGameObject("Cube");
    m_Cube->GetTransform()->SetRotation({ 25.f, 35.f, 0.f });

    auto* mr = m_Cube->AddComponent<MeshRenderer>();
    mr->SetMesh(m_Mesh);
    mr->SetPipeline(&m_Pipeline);
    mr->SetCommandList(m_DX12.GetCommandList());
    mr->SetTexture(&m_Texture);
    mr->CreateConstantBuffer(m_DX12.GetDevice());
}

void SandboxApp::OnUpdate(float dt)
{
    if (!m_Cube) return;
    auto* t   = m_Cube->GetTransform();
    auto& inp = InputManager::Get();

    // Arrow keys: rotate the cube
    const float speed = 90.0f;
    if (inp.IsKeyDown(KeyCode::Left))  t->Rotate(-speed * dt, 0.f, 0.f);
    if (inp.IsKeyDown(KeyCode::Right)) t->Rotate( speed * dt, 0.f, 0.f);
    if (inp.IsKeyDown(KeyCode::Up))    t->Rotate(0.f, -speed * dt, 0.f);
    if (inp.IsKeyDown(KeyCode::Down))  t->Rotate(0.f,  speed * dt, 0.f);
    if (inp.IsKeyDown(KeyCode::Space)) t->Rotate(0.f,  speed * dt, speed * dt * 0.3f);
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
