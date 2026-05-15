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

    // Initialize resource layer
    ResourceManager::Get().Initialize(&m_DX12);

    // Upload all geometry + textures in one command list submission
    m_DX12.BeginFrame();
    auto cubeMesh  = ResourceManager::Get().GetCube();
    auto planeMesh = ResourceManager::Get().GetPlane();
    auto texture   = ResourceManager::Get().GetOrLoadTexture(
                         GetExeDir() + L"Textures/checkerboard.png");
    m_DX12.EndFrame();
    m_DX12.WaitForGPU();
    ResourceManager::Get().ReleaseUploadBuffers();

    LightManager::Get().Initialize(m_DX12.GetDevice());

    if (!m_Pipeline.Create(m_DX12.GetDevice(), m_DX12.GetBackBufferFormat())) {
        MessageBoxA(hwnd, "Pipeline create failed", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Materials — texture* is valid as long as ResourceManager is alive
    Texture* tex = texture ? texture.get() : nullptr;

    m_CubeMaterial.SetPipeline(&m_Pipeline);
    m_CubeMaterial.SetTexture(tex);
    m_CubeMaterial.SetAlbedo({ 1.f, 1.f, 1.f, 1.f });
    m_CubeMaterial.SetRoughness(0.3f);
    m_CubeMaterial.SetMetallic(0.0f);
    m_CubeMaterial.Create(m_DX12.GetDevice());

    m_FloorMaterial.SetPipeline(&m_Pipeline);
    m_FloorMaterial.SetTexture(tex);
    m_FloorMaterial.SetAlbedo({ 0.6f, 0.6f, 0.6f, 1.f });
    m_FloorMaterial.SetRoughness(0.9f);
    m_FloorMaterial.SetMetallic(0.0f);
    m_FloorMaterial.Create(m_DX12.GetDevice());

    auto* scene = SceneManager::Get().CreateScene("Main");
    SceneManager::Get().LoadScene("Main");

    // Camera: elevated position, slight downward pitch to see cube + floor
    auto* camGO = scene->CreateGameObject("Camera");
    camGO->AddComponent<Camera>();
    camGO->GetTransform()->SetPosition({ 0.f, 1.2f, -4.f });
    m_Camera = camGO->GetComponent<Camera>();
    m_Camera->SetAspect(1280.f / 720.f);
    m_Camera->SetPitch(-15.f);

    // Cube: 45-degree corner view
    m_Cube = scene->CreateGameObject("Cube");
    m_Cube->GetTransform()->SetRotation({ 30.f, 45.f, 0.f });

    auto* mr = m_Cube->AddComponent<MeshRenderer>();
    mr->SetMesh(cubeMesh);
    mr->SetCommandList(m_DX12.GetCommandList());
    mr->SetMaterial(&m_CubeMaterial);
    mr->CreateConstantBuffer(m_DX12.GetDevice());

    // Floor plane
    m_Floor = scene->CreateGameObject("Floor");
    m_Floor->GetTransform()->SetPosition({ 0.f, -0.5f, 0.f });
    m_Floor->GetTransform()->SetScale({ 3.f, 1.f, 3.f });

    auto* floorMR = m_Floor->AddComponent<MeshRenderer>();
    floorMR->SetMesh(planeMesh);
    floorMR->SetCommandList(m_DX12.GetCommandList());
    floorMR->SetMaterial(&m_FloorMaterial);
    floorMR->CreateConstantBuffer(m_DX12.GetDevice());
}

void SandboxApp::OnUpdate(float dt)
{
    if (!m_Cube) return;
    auto* t   = m_Cube->GetTransform();
    auto& inp = InputManager::Get();

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

void SandboxApp::OnResize(int w, int h)
{
    m_DX12.Resize(static_cast<UINT>(w), static_cast<UINT>(h));
    if (m_Camera)
        m_Camera->SetAspect(static_cast<float>(w) / static_cast<float>(h));
}

void SandboxApp::OnShutdown()
{
    m_DX12.WaitForGPU();
    m_Pipeline.Destroy();
    ResourceManager::Get().Shutdown();
    m_DX12.Shutdown();
}
