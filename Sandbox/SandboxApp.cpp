#include "SandboxApp.h"
#include "Core/SceneManager.h"
#include "Core/Scene.h"
#include "Core/Transform.h"
#include "Renderer/Camera.h"
#include "Renderer/MeshRenderer.h"
#include "Renderer/LightManager.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/Rigidbody.h"
#include "Audio/AudioManager.h"
#include "Audio/AudioSource.h"
#include "Input/InputManager.h"
#include <string>
#include <algorithm>

using namespace VibeEngine;

// ---------------------------------------------------------------------------
// ImpactSoundPlayer — plays impact/bounce sounds on collision events.
// Pitch is slightly randomised each hit for variation.
// ---------------------------------------------------------------------------
class ImpactSoundPlayer : public VibeEngine::Component {
public:
    std::shared_ptr<VibeEngine::AudioClip> impactClip;
    std::shared_ptr<VibeEngine::AudioClip> bounceClip;

    // Consecutive-bounce counter: first contact = full impact,
    // subsequent lighter contacts = bounce with higher pitch.
    int bounceCount = 0;

    void OnCollisionEnter(VibeEngine::Rigidbody* /*other*/) override
    {
        auto& am = VibeEngine::AudioManager::Get();
        if (bounceCount == 0 && impactClip) {
            // First hit — deep thud
            am.PlayOneShot(impactClip, 0.90f, 1.0f);
        }
        else if (bounceClip) {
            // Subsequent bounces — higher pitch, lower volume each time
            float pitch  = 1.0f + bounceCount * 0.12f;
            float volume = (std::max)(0.05f, 0.75f - bounceCount * 0.15f);
            am.PlayOneShot(bounceClip, volume, pitch);
        }
        ++bounceCount;
    }

    void OnCollisionExit(VibeEngine::Rigidbody* /*other*/) override
    {
        // Reset after a brief separation so the next drop sounds fresh.
        // (We simply don't reset — accumulation per run is intentional.)
    }
};

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
    m_DX12.EndFrame();
    m_DX12.WaitForGPU();
    ResourceManager::Get().ReleaseUploadBuffers();

    // ---- Audio ---------------------------------------------------------------
    // AudioManager is already initialized by Application::Run() before OnInit.
    // Load clips once here; they survive scene restarts.
    {
        std::wstring soundDir = GetExeDir() + L"Sounds/";
        m_ImpactClip = AudioManager::Get().LoadClip(soundDir + L"impact.wav");
        m_BounceClip = AudioManager::Get().LoadClip(soundDir + L"bounce.wav");
        m_BgmClip    = AudioManager::Get().LoadClip(soundDir + L"bgm.wav");
    }

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

    // ---- Collision sound ----------------------------------------------------
    // ImpactSoundPlayer sits on the cube and reacts to OnCollisionEnter.
    auto* isp = cube->AddComponent<ImpactSoundPlayer>();
    isp->impactClip = m_ImpactClip;
    isp->bounceClip = m_BounceClip;

    // ---- BGM (looping ambient pad) ------------------------------------------
    // Attach to a dedicated "AudioBed" GameObject so it persists independently.
    auto* audioBed = scene->CreateGameObject("AudioBed");
    auto* bgmAS    = audioBed->AddComponent<AudioSource>();
    if (m_BgmClip) {
        bgmAS->SetClip       (m_BgmClip);
        bgmAS->SetLoop       (true);
        bgmAS->SetVolume     (0.35f);
        bgmAS->SetPlayOnStart(true);
    }
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
void SandboxApp::OnUpdate(float /*dt*/)
{
    if (InputManager::Get().IsKeyPressed(KeyCode::R))
        RestartScene();
}

// ---------------------------------------------------------------------------
void SandboxApp::OnRender()
{
    m_DX12.BeginFrame();
    if (auto* scene = SceneManager::Get().GetActiveScene())
        scene->Render();
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
    m_Pipeline.Destroy();
    ResourceManager::Get().Shutdown();
    m_DX12.Shutdown();
}
