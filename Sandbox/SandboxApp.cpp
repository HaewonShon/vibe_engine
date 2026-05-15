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
#include "UI/UIRenderer.h"
#include "UI/UIPanel.h"
#include "UI/UIButton.h"
#include "Animation/AnimationClip.h"
#include "Animation/Animator.h"
#include <string>
#include <cstdio>

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
    : Application("VibeEngine — Physics Demo", 1280, 720)
{}

SandboxApp::~SandboxApp() = default;

// ---------------------------------------------------------------------------
void SandboxApp::OnInit()
{
    HWND hwnd = GetWindow()->GetHandle();

    if (!m_DX12.Initialize(hwnd, 1280, 720)) {
        MessageBoxA(hwnd, "DX12 init failed", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    ResourceManager::Get().Initialize(&m_DX12);

    // Upload geometry and textures once
    m_DX12.BeginFrame();
    m_CubeMesh = ResourceManager::Get().GetCube();
    auto texture = ResourceManager::Get().GetOrLoadTexture(
                       GetExeDir() + L"Textures/checkerboard.png");

    // Init UI renderer in the same upload pass (font atlas is uploaded to GPU)
    UIRenderer::Get().Initialize(
        m_DX12.GetDevice(), m_DX12.GetCommandList(),
        m_DX12.GetBackBufferFormat(), 1280, 720);

    m_DX12.EndFrame();
    m_DX12.WaitForGPU();
    ResourceManager::Get().ReleaseUploadBuffers();
    UIRenderer::Get().ReleaseUploadBuffer();

    LightManager::Get().Initialize(m_DX12.GetDevice());

    if (!m_Pipeline.Create(m_DX12.GetDevice(), m_DX12.GetBackBufferFormat())) {
        MessageBoxA(hwnd, "Pipeline create failed", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    Texture* tex = texture ? texture.get() : nullptr;

    // Cube material — vivid red-orange so it pops against the grey floor
    m_BoxMaterial.SetPipeline(&m_Pipeline);
    m_BoxMaterial.SetTexture(tex);
    m_BoxMaterial.SetAlbedo({ 0.95f, 0.30f, 0.10f, 1.f });
    m_BoxMaterial.SetRoughness(0.35f);
    m_BoxMaterial.SetMetallic(0.0f);
    m_BoxMaterial.Create(m_DX12.GetDevice());

    // Floor material — light grey
    m_FloorMaterial.SetPipeline(&m_Pipeline);
    m_FloorMaterial.SetTexture(tex);
    m_FloorMaterial.SetAlbedo({ 0.70f, 0.70f, 0.70f, 1.f });
    m_FloorMaterial.SetRoughness(0.85f);
    m_FloorMaterial.SetMetallic(0.0f);
    m_FloorMaterial.Create(m_DX12.GetDevice());

    auto* scene = SceneManager::Get().CreateScene("PhysicsDemo");
    SceneManager::Get().LoadScene("PhysicsDemo");

    PhysicsWorld::Get().Initialize();

    SetupScene(scene);
    SetupHUD();
}

// ---------------------------------------------------------------------------
// SetupHUD  — builds the 2D overlay once (survives scene restarts)
// ---------------------------------------------------------------------------
void SandboxApp::SetupHUD()
{
    m_Canvas.Clear();

    const float pad = 12.f;
    const float pw  = 240.f, ph = 100.f;

    // ---- Background panel (top-left) ----------------------------------------
    auto* panel         = m_Canvas.AddElement<UIPanel>();
    panel->SetRect(pad, pad, pw, ph);
    panel->backgroundColor  = { 0.05f, 0.05f, 0.08f, 0.78f };
    panel->borderColor      = { 0.50f, 0.60f, 0.80f, 0.90f };
    panel->borderWidth      = 1.5f;

    // Title
    auto* title = m_Canvas.AddElement<UILabel>();
    title->SetRect(pad + 10.f, pad + 8.f, pw - 20.f, 0.f);
    title->text  = "VibeEngine";
    title->color = { 0.65f, 0.80f, 1.00f, 1.f };
    title->scale = 1.4f;
    title->align = UILabel::Align::Left;

    // Subtitle
    auto* sub = m_Canvas.AddElement<UILabel>();
    sub->SetRect(pad + 10.f, pad + 36.f, pw - 20.f, 0.f);
    sub->text  = "Physics Demo";
    sub->color = { 0.85f, 0.85f, 0.85f, 1.f };
    sub->scale = 1.0f;

    // FPS counter (updated every frame in OnUpdate)
    m_FpsLabel = m_Canvas.AddElement<UILabel>();
    m_FpsLabel->SetRect(pad + 10.f, pad + 58.f, pw - 20.f, 0.f);
    m_FpsLabel->text  = "0.0 FPS";
    m_FpsLabel->color = { 0.60f, 1.00f, 0.60f, 1.f };
    m_FpsLabel->scale = 0.9f;

    // ---- Hint label (bottom-left) -------------------------------------------
    auto* hint = m_Canvas.AddElement<UILabel>();
    hint->SetRect(pad, static_cast<float>(GetWindow()->GetHeight()) - 30.f, 0.f, 0.f);
    hint->text  = "R = Restart simulation";
    hint->color = { 0.75f, 0.75f, 0.75f, 0.85f };
    hint->scale = 0.9f;

    // ---- Restart button (top-right corner) ----------------------------------
    const float bw = 120.f, bh = 32.f;
    const float bx = static_cast<float>(GetWindow()->GetWidth()) - bw - pad;
    auto* btn = m_Canvas.AddElement<UIButton>();
    btn->SetRect(bx, pad, bw, bh);
    btn->label = "Restart";
    btn->hwnd  = GetWindow()->GetHandle();
    btn->SetOnClick([this] { RestartScene(); });
}

// ---------------------------------------------------------------------------
// SetupScene
//
// Layout (Y-up):
//
//   y=6  ●  cube (1×1×1) — dynamic, starts here, falls under gravity
//        |
//        ↓  (gravity = -9.81 m/s²)
//        |
//   y=0  ▬▬▬▬▬▬▬▬▬▬  floor slab (10×0.2×10) — static
//
// Camera is offset to the right and above so we see the 3-D perspective of the fall.
// Press R at any time to reset the simulation.
// ---------------------------------------------------------------------------
void SandboxApp::SetupScene(Scene* scene)
{
    const float aspect = static_cast<float>(GetWindow()->GetWidth())
                       / static_cast<float>(GetWindow()->GetHeight());

    // ---- Camera ---------------------------------------------------------------
    // Position (5, 10, -10).  Target: scene centre ~(0, 3, 0).
    // Direction (-5,-7,10) -> yaw = atan2(-5,10) ~ -27 deg,
    //                          pitch: sin(pitch) = 7/sqrt(174) ~ 0.531 -> +32 deg.
    // Camera code: forward.y = -sin(pitchRad), so POSITIVE pitch = look DOWN.
    auto* camGO = scene->CreateGameObject("Camera");
    camGO->AddComponent<Camera>();
    camGO->GetTransform()->SetPosition({ 5.f, 10.f, -10.f });
    m_Camera = camGO->GetComponent<Camera>();
    m_Camera->SetAspect(aspect);
    m_Camera->SetYaw  (-27.f);   // turn left toward x = 0
    m_Camera->SetPitch(+32.f);   // look DOWN (positive = downward in this engine)

    // ---- Floor (static rigidbody) -------------------------------------------
    // Cube mesh scaled thin (0.2 units tall) so it reads as a flat plane.
    // Transform centred at y=-0.1 → top surface exactly at y=0.
    // Physics box halfExtents (5, 0.1, 5) matches the visual exactly.
    auto* floor = scene->CreateGameObject("Floor");
    floor->GetTransform()->SetPosition({ 0.f, -0.1f, 0.f });
    floor->GetTransform()->SetScale   ({ 10.f, 0.2f, 10.f });

    auto* floorMR = floor->AddComponent<MeshRenderer>();
    floorMR->SetMesh(m_CubeMesh);
    floorMR->SetCommandList(m_DX12.GetCommandList());
    floorMR->SetMaterial(&m_FloorMaterial);
    floorMR->CreateConstantBuffer(m_DX12.GetDevice());

    auto* floorRB = floor->AddComponent<Rigidbody>();
    floorRB->SetBoxHalfExtents({ 5.f, 0.1f, 5.f });
    floorRB->SetStatic     (true);
    floorRB->SetRestitution(0.4f);
    floorRB->SetFriction   (0.6f);

    // ---- Falling cube (dynamic rigidbody) -----------------------------------
    // Start height y=6; initial tilt so the cube tumbles realistically on impact.
    // restitution=0.5 → bounces to ~50% of drop height before settling.
    auto* cube = scene->CreateGameObject("Cube");
    cube->GetTransform()->SetPosition({ 0.f, 6.f, 0.f });
    cube->GetTransform()->SetRotation({ 25.f, 40.f, 10.f }); // degrees (X, Y, Z euler)

    auto* cubeMR = cube->AddComponent<MeshRenderer>();
    cubeMR->SetMesh(m_CubeMesh);
    cubeMR->SetCommandList(m_DX12.GetCommandList());
    cubeMR->SetMaterial(&m_BoxMaterial);
    cubeMR->CreateConstantBuffer(m_DX12.GetDevice());

    auto* cubeRB = cube->AddComponent<Rigidbody>();
    cubeRB->SetBoxHalfExtents({ 0.5f, 0.5f, 0.5f });
    cubeRB->SetMass       (1.f);
    cubeRB->SetRestitution(0.5f);
    cubeRB->SetFriction   (0.5f);

    // ---- Animated orb (no physics, pure keyframe animation) ----------------
    //
    // A small grey cube hovers to the left of the scene, bobbing up/down
    // with SineInOut easing and slowly spinning.  Demonstrates the Animator
    // Component + AnimationClip system running alongside physics.
    //
    // Clip layout (4-second loop):
    //   position : y bobs 3.0 ↑ 4.2 ↓ 3.0 (2-s period, SineInOut)
    //   rotation : Y-axis full spin every 4 s (linear)
    //   scale    : slight pulse 0.35 ↔ 0.50 in sync with the bob
    auto* orb = scene->CreateGameObject("Orb");
    orb->GetTransform()->SetPosition({ -3.5f, 3.0f, 0.f });
    orb->GetTransform()->SetScale   ({ 0.35f, 0.35f, 0.35f });

    auto* orbMR = orb->AddComponent<MeshRenderer>();
    orbMR->SetMesh       (m_CubeMesh);
    orbMR->SetCommandList(m_DX12.GetCommandList());
    orbMR->SetMaterial   (&m_FloorMaterial);
    orbMR->CreateConstantBuffer(m_DX12.GetDevice());

    auto clip = std::make_shared<AnimationClip>("orb_float");
    clip->SetDuration(4.f);

    // Position: bob up and down (2-second period → two full cycles in 4 s)
    clip->AddPositionKey(0.f, { -3.5f, 3.0f, 0.f })
        .AddPositionKey(1.f, { -3.5f, 4.2f, 0.f }, EasingMode::SineInOut)
        .AddPositionKey(2.f, { -3.5f, 3.0f, 0.f }, EasingMode::SineInOut)
        .AddPositionKey(3.f, { -3.5f, 4.2f, 0.f }, EasingMode::SineInOut)
        .AddPositionKey(4.f, { -3.5f, 3.0f, 0.f }, EasingMode::SineInOut);

    // Rotation: one full Y-spin per loop (linear = constant angular speed)
    clip->AddRotationKey(0.f, {  0.f,   0.f, 0.f })
        .AddRotationKey(4.f, {  0.f, 360.f, 0.f });

    // Scale: pulse in sync with the bob (0.35 at low, 0.50 at high)
    clip->AddScaleKey(0.f, { 0.35f, 0.35f, 0.35f })
        .AddScaleKey(1.f, { 0.50f, 0.50f, 0.50f }, EasingMode::SineInOut)
        .AddScaleKey(2.f, { 0.35f, 0.35f, 0.35f }, EasingMode::SineInOut)
        .AddScaleKey(3.f, { 0.50f, 0.50f, 0.50f }, EasingMode::SineInOut)
        .AddScaleKey(4.f, { 0.35f, 0.35f, 0.35f }, EasingMode::SineInOut);

    auto* anim = orb->AddComponent<Animator>();
    anim->Play(clip, /*loop=*/true);
}

// ---------------------------------------------------------------------------
void SandboxApp::RestartScene()
{
    auto* scene = SceneManager::Get().GetActiveScene();
    if (!scene) return;

    m_Camera = nullptr;
    scene->Clear();                       // ~GameObject() → Rigidbody::OnDestroy
    PhysicsWorld::Get().Shutdown();
    PhysicsWorld::Get().Initialize();
    SetupScene(scene);
}

// ---------------------------------------------------------------------------
void SandboxApp::OnPreUpdate(float dt)
{
    // Step physics before scene Update so Rigidbody::Update reads current positions.
    PhysicsWorld::Get().Update(dt);
}

// ---------------------------------------------------------------------------
void SandboxApp::OnUpdate(float dt)
{
    if (InputManager::Get().IsKeyPressed(KeyCode::R))
        RestartScene();

    // Update UI widgets (button hover/click state, etc.)
    m_Canvas.Update(dt);

    // Refresh FPS label
    if (m_FpsLabel && dt > 0.f) {
        static float fpsSmooth = 0.f;
        fpsSmooth = fpsSmooth * 0.9f + (1.f / dt) * 0.1f;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f FPS", fpsSmooth);
        m_FpsLabel->text = buf;
    }
}

// ---------------------------------------------------------------------------
void SandboxApp::OnRender()
{
    m_DX12.BeginFrame();

    if (auto* scene = SceneManager::Get().GetActiveScene())
        scene->Render();

    // 2D UI overlay — rendered on top of the 3D scene
    if (UIRenderer::Get().IsInitialized()) {
        UIRenderer::Get().BeginPass(m_DX12.GetCommandList(),
                                    static_cast<UINT>(GetWindow()->GetWidth()),
                                    static_cast<UINT>(GetWindow()->GetHeight()));
        m_Canvas.Draw(UIRenderer::Get());
        UIRenderer::Get().EndPass();
    }

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
    if (auto* scene = SceneManager::Get().GetActiveScene())
        scene->Clear();           // ensure Rigidbody::OnDestroy runs before Shutdown
    PhysicsWorld::Get().Shutdown();

    m_DX12.WaitForGPU();
    UIRenderer::Get().Shutdown();
    m_Pipeline.Destroy();
    ResourceManager::Get().Shutdown();
    m_DX12.Shutdown();
}
