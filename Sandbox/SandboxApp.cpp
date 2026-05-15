#include "SandboxApp.h"
#include "Core/SceneManager.h"
#include "Core/Scene.h"
#include "Core/Transform.h"
#include "Renderer/Camera.h"
#include "Renderer/MeshRenderer.h"
#include "Renderer/LightManager.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/Rigidbody.h"
#include "Input/InputManager.h"
#include <string>

using namespace VibeEngine;

// ---------------------------------------------------------------------------
static std::wstring GetExeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    return path.substr(0, path.find_last_of(L"\\/") + 1);
}

// ===========================================================================
SandboxApp::SandboxApp()
    : Application("VibeEngine Sandbox", 1280, 720)
{}

SandboxApp::~SandboxApp() = default;

// ---------------------------------------------------------------------------
void SandboxApp::OnInit()
{
    HWND hwnd = GetWindow()->GetHandle();

    // DX12
    if (!m_DX12.Initialize(hwnd, 1280, 720)) {
        MessageBoxA(hwnd, "DX12 init failed", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    ResourceManager::Get().Initialize(&m_DX12);

    // Upload geometry + textures
    m_DX12.BeginFrame();
    m_CubeMesh              = ResourceManager::Get().GetCube();
    auto texture            = ResourceManager::Get().GetOrLoadTexture(
                                  GetExeDir() + L"Textures/checkerboard.png");
    m_DX12.EndFrame();
    m_DX12.WaitForGPU();
    ResourceManager::Get().ReleaseUploadBuffers();

    LightManager::Get().Initialize(m_DX12.GetDevice());

    if (!m_Pipeline.Create(m_DX12.GetDevice(), m_DX12.GetBackBufferFormat())) {
        MessageBoxA(hwnd, "Pipeline create failed", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    Texture* tex = texture ? texture.get() : nullptr;

    // Dynamic-box material — warm orange tint
    m_BoxMaterial.SetPipeline(&m_Pipeline);
    m_BoxMaterial.SetTexture(tex);
    m_BoxMaterial.SetAlbedo({ 0.9f, 0.45f, 0.1f, 1.f });
    m_BoxMaterial.SetRoughness(0.4f);
    m_BoxMaterial.SetMetallic(0.0f);
    m_BoxMaterial.Create(m_DX12.GetDevice());

    // Floor material — neutral grey
    m_FloorMaterial.SetPipeline(&m_Pipeline);
    m_FloorMaterial.SetTexture(tex);
    m_FloorMaterial.SetAlbedo({ 0.55f, 0.55f, 0.55f, 1.f });
    m_FloorMaterial.SetRoughness(0.9f);
    m_FloorMaterial.SetMetallic(0.0f);
    m_FloorMaterial.Create(m_DX12.GetDevice());

    // Scene
    auto* scene = SceneManager::Get().CreateScene("Physics");
    SceneManager::Get().LoadScene("Physics");

    // Physics system
    PhysicsWorld::Get().Initialize();

    SetupScene(scene);
}

// ---------------------------------------------------------------------------
void SandboxApp::SetupScene(Scene* scene)
{
    const float aspect = static_cast<float>(GetWindow()->GetWidth())
                       / static_cast<float>(GetWindow()->GetHeight());

    // ---- Camera: elevated, looking down at the arena -------------------------
    auto* camGO = scene->CreateGameObject("Camera");
    camGO->AddComponent<Camera>();
    camGO->GetTransform()->SetPosition({ 0.f, 6.f, -14.f });
    m_Camera = camGO->GetComponent<Camera>();
    m_Camera->SetAspect(aspect);
    m_Camera->SetPitch(-20.f);

    // ---- Floor (static rigidbody) -------------------------------------------
    // Cube mesh verts are ±0.5, so scale (16,1,16) → top surface at y = 0.
    // Transform centred at (0, -0.5, 0); physics halfExtents match exactly.
    auto* floor = scene->CreateGameObject("Floor");
    floor->GetTransform()->SetPosition({ 0.f, -0.5f, 0.f });
    floor->GetTransform()->SetScale({ 16.f, 1.f, 16.f });

    auto* floorMR = floor->AddComponent<MeshRenderer>();
    floorMR->SetMesh(m_CubeMesh);
    floorMR->SetCommandList(m_DX12.GetCommandList());
    floorMR->SetMaterial(&m_FloorMaterial);
    floorMR->CreateConstantBuffer(m_DX12.GetDevice());

    auto* floorRB = floor->AddComponent<Rigidbody>();
    floorRB->SetBoxHalfExtents({ 8.f, 0.5f, 8.f });
    floorRB->SetStatic(true);
    floorRB->SetFriction(0.7f);

    // ---- Dynamic boxes -------------------------------------------------------
    // Two rows (z = ±2.5), four columns at staggered heights so they tumble
    // down asynchronously and make for an interesting first-second of physics.
    static const DirectX::XMFLOAT3 kSpawnPositions[8] = {
        { -4.5f, 2.0f, -2.5f },
        { -1.5f, 3.5f, -2.5f },
        {  1.5f, 5.0f, -2.5f },
        {  4.5f, 6.5f, -2.5f },
        { -4.5f, 2.0f,  2.5f },
        { -1.5f, 3.5f,  2.5f },
        {  1.5f, 5.0f,  2.5f },
        {  4.5f, 6.5f,  2.5f },
    };

    for (int i = 0; i < 8; ++i) {
        auto* box = scene->CreateGameObject("Box_" + std::to_string(i));
        box->GetTransform()->SetPosition(kSpawnPositions[i]);

        auto* mr = box->AddComponent<MeshRenderer>();
        mr->SetMesh(m_CubeMesh);
        mr->SetCommandList(m_DX12.GetCommandList());
        mr->SetMaterial(&m_BoxMaterial);
        mr->CreateConstantBuffer(m_DX12.GetDevice());

        auto* rb = box->AddComponent<Rigidbody>();
        rb->SetBoxHalfExtents({ 0.5f, 0.5f, 0.5f });
        rb->SetRestitution(0.3f);
        rb->SetFriction(0.6f);
    }
}

// ---------------------------------------------------------------------------
void SandboxApp::RestartScene()
{
    auto* scene = SceneManager::Get().GetActiveScene();
    if (!scene) return;

    // 1. Null our raw pointer before it becomes dangling.
    m_Camera = nullptr;

    // 2. Destroy all GameObjects; ~GameObject() calls Rigidbody::OnDestroy()
    //    which removes each body from Jolt before we tear down the system.
    scene->Clear();

    // 3. Full physics reset — re-Initialize gives us a fresh simulation.
    PhysicsWorld::Get().Shutdown();
    PhysicsWorld::Get().Initialize();

    // 4. Repopulate — Start() is deferred to the next scene->Update call.
    SetupScene(scene);
}

// ---------------------------------------------------------------------------
void SandboxApp::OnPreUpdate(float dt)
{
    // Step Jolt *before* scene Update so Rigidbody::Update reads this frame's positions.
    PhysicsWorld::Get().Update(dt);
}

// ---------------------------------------------------------------------------
void SandboxApp::OnUpdate(float dt)
{
    (void)dt;
    if (InputManager::Get().IsKeyPressed(KeyCode::R))
        RestartScene();
}

// ---------------------------------------------------------------------------
void SandboxApp::OnRender()
{
    m_DX12.BeginFrame();
    auto* scene = SceneManager::Get().GetActiveScene();
    if (scene) scene->Render();
    m_DX12.EndFrame();
}

// ---------------------------------------------------------------------------
void SandboxApp::OnResize(int w, int h)
{
    m_DX12.Resize(static_cast<UINT>(w), static_cast<UINT>(h));
    if (m_Camera)
        m_Camera->SetAspect(static_cast<float>(w) / static_cast<float>(h));
}

// ---------------------------------------------------------------------------
void SandboxApp::OnShutdown()
{
    // Clear scene first so Rigidbody::OnDestroy runs before physics shuts down.
    if (auto* scene = SceneManager::Get().GetActiveScene())
        scene->Clear();

    PhysicsWorld::Get().Shutdown();

    m_DX12.WaitForGPU();
    m_Pipeline.Destroy();
    ResourceManager::Get().Shutdown();
    m_DX12.Shutdown();
}
