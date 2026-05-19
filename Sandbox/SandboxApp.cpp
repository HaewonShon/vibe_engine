#include "SandboxApp.h"
#include "Core/SceneManager.h"
#include "Core/Scene.h"
#include "Core/Transform.h"
#include "Core/SceneSerializer.h"
#include "Core/Log.h"
#include "Renderer/Camera.h"
#include "Renderer/DX12Helpers.h"
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
#include "Animation/HierarchicalAnimationClip.h"
#include "Animation/HierarchicalAnimator.h"
#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder* + DockBuilderGetNode
#include <functional>
#include <string>
#include <cstdio>
#include <algorithm>  // std::min

using namespace VibeEngine;

// ---------------------------------------------------------------------------
// TransformCmd — records a single Transform change for Undo/Redo.
// Captures position + rotation + scale together to handle gizmo drags
// (which may combine all three) as a single undoable step.
// ---------------------------------------------------------------------------
struct TransformCmd : ICommand {
    GameObject*           go  = nullptr;
    DirectX::XMFLOAT3 oldP{}, oldR{}, oldS{};
    DirectX::XMFLOAT3 newP{}, newR{}, newS{};

    void Execute() override {
        if (!go) return;
        auto* t = go->GetTransform();
        t->SetPosition(newP); t->SetRotation(newR); t->SetScale(newS);
    }
    void Undo() override {
        if (!go) return;
        auto* t = go->GetTransform();
        t->SetPosition(oldP); t->SetRotation(oldR); t->SetScale(oldS);
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

static std::string WideToNarrow(const std::wstring& ws)
{
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1,
                                  nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1,
                        s.data(), len, nullptr, nullptr);
    return s;
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
    m_CubeMesh  = ResourceManager::Get().GetCube();
    m_FloorMesh = ResourceManager::Get().GetGrid(16); // 16x16 subdivided — no diagonal seam
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

    // BloomPass must be initialized before the main pipeline so we can pass
    // the HDR render target format (R16G16B16A16_FLOAT) to BasicPipeline::Create().
    if (!m_BloomPass.Initialize(m_DX12.GetDevice(), m_DX12,
                                 m_DX12.GetWidth(), m_DX12.GetHeight())) {
        MessageBoxA(hwnd, "BloomPass init failed", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Scene now renders into the HDR float RT — pass that format to the PSO.
    if (!m_Pipeline.Create(m_DX12.GetDevice(), BloomPass::HDRFormat)) {
        MessageBoxA(hwnd, "Pipeline create failed", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Shadow map: 2048x2048 depth texture + SRV in the shared heap.
    // Must be initialized after DX12 is up but before any scene setup so that
    // MeshRenderers can receive the pointer during SetupScene().
    if (!m_ShadowMap.Initialize(m_DX12.GetDevice(), m_DX12)) {
        MessageBoxA(hwnd, "ShadowMap init failed", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    // Light direction matches LightManager default: upper-right-front.
    m_ShadowMap.SetLightDirection({ 0.577f, 0.577f, 0.577f });
    // Scene footprint: floor is 30 units wide; use 35 for a safe margin.
    m_ShadowMap.SetSceneBounds(35.0f, 100.0f);

    if (!m_ShadowPipeline.Create(m_DX12.GetDevice())) {
        MessageBoxA(hwnd, "ShadowPipeline create failed", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // IBL map + flat normal + SSAO: all uploaded in the same GPU frame.
    // IBL: procedural sky gradient (irradiance, specular, BRDF LUT).
    // Flat normal: 1x1 RGBA8 (128,128,255,255) = tangent-space neutral normal.
    // SSAO: creates AO textures and uploads the 4×4 noise texture.
    {
        m_DX12.BeginFrame();

        if (!m_IBLMap.Initialize(m_DX12.GetDevice(), m_DX12,
                                  m_DX12.GetCommandList())) {
            MessageBoxA(hwnd, "IBLMap init failed", "Error", MB_OK | MB_ICONERROR);
            return;
        }

        // (128,128,255,255) decodes to tangent-space (0,0,1) — points straight up,
        // meaning "no perturbation" which preserves the geometric normal.
        const uint8_t flatPixel[4] = { 128, 128, 255, 255 };
        if (!m_FlatNormal.LoadFromPixels(m_DX12.GetDevice(), m_DX12.GetCommandList(),
                                         m_DX12, flatPixel, 1, 1)) {
            MessageBoxA(hwnd, "FlatNormal create failed", "Error", MB_OK | MB_ICONERROR);
            return;
        }

        if (!m_SSAOPass.Initialize(m_DX12.GetDevice(), m_DX12,
                                    m_DX12.GetCommandList(),
                                    m_DX12.GetWidth(), m_DX12.GetHeight())) {
            MessageBoxA(hwnd, "SSAOPass init failed", "Error", MB_OK | MB_ICONERROR);
            return;
        }

        m_DX12.EndFrame();
        m_DX12.WaitForGPU();
        m_IBLMap.ReleaseUploadBuffers();
        m_FlatNormal.ReleaseUploadBuffer();
        m_SSAOPass.ReleaseUploadBuffers();
    }

    // Set initial screen size for SSAO UV lookup in Basic.hlsl
    LightManager::Get().SetScreenSize(
        { static_cast<float>(m_DX12.GetWidth()),
          static_cast<float>(m_DX12.GetHeight()) });

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

    // Skin material — warm peach for skeletal bone segments
    m_SkinMaterial.SetPipeline(&m_Pipeline);
    m_SkinMaterial.SetTexture(tex);
    m_SkinMaterial.SetAlbedo({ 0.88f, 0.65f, 0.48f, 1.f });
    m_SkinMaterial.SetRoughness(0.60f);
    m_SkinMaterial.SetMetallic(0.0f);
    m_SkinMaterial.Create(m_DX12.GetDevice());

    // Joint material — bright gold to highlight pivot points
    m_JointMaterial.SetPipeline(&m_Pipeline);
    m_JointMaterial.SetTexture(tex);
    m_JointMaterial.SetAlbedo({ 1.00f, 0.80f, 0.10f, 1.f });
    m_JointMaterial.SetRoughness(0.25f);
    m_JointMaterial.SetMetallic(0.7f);
    m_JointMaterial.Create(m_DX12.GetDevice());

    // Save path — <exedir>saves\PhysicsDemo.json
    m_SavePath = WideToNarrow(GetExeDir()) + "saves\\PhysicsDemo.json";

    auto* scene = SceneManager::Get().CreateScene("PhysicsDemo");
    SceneManager::Get().LoadScene("PhysicsDemo");

    PhysicsWorld::Get().Initialize();

    SetupScene(scene);
    SetupHUD();

    // GPU profiler — timestamp queries, frame-buffered readback
    m_GPUProfiler.Initialize(m_DX12.GetDevice(), m_DX12.GetCommandQueue());

    // ImGui debug overlay — must be last (font upload happens on first Begin())
    m_ImGuiLayer.Init(&m_DX12, hwnd);
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

    // Subtitle — stored so SwitchDemo() can update it
    m_SubtitleLabel = m_Canvas.AddElement<UILabel>();
    m_SubtitleLabel->SetRect(pad + 10.f, pad + 36.f, pw - 20.f, 0.f);
    m_SubtitleLabel->text  = "Physics Demo";
    m_SubtitleLabel->color = { 0.85f, 0.85f, 0.85f, 1.f };
    m_SubtitleLabel->scale = 1.0f;

    // FPS counter (updated every frame in OnUpdate)
    m_FpsLabel = m_Canvas.AddElement<UILabel>();
    m_FpsLabel->SetRect(pad + 10.f, pad + 58.f, pw - 20.f, 0.f);
    m_FpsLabel->text  = "0.0 FPS";
    m_FpsLabel->color = { 0.60f, 1.00f, 0.60f, 1.f };
    m_FpsLabel->scale = 0.9f;

    // ---- Clip-name label (just below panel, skeletal demo) ------------------
    m_ClipLabel = m_Canvas.AddElement<UILabel>();
    m_ClipLabel->SetRect(pad + 10.f, pad + ph + 6.f, pw, 0.f);
    m_ClipLabel->text  = "";
    m_ClipLabel->color = { 1.00f, 0.85f, 0.30f, 1.f };
    m_ClipLabel->scale = 0.85f;

    // ---- Hint label (bottom-left) -------------------------------------------
    auto* hint = m_Canvas.AddElement<UILabel>();
    hint->SetRect(pad, static_cast<float>(GetWindow()->GetHeight()) - 46.f, 0.f, 0.f);
    hint->text  = "R = Restart   S = Save   L = Load";
    hint->color = { 0.75f, 0.75f, 0.75f, 0.85f };
    hint->scale = 0.9f;

    auto* hint2 = m_Canvas.AddElement<UILabel>();
    hint2->SetRect(pad, static_cast<float>(GetWindow()->GetHeight()) - 26.f, 0.f, 0.f);
    hint2->text  = "Tab = Switch Demo";
    hint2->color = { 0.75f, 0.75f, 0.75f, 0.85f };
    hint2->scale = 0.9f;

    // ---- Restart button (top-right corner) ----------------------------------
    const float bw = 120.f, bh = 32.f;
    const float bx = static_cast<float>(GetWindow()->GetWidth()) - bw - pad;
    auto* btn = m_Canvas.AddElement<UIButton>();
    btn->SetRect(bx, pad, bw, bh);
    btn->label = "Restart";
    btn->hwnd  = GetWindow()->GetHandle();
    btn->SetOnClick([this] { RestartCurrentDemo(); });
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
    camGO->GetTransform()->SetPosition({ 0.f, 18.f, -22.f });
    m_Camera = camGO->GetComponent<Camera>();
    m_Camera->SetAspect(aspect);
    m_Camera->SetYaw  (0.f);    // centered — gallery spans X axis
    m_Camera->SetPitch(38.f);   // steep downward tilt — aerial gallery view

    // ---- Floor (static rigidbody) -------------------------------------------
    // Visual: 16x16 subdivided grid mesh — no diagonal seam artifact.
    //         Scale 30x30 in XZ so edges stay outside the camera frustum.
    // Position offset +5 on Z so the grid's near edge sits at Z=-10 (same as
    // camera Z).  Without this offset the grid extends to Z=-15, which puts the
    // near-right corner behind the camera's near plane (depth < 0.1).  The GPU
    // clips those triangles and the near-plane boundary projects as a visible
    // diagonal seam line across the floor.  Shifting to (0,0,5) → Z∈[-10,20]
    // keeps the shallowest floor vertex at depth ≈ 1.45 >> 0.1.
    auto* floor = scene->CreateGameObject("Floor");
    floor->GetTransform()->SetPosition({ 0.f, 0.f, 5.f });
    floor->GetTransform()->SetScale   ({ 30.f, 1.f, 30.f });

    auto* floorMR = floor->AddComponent<MeshRenderer>();
    floorMR->SetMesh(m_FloorMesh);
    floorMR->SetCommandList(m_DX12.GetCommandList());
    floorMR->SetMaterial(&m_FloorMaterial);
    floorMR->CreateConstantBuffer(m_DX12.GetDevice());

    auto* floorRB = floor->AddComponent<Rigidbody>();
    floorRB->SetBoxHalfExtents({ 15.f, 0.1f, 15.f }); // match visual 30x30 footprint
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

    // ---- FBX gallery (with material auto-extraction) ----------------------------
    // Loads 5 FBX files, extracts per-file material data (albedo, roughness,
    // metallic, diffuse texture path), creates a Material per object automatically.
    // All meshes are cached after the first load — RestartScene() is cheap.
    //
    // Layout (top view, X axis):   z=2
    //   -8      -4       0       4       8
    //  spider  huesit  cottage phong   cubes
    {
        struct FbxEntry {
            std::wstring file;
            std::string  name;
            float        x;
            float        scale;
        };
        std::wstring modDir = GetExeDir() + L"Models\\";
        FbxEntry entries[] = {
            { modDir + L"spider.fbx",            "FBX_Spider",  -8.f, 0.015f  },
            { modDir + L"huesitos.fbx",           "FBX_Huesitos",-4.f, 0.015f  },
            { modDir + L"cottage_fbx.fbx",        "FBX_Cottage",  0.f, 0.0005f },
            { modDir + L"phong_cube.fbx",         "FBX_Phong",    4.f, 1.5f    },
            { modDir + L"cubes_with_names.fbx",   "FBX_Cubes",    8.f, 0.5f    },
        };
        constexpr size_t N = std::size(entries);

        // Open upload frame if any mesh needs to be uploaded
        bool anyNeedUpload = false;
        for (auto& e : entries) {
            std::string key(e.file.begin(), e.file.end());
            if (!ResourceManager::Get().HasMesh(key)) { anyNeedUpload = true; break; }
        }
        if (anyNeedUpload) m_DX12.BeginFrame();

        // Load all files (geometry + material)
        ResourceManager::ModelWithMaterial results[N];
        for (size_t i = 0; i < N; ++i)
            results[i] = ResourceManager::Get().LoadModelWithMaterial(entries[i].file);

        if (anyNeedUpload) {
            m_DX12.EndFrame();
            m_DX12.WaitForGPU();
            ResourceManager::Get().ReleaseUploadBuffers();
        }

        // Create per-object Material from extracted data, spawn GameObjects
        for (size_t i = 0; i < N; ++i) {
            if (!results[i].mesh) {
                LOG_WARN("SandboxApp: FBX load failed [%s]", entries[i].name.c_str());
                continue;
            }

            // Build a heap-allocated Material driven by FBX material data.
            // Fallback: white albedo / roughness 0.5 / metallic 0 when no material.
            auto fbxMat = std::make_shared<Material>();
            fbxMat->SetPipeline(&m_Pipeline);

            const auto& md = results[i].material;
            if (md.hasMaterial) {
                fbxMat->SetAlbedo   (md.albedo);
                fbxMat->SetRoughness(md.roughness);
                fbxMat->SetMetallic (md.metallic);
                fbxMat->SetEmissive (md.emissive);

                // Embedded texture (glTF .glb) takes priority over external path.
                if (md.embeddedAlbedo) {
                    fbxMat->SetTexture(md.embeddedAlbedo.get());
                } else if (!md.diffuseTexturePath.empty()) {
                    auto tex = ResourceManager::Get().GetOrLoadTexture(md.diffuseTexturePath);
                    if (tex) fbxMat->SetTexture(tex.get());
                }
            } else {
                // No material in file — use a distinct color per slot
                const DirectX::XMFLOAT4 kFallback[5] = {
                    {0.85f,0.60f,0.45f,1.f}, {1.00f,0.80f,0.10f,1.f},
                    {0.70f,0.70f,0.70f,1.f}, {0.95f,0.30f,0.10f,1.f},
                    {0.65f,0.80f,1.00f,1.f},
                };
                fbxMat->SetAlbedo(kFallback[i % 5]);
            }
            fbxMat->Create(m_DX12.GetDevice());
            m_FbxMaterials.push_back(fbxMat);   // keep alive for scene lifetime

            auto* go = scene->CreateGameObject(entries[i].name);
            go->GetTransform()->SetPosition({ entries[i].x, 0.f, 2.f });
            go->GetTransform()->SetScale   ({ entries[i].scale, entries[i].scale, entries[i].scale });

            auto* mr = go->AddComponent<MeshRenderer>();
            mr->SetMesh(results[i].mesh);
            mr->SetCommandList(m_DX12.GetCommandList());
            mr->SetMaterial(fbxMat.get());
            mr->CreateConstantBuffer(m_DX12.GetDevice());

            LOG_INFO("SandboxApp: placed %s  hasMat=%d  albedo=(%.2f,%.2f,%.2f)",
                     entries[i].name.c_str(), md.hasMaterial,
                     md.albedo.x, md.albedo.y, md.albedo.z);
        }
    }

    // ---- glTF gallery (Khronos sample assets) --------------------------------
    // box.glb / duck.glb live next to the FBX models in the Models folder.
    // Files are optional — missing ones are skipped gracefully.
    // duck.glb uses an embedded texture (PNG blob inside the .glb binary).
    {
        struct GltfEntry { std::wstring file; std::string name; float x; float scale; };
        std::wstring modDir = GetExeDir() + L"Models\\";
        GltfEntry gltfEntries[] = {
            { modDir + L"box.glb",  "GLTF_Box",  -13.f, 1.0f  },
            { modDir + L"duck.glb", "GLTF_Duck", -10.f, 0.01f },
        };
        constexpr size_t NG = std::size(gltfEntries);

        bool anyGltfNeedUpload = false;
        for (auto& e : gltfEntries) {
            std::string key(e.file.begin(), e.file.end());
            if (!ResourceManager::Get().HasMesh(key)) { anyGltfNeedUpload = true; break; }
        }
        if (anyGltfNeedUpload) m_DX12.BeginFrame();

        ResourceManager::ModelWithMaterial gltfResults[NG];
        for (size_t i = 0; i < NG; ++i)
            gltfResults[i] = ResourceManager::Get().LoadModelWithMaterial(gltfEntries[i].file);

        if (anyGltfNeedUpload) {
            m_DX12.EndFrame();
            m_DX12.WaitForGPU();
            ResourceManager::Get().ReleaseUploadBuffers();
        }

        for (size_t i = 0; i < NG; ++i) {
            if (!gltfResults[i].mesh) {
                LOG_WARN("SandboxApp: glTF load failed or file missing [%s]",
                         gltfEntries[i].name.c_str());
                continue;
            }

            auto gltfMat = std::make_shared<Material>();
            gltfMat->SetPipeline(&m_Pipeline);

            const auto& md = gltfResults[i].material;
            if (md.hasMaterial) {
                gltfMat->SetAlbedo   (md.albedo);
                gltfMat->SetRoughness(md.roughness);
                gltfMat->SetMetallic (md.metallic);
                if (md.embeddedAlbedo)
                    gltfMat->SetTexture(md.embeddedAlbedo.get());
                else if (!md.diffuseTexturePath.empty()) {
                    auto t = ResourceManager::Get().GetOrLoadTexture(md.diffuseTexturePath);
                    if (t) gltfMat->SetTexture(t.get());
                }
            } else {
                gltfMat->SetAlbedo({ 0.60f, 0.85f, 1.00f, 1.f }); // light-blue fallback
            }
            gltfMat->Create(m_DX12.GetDevice());
            m_FbxMaterials.push_back(gltfMat);

            auto* go = scene->CreateGameObject(gltfEntries[i].name);
            go->GetTransform()->SetPosition({ gltfEntries[i].x, 0.f, 2.f });
            go->GetTransform()->SetScale   ({ gltfEntries[i].scale,
                                             gltfEntries[i].scale,
                                             gltfEntries[i].scale });

            auto* mr = go->AddComponent<MeshRenderer>();
            mr->SetMesh(gltfResults[i].mesh);
            mr->SetCommandList(m_DX12.GetCommandList());
            mr->SetMaterial(gltfMat.get());
            mr->CreateConstantBuffer(m_DX12.GetDevice());

            LOG_INFO("SandboxApp: placed %s (glTF)  hasMat=%d  embedded=%d",
                     gltfEntries[i].name.c_str(),
                     md.hasMaterial, md.embeddedAlbedo != nullptr);
        }
    }

    // ---- Wire shadow map, IBL, flat normal, and SSAO pass to every MeshRenderer
    {
        auto mrs = scene->FindComponentsOfType<MeshRenderer>();
        for (auto* mr : mrs) {
            mr->SetShadowMap (&m_ShadowMap);
            mr->SetIBLMap    (&m_IBLMap);
            mr->SetFlatNormal(&m_FlatNormal);
            mr->SetAOPass    (&m_SSAOPass);
        }
    }
}

// ---------------------------------------------------------------------------
void SandboxApp::RestartScene()
{
    auto* scene = SceneManager::Get().GetActiveScene();
    if (!scene) return;

    m_Camera     = nullptr;
    m_SelectedGO = nullptr;              // inspector: clear stale pointer
    m_FbxMaterials.clear();              // release per-object FBX materials
    scene->Clear();                       // ~GameObject() → Rigidbody::OnDestroy
    PhysicsWorld::Get().Shutdown();
    PhysicsWorld::Get().Initialize();
    SetupScene(scene);
}

// ---------------------------------------------------------------------------
void SandboxApp::RestartCurrentDemo()
{
    auto* scene = SceneManager::Get().GetActiveScene();
    if (!scene) return;

    m_Camera     = nullptr;
    m_SelectedGO = nullptr;
    m_FbxMaterials.clear();
    m_UndoStack.Clear();
    m_IsPlaying = false;
    scene->Clear();
    PhysicsWorld::Get().Shutdown();
    PhysicsWorld::Get().Initialize();

    if (m_DemoMode == DemoMode::Physics)
        SetupScene(scene);
    else
        SetupSkeletalScene(scene);
}

// ---------------------------------------------------------------------------
void SandboxApp::SwitchDemo()
{
    auto* scene = SceneManager::Get().GetActiveScene();
    if (!scene) return;

    m_Camera     = nullptr;
    m_SelectedGO = nullptr;
    m_UndoStack.Clear();
    m_IsPlaying = false;
    scene->Clear();
    PhysicsWorld::Get().Shutdown();
    PhysicsWorld::Get().Initialize();

    if (m_DemoMode == DemoMode::Physics) {
        m_DemoMode = DemoMode::Skeletal;
        SetupSkeletalScene(scene);
        if (m_SubtitleLabel) m_SubtitleLabel->text = "Skeletal Demo";
    } else {
        m_DemoMode = DemoMode::Physics;
        m_WalkClip.reset();
        m_WaveClip.reset();
        SetupScene(scene);
        if (m_SubtitleLabel) m_SubtitleLabel->text = "Physics Demo";
        if (m_ClipLabel)     m_ClipLabel->text     = "";
    }
}

// ---------------------------------------------------------------------------
// SetupSkeletalScene
//
// Humanoid skeleton hierarchy (Y-up, standing at y=0):
//
//   SkeletonRoot (invisible, holds HierarchicalAnimator)
//     └─ Torso_Joint  (y=1.65)
//          ├─ Head_Joint       (local y=+0.42)
//          ├─ L/R_Shoulder     (local x=±0.28, y=+0.28)
//          │     └─ L/R_Elbow  (local y=-0.70)
//          └─ L/R_Hip          (local x=±0.12, y=-0.35)
//                └─ L/R_Knee   (local y=-0.70)
//
// Joint GOs (scale 1,1,1) are the animation pivots driven by HierarchicalAnimator.
// Visual GOs (children of joints) are separate so scale does not cascade.
// Gold cubes (m_JointMaterial) mark every joint; peach boxes (m_SkinMaterial)
// represent the limb segments between joints.
//
// Two clips are baked here and CrossfadeTo-ed with the T key:
//   walk  (4 s loop) — walking gait: legs alternating, arms counter-swing
//   wave  (3 s loop) — right arm raise + elbow waving, head tracking
// ---------------------------------------------------------------------------
void SandboxApp::SetupSkeletalScene(Scene* scene)
{
    const float aspect = static_cast<float>(GetWindow()->GetWidth())
                       / static_cast<float>(GetWindow()->GetHeight());

    // ---- Camera -------------------------------------------------------------
    // Offset +2 on X and pull back slightly so the camera views the skeleton
    // from a 3/4 angle.  This prevents any single bone from aligning edge-on
    // with the view direction (which appeared as a thin diagonal line when the
    // camera was dead-centre at X=0 and the wave clip rotated the forearm
    // toward the camera).
    auto* camGO = scene->CreateGameObject("Camera");
    camGO->AddComponent<Camera>();
    camGO->GetTransform()->SetPosition({ 2.0f, 3.0f, -5.5f });
    m_Camera = camGO->GetComponent<Camera>();
    m_Camera->SetAspect(aspect);
    m_Camera->SetYaw  (-20.f);   // turn left to face skeleton at x=0
    m_Camera->SetPitch( 12.f);   // slight downward tilt

    // ---- Floor (visual only — no physics needed for skeletal demo) ----------
    // Scale 20x20 so all four edges stay outside the camera frustum.
    auto* floor = scene->CreateGameObject("Floor");
    floor->GetTransform()->SetPosition({ 0.f, 0.f, 0.f });
    floor->GetTransform()->SetScale   ({ 20.f, 1.f, 20.f });
    auto* floorMR = floor->AddComponent<MeshRenderer>();
    floorMR->SetMesh(m_FloorMesh);
    floorMR->SetCommandList(m_DX12.GetCommandList());
    floorMR->SetMaterial(&m_FloorMaterial);
    floorMR->CreateConstantBuffer(m_DX12.GetDevice());

    // =========================================================================
    // Skeleton construction helpers (lambdas capture scene + this)
    // =========================================================================

    // Create a joint-pivot GO (scale 1,1,1, no mesh).
    // If parentJoint != nullptr the GO is parented and localPos is LOCAL.
    auto MakeJoint = [&](const std::string& name,
                         GameObject* parentJoint,
                         DirectX::XMFLOAT3 localPos) -> GameObject*
    {
        auto* j = scene->CreateGameObject(name + "_Joint");
        if (parentJoint)
            j->GetTransform()->SetParent(parentJoint->GetTransform());
        j->GetTransform()->SetPosition(localPos);
        // scale stays {1,1,1} so children inherit no unwanted scale

        // Gold joint-marker cube (child, scale separate from pivot)
        auto* m = scene->CreateGameObject(name + "_Marker");
        m->GetTransform()->SetParent(j->GetTransform());
        m->GetTransform()->SetPosition({ 0.f, 0.f, 0.f });
        m->GetTransform()->SetScale   ({ 0.12f, 0.12f, 0.12f });
        auto* mr = m->AddComponent<MeshRenderer>();
        mr->SetMesh(m_CubeMesh);
        mr->SetCommandList(m_DX12.GetCommandList());
        mr->SetMaterial(&m_JointMaterial);
        mr->CreateConstantBuffer(m_DX12.GetDevice());

        return j;
    };

    // Create a limb-segment visual (elongated box, child of a joint).
    auto MakeBone = [&](const std::string& name,
                        GameObject* parentJoint,
                        DirectX::XMFLOAT3 localPos,
                        DirectX::XMFLOAT3 scale)
    {
        auto* b = scene->CreateGameObject(name + "_Bone");
        b->GetTransform()->SetParent(parentJoint->GetTransform());
        b->GetTransform()->SetPosition(localPos);
        b->GetTransform()->SetScale   (scale);
        auto* mr = b->AddComponent<MeshRenderer>();
        mr->SetMesh(m_CubeMesh);
        mr->SetCommandList(m_DX12.GetCommandList());
        mr->SetMaterial(&m_SkinMaterial);
        mr->CreateConstantBuffer(m_DX12.GetDevice());
    };

    // =========================================================================
    // Build hierarchy
    // =========================================================================

    // Invisible root GO — the HierarchicalAnimator lives here
    auto* skelRoot = scene->CreateGameObject("SkeletonRoot");
    // Raise 0.1 units so foot geometry clears the floor surface (avoids Z-fighting)
    skelRoot->GetTransform()->SetPosition({ 0.f, 0.1f, 0.f });

    // ---- Torso --------------------------------------------------------------
    auto* torso = MakeJoint("Torso", skelRoot, { 0.f, 1.65f, 0.f });
    MakeBone("Torso", torso, { 0.f, 0.f, 0.f }, { 0.40f, 0.70f, 0.22f });

    // ---- Head ---------------------------------------------------------------
    auto* head = MakeJoint("Head", torso, { 0.f, 0.42f, 0.f });
    MakeBone("Head", head, { 0.f, 0.17f, 0.f }, { 0.30f, 0.30f, 0.28f });

    // ---- Left arm -----------------------------------------------------------
    auto* lShoulder = MakeJoint("L_Shoulder", torso, { -0.28f, 0.28f, 0.f });
    MakeBone("L_UpperArm", lShoulder, { 0.f, -0.35f, 0.f }, { 0.11f, 0.70f, 0.11f });

    auto* lElbow = MakeJoint("L_Elbow", lShoulder, { 0.f, -0.70f, 0.f });
    MakeBone("L_Forearm", lElbow, { 0.f, -0.25f, 0.f }, { 0.09f, 0.50f, 0.09f });
    MakeBone("L_Hand",    lElbow, { 0.f, -0.50f, 0.f }, { 0.13f, 0.09f, 0.07f });

    // ---- Right arm (mirror X) -----------------------------------------------
    auto* rShoulder = MakeJoint("R_Shoulder", torso, { +0.28f, 0.28f, 0.f });
    MakeBone("R_UpperArm", rShoulder, { 0.f, -0.35f, 0.f }, { 0.11f, 0.70f, 0.11f });

    auto* rElbow = MakeJoint("R_Elbow", rShoulder, { 0.f, -0.70f, 0.f });
    MakeBone("R_Forearm", rElbow, { 0.f, -0.25f, 0.f }, { 0.09f, 0.50f, 0.09f });
    MakeBone("R_Hand",    rElbow, { 0.f, -0.50f, 0.f }, { 0.13f, 0.09f, 0.07f });

    // ---- Left leg -----------------------------------------------------------
    auto* lHip = MakeJoint("L_Hip", torso, { -0.12f, -0.35f, 0.f });
    MakeBone("L_Thigh", lHip, { 0.f, -0.35f, 0.f }, { 0.13f, 0.70f, 0.13f });

    auto* lKnee = MakeJoint("L_Knee", lHip, { 0.f, -0.70f, 0.f });
    MakeBone("L_Shin", lKnee, { 0.f, -0.30f, 0.f }, { 0.11f, 0.60f, 0.11f });
    MakeBone("L_Foot", lKnee, { 0.f, -0.60f, 0.f }, { 0.14f, 0.08f, 0.25f });

    // ---- Right leg (mirror X) -----------------------------------------------
    auto* rHip = MakeJoint("R_Hip", torso, { +0.12f, -0.35f, 0.f });
    MakeBone("R_Thigh", rHip, { 0.f, -0.35f, 0.f }, { 0.13f, 0.70f, 0.13f });

    auto* rKnee = MakeJoint("R_Knee", rHip, { 0.f, -0.70f, 0.f });
    MakeBone("R_Shin", rKnee, { 0.f, -0.30f, 0.f }, { 0.11f, 0.60f, 0.11f });
    MakeBone("R_Foot", rKnee, { 0.f, -0.60f, 0.f }, { 0.14f, 0.08f, 0.25f });

    // =========================================================================
    // HierarchicalAnimator — register all joints
    // =========================================================================
    auto* ctrl = skelRoot->AddComponent<HierarchicalAnimator>();
    ctrl->RegisterBone("Torso",      torso);
    ctrl->RegisterBone("Head",       head);
    ctrl->RegisterBone("L_Shoulder", lShoulder);
    ctrl->RegisterBone("R_Shoulder", rShoulder);
    ctrl->RegisterBone("L_Elbow",    lElbow);
    ctrl->RegisterBone("R_Elbow",    rElbow);
    ctrl->RegisterBone("L_Hip",      lHip);
    ctrl->RegisterBone("R_Hip",      rHip);
    ctrl->RegisterBone("L_Knee",     lKnee);
    ctrl->RegisterBone("R_Knee",     rKnee);

    // =========================================================================
    // Clip 1: "walk"  — 4 s loop, full walking gait
    //
    // Coordinate convention (right-hand, Y-up):
    //   X rotation (pitch) = leg/arm swings forward (+) / backward (-)
    //   Z rotation (roll)  = lateral lean / arm raise
    // =========================================================================
    auto walk = std::make_shared<HierarchicalAnimationClip>("walk");
    walk->SetDuration(4.f);

    // Legs — hip swing, opposite phase
    walk->AddRotationKey("L_Hip", 0.f, {-25,0,0})
        .AddRotationKey("L_Hip", 2.f, { 25,0,0}, EasingMode::SineInOut)
        .AddRotationKey("L_Hip", 4.f, {-25,0,0}, EasingMode::SineInOut);

    walk->AddRotationKey("R_Hip", 0.f, { 25,0,0})
        .AddRotationKey("R_Hip", 2.f, {-25,0,0}, EasingMode::SineInOut)
        .AddRotationKey("R_Hip", 4.f, { 25,0,0}, EasingMode::SineInOut);

    // Knees — bend when leg swings back (push-off phase)
    // L leg is back at t=0 → knee bent; forward at t=2 → knee straight
    walk->AddRotationKey("L_Knee", 0.0f, {-45,0,0})
        .AddRotationKey("L_Knee", 0.8f, {-10,0,0}, EasingMode::EaseOut)
        .AddRotationKey("L_Knee", 1.5f, {  0,0,0}, EasingMode::EaseOut)
        .AddRotationKey("L_Knee", 2.0f, {  0,0,0})
        .AddRotationKey("L_Knee", 2.8f, {-10,0,0}, EasingMode::EaseIn)
        .AddRotationKey("L_Knee", 3.5f, {-45,0,0}, EasingMode::EaseIn)
        .AddRotationKey("L_Knee", 4.0f, {-45,0,0});

    walk->AddRotationKey("R_Knee", 0.0f, {  0,0,0})
        .AddRotationKey("R_Knee", 0.8f, {-10,0,0}, EasingMode::EaseIn)
        .AddRotationKey("R_Knee", 1.5f, {-45,0,0}, EasingMode::EaseIn)
        .AddRotationKey("R_Knee", 2.0f, {-45,0,0})
        .AddRotationKey("R_Knee", 2.8f, {-10,0,0}, EasingMode::EaseOut)
        .AddRotationKey("R_Knee", 3.5f, {  0,0,0}, EasingMode::EaseOut)
        .AddRotationKey("R_Knee", 4.0f, {  0,0,0});

    // Arms — counter-swing to legs (natural walking)
    walk->AddRotationKey("L_Shoulder", 0.f, { 20,0,0})
        .AddRotationKey("L_Shoulder", 2.f, {-20,0,0}, EasingMode::SineInOut)
        .AddRotationKey("L_Shoulder", 4.f, { 20,0,0}, EasingMode::SineInOut);

    walk->AddRotationKey("R_Shoulder", 0.f, {-20,0,0})
        .AddRotationKey("R_Shoulder", 2.f, { 20,0,0}, EasingMode::SineInOut)
        .AddRotationKey("R_Shoulder", 4.f, {-20,0,0}, EasingMode::SineInOut);

    // Elbows — slight passive bend in sync with shoulder swing
    walk->AddRotationKey("L_Elbow", 0.f, {-35,0,0})
        .AddRotationKey("L_Elbow", 1.f, { -5,0,0}, EasingMode::SineInOut)
        .AddRotationKey("L_Elbow", 2.f, {-35,0,0}, EasingMode::SineInOut)
        .AddRotationKey("L_Elbow", 3.f, { -5,0,0}, EasingMode::SineInOut)
        .AddRotationKey("L_Elbow", 4.f, {-35,0,0}, EasingMode::SineInOut);

    walk->AddRotationKey("R_Elbow", 0.f, { -5,0,0})
        .AddRotationKey("R_Elbow", 1.f, {-35,0,0}, EasingMode::SineInOut)
        .AddRotationKey("R_Elbow", 2.f, { -5,0,0}, EasingMode::SineInOut)
        .AddRotationKey("R_Elbow", 3.f, {-35,0,0}, EasingMode::SineInOut)
        .AddRotationKey("R_Elbow", 4.f, { -5,0,0}, EasingMode::SineInOut);

    // Head — subtle look-around
    walk->AddRotationKey("Head", 0.f, {0,-8,0})
        .AddRotationKey("Head", 2.f, {0, 8,0}, EasingMode::SineInOut)
        .AddRotationKey("Head", 4.f, {0,-8,0}, EasingMode::SineInOut);

    // Torso — slight lateral sway
    walk->AddRotationKey("Torso", 0.f, {0,0,-4})
        .AddRotationKey("Torso", 2.f, {0,0, 4}, EasingMode::SineInOut)
        .AddRotationKey("Torso", 4.f, {0,0,-4}, EasingMode::SineInOut);

    // =========================================================================
    // Clip 2: "wave"  — 3 s loop, right arm raised + waving
    // =========================================================================
    auto wave = std::make_shared<HierarchicalAnimationClip>("wave");
    wave->SetDuration(3.f);

    // Raise right arm (Z roll lifts arm outward/up from hanging position)
    wave->AddRotationKey("R_Shoulder", 0.0f, {  0,0,  0})
        .AddRotationKey("R_Shoulder", 0.5f, {-15,0,-85}, EasingMode::EaseOut)
        .AddRotationKey("R_Shoulder", 3.0f, {-15,0,-85});   // hold raised

    // Forearm waving back-and-forth (X rotation of elbow)
    wave->AddRotationKey("R_Elbow", 0.0f, {   0,0,0})
        .AddRotationKey("R_Elbow", 0.5f, {-105,0,0}, EasingMode::EaseOut)
        .AddRotationKey("R_Elbow", 1.0f, { -55,0,0}, EasingMode::SineInOut)
        .AddRotationKey("R_Elbow", 1.5f, {-105,0,0}, EasingMode::SineInOut)
        .AddRotationKey("R_Elbow", 2.0f, { -55,0,0}, EasingMode::SineInOut)
        .AddRotationKey("R_Elbow", 2.5f, {-105,0,0}, EasingMode::SineInOut)
        .AddRotationKey("R_Elbow", 3.0f, { -55,0,0}, EasingMode::SineInOut);

    // Head turns to look toward raised hand (positive Y = look right)
    wave->AddRotationKey("Head", 0.0f, {0,  0,0})
        .AddRotationKey("Head", 0.5f, {0, 28,0}, EasingMode::EaseOut)
        .AddRotationKey("Head", 3.0f, {0, 28,0});

    // Left arm hangs naturally
    wave->AddRotationKey("L_Shoulder", 0.f, {0,0,0})
        .AddRotationKey("L_Shoulder", 3.f, {0,0,0});
    wave->AddRotationKey("L_Elbow", 0.f, {-10,0,0})
        .AddRotationKey("L_Elbow", 3.f, {-10,0,0});

    // Torso slight lean toward raised arm
    wave->AddRotationKey("Torso", 0.0f, {0,0,  0})
        .AddRotationKey("Torso", 0.5f, {0,0, +6}, EasingMode::EaseOut)
        .AddRotationKey("Torso", 3.0f, {0,0, +6});

    // =========================================================================
    // Start with walk; T key cross-fades to wave (handled in OnUpdate).
    // Both shared_ptrs are stored as SandboxApp members so they outlive
    // this function and are available for CrossfadeTo().
    // =========================================================================
    m_WalkClip          = walk;
    m_WaveClip          = wave;
    m_SkeletalPlayingWalk = true;

    ctrl->Play(walk, /*loop=*/true);
    if (m_ClipLabel) m_ClipLabel->text = "clip: walk  [T = crossfade]";

    // ---- Wire shadow map, IBL, flat normal, and SSAO pass to every MeshRenderer
    {
        auto mrs = scene->FindComponentsOfType<MeshRenderer>();
        for (auto* mr : mrs) {
            mr->SetShadowMap (&m_ShadowMap);
            mr->SetIBLMap    (&m_IBLMap);
            mr->SetFlatNormal(&m_FlatNormal);
            mr->SetAOPass    (&m_SSAOPass);
        }
    }
}

// ---------------------------------------------------------------------------
// DuplicateSelected — Ctrl+D: copy selected GO + its MeshRenderer.
// The new object is placed 0.5 units to the right of the original.
// ---------------------------------------------------------------------------
void SandboxApp::DuplicateSelected()
{
    auto* scene = SceneManager::Get().GetActiveScene();
    if (!scene || !m_SelectedGO) return;

    auto* orig = m_SelectedGO;
    auto* dup  = scene->CreateGameObject(orig->GetName() + " (Copy)");

    // Copy transform (local, assuming top-level object)
    auto* ot = orig->GetTransform();
    auto* dt = dup->GetTransform();
    auto  p  = ot->GetLocalPosition();
    p.x += 0.5f;                        // slight offset so it's not hidden behind original
    dt->SetPosition(p);
    dt->SetRotation(ot->GetLocalRotation());
    dt->SetScale   (ot->GetLocalScale());

    // Copy MeshRenderer if present
    if (auto* mr = orig->GetComponent<MeshRenderer>()) {
        auto* dmr = dup->AddComponent<MeshRenderer>();
        dmr->SetMesh       (mr->GetMesh());
        dmr->SetMaterial   (mr->GetMaterial());
        dmr->SetCommandList(m_DX12.GetCommandList());
        dmr->SetShadowMap  (&m_ShadowMap);
        dmr->SetIBLMap     (&m_IBLMap);
        dmr->SetFlatNormal (&m_FlatNormal);
        dmr->SetAOPass     (&m_SSAOPass);
        dmr->CreateConstantBuffer(m_DX12.GetDevice());
    }

    m_SelectedGO = dup;
}

// ---------------------------------------------------------------------------
// FocusCamera — F key: move editor camera to frame the selected GO.
// ---------------------------------------------------------------------------
void SandboxApp::FocusCamera()
{
    if (!m_Camera || !m_SelectedGO) return;
    auto* camGO = m_Camera->GetGameObject();
    if (!camGO) return;

    auto* t  = m_SelectedGO->GetTransform();
    auto* ct = camGO->GetTransform();

    // World-space centre from world matrix
    DirectX::XMFLOAT4X4 wf;
    DirectX::XMStoreFloat4x4(&wf, t->GetWorldMatrix());
    DirectX::XMFLOAT3 center = { wf._41, wf._42, wf._43 };

    // Move camera back along its current forward direction, at a fixed distance
    float dist = 5.0f;
    float yRad = DirectX::XMConvertToRadians(m_Camera->GetYaw());
    float pRad = DirectX::XMConvertToRadians(m_Camera->GetPitch());
    DirectX::XMFLOAT3 forward = {
        sinf(yRad) * cosf(pRad),
       -sinf(pRad),
        cosf(yRad) * cosf(pRad)
    };
    ct->SetPosition({
        center.x - forward.x * dist,
        center.y - forward.y * dist,
        center.z - forward.z * dist
    });
}

// ---------------------------------------------------------------------------
void SandboxApp::SaveScene()
{
    auto* scene = SceneManager::Get().GetActiveScene();
    if (!scene) return;
    if (SceneSerializer::Save(*scene, m_SavePath))
        LOG_INFO("Scene saved to %s", m_SavePath.c_str());
}

// ---------------------------------------------------------------------------
// LoadScene:
//   1. Restart (clears GOs and reinits physics)
//   2. SetupScene created fresh GOs with default config
//   3. SceneSerializer::Load patches their Transforms & component configs
//   4. On the next frame Update → Awake/Start fires, Rigidbody uses patched values
// ---------------------------------------------------------------------------
void SandboxApp::LoadScene()
{
    auto* scene = SceneManager::Get().GetActiveScene();
    if (!scene) return;

    // Step 1-2: recreate structure (same as RestartScene)
    m_Camera     = nullptr;
    m_SelectedGO = nullptr;
    scene->Clear();
    PhysicsWorld::Get().Shutdown();
    PhysicsWorld::Get().Initialize();
    SetupScene(scene);

    // Step 3: restore saved state before Start() runs
    if (!SceneSerializer::Load(m_SavePath, *scene))
        LOG_WARN("LoadScene: save file not found, running with defaults");
}

// ---------------------------------------------------------------------------
void SandboxApp::OnWindowMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    if (m_ImGuiLayer.IsInitialized())
        m_ImGuiLayer.HandleWindowMessage(GetWindow()->GetHandle(), msg, wp, lp);
}

// ---------------------------------------------------------------------------
void SandboxApp::OnPreUpdate(float dt)
{
    // Step physics only while in Play mode; editor mode freezes simulation.
    if (m_IsPlaying)
        PhysicsWorld::Get().Update(dt);
}

// ---------------------------------------------------------------------------
void SandboxApp::OnUpdate(float dt)
{
    // Don't process game hotkeys when ImGui is capturing the keyboard
    // (e.g. user is typing in an inspector text field).
    const bool imguiWantsKb = m_ImGuiLayer.IsInitialized()
                            && ImGui::GetIO().WantCaptureKeyboard;

    if (!imguiWantsKb) {
        auto& inp = InputManager::Get();
        bool  ctrl = inp.IsKeyDown(KeyCode::Ctrl);

        // ---- Undo / Redo (Ctrl+Z / Ctrl+Y) ----------------------------------
        if (ctrl && inp.IsKeyPressed(KeyCode::Z)) m_UndoStack.Undo();
        if (ctrl && inp.IsKeyPressed(KeyCode::Y)) m_UndoStack.Redo();

        // ---- Duplicate (Ctrl+D) ---------------------------------------------
        if (ctrl && inp.IsKeyPressed(KeyCode::D)) DuplicateSelected();

        // ---- Focus camera (F) -----------------------------------------------
        if (!ctrl && inp.IsKeyPressed(KeyCode::F)) FocusCamera();

        // ---- Play / Stop (Space) --------------------------------------------
        if (!ctrl && inp.IsKeyPressed(KeyCode::Space))
            m_IsPlaying = !m_IsPlaying;

        if (!ctrl) {
            if (inp.IsKeyPressed(KeyCode::R))   RestartCurrentDemo();
            if (inp.IsKeyPressed(KeyCode::Tab)) SwitchDemo();
            // S / L — save / load (both demo modes)
            if (inp.IsKeyPressed(KeyCode::S))   SaveScene();
            if (inp.IsKeyPressed(KeyCode::L))   LoadScene();
        }
    }

    // Skeletal-mode: T key crossfades between walk ↔ wave clips
    if (!imguiWantsKb
     && m_DemoMode == DemoMode::Skeletal
     && InputManager::Get().IsKeyPressed(KeyCode::T)
     && m_WalkClip && m_WaveClip)
    {
        if (auto* scene = SceneManager::Get().GetActiveScene()) {
            auto* rootGO = scene->FindByName("SkeletonRoot");
            auto* ctrl   = rootGO ? rootGO->GetComponent<HierarchicalAnimator>() : nullptr;
            if (ctrl) {
                if (m_SkeletalPlayingWalk) {
                    ctrl->CrossfadeTo(m_WaveClip, 0.4f, /*loop=*/true);
                    m_SkeletalPlayingWalk = false;
                    if (m_ClipLabel) m_ClipLabel->text = "clip: wave  [T = crossfade]";
                } else {
                    ctrl->CrossfadeTo(m_WalkClip, 0.4f, /*loop=*/true);
                    m_SkeletalPlayingWalk = true;
                    if (m_ClipLabel) m_ClipLabel->text = "clip: walk  [T = crossfade]";
                }
            }
        }
    }

    // ---- Object picking (left click, ImGui not consuming mouse) ---------------
    if (!ImGui::GetIO().WantCaptureMouse && m_Camera) {
        static bool s_WasLDown = false;
        bool lDown    = InputManager::Get().IsMouseButtonDown(0);
        bool lClicked = lDown && !s_WasLDown;
        s_WasLDown    = lDown;

        if (lClicked) {
            POINT cursor = InputManager::Get().GetMousePosition();
            HWND  hwnd   = GetWindow()->GetHandle();
            ScreenToClient(hwnd, &cursor);
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;

            if (w > 0 && h > 0) {
                float ndcX =  2.f * cursor.x / static_cast<float>(w) - 1.f;
                float ndcY = -2.f * cursor.y / static_cast<float>(h) + 1.f;

                auto ray = m_Camera->ScreenToRay(ndcX, ndcY);

                using namespace DirectX;
                XMVECTOR ro = XMLoadFloat3(&ray.Origin);
                XMVECTOR rd = XMLoadFloat3(&ray.Direction);

                float       minT = FLT_MAX;
                GameObject* hit  = nullptr;

                if (auto* scene = SceneManager::Get().GetActiveScene()) {
                    for (const auto& goPtr : scene->GetGameObjects()) {
                        auto* go = goPtr.get();
                        auto* t  = go->GetTransform();
                        if (!t) continue;

                        // World-space centre from world matrix translation row
                        XMFLOAT4X4 wf;
                        XMStoreFloat4x4(&wf, t->GetWorldMatrix());
                        XMFLOAT3 center = { wf._41, wf._42, wf._43 };

                        XMFLOAT3 scl = t->GetLocalScale();
                        float maxDim = std::abs(scl.x);
                        if (std::abs(scl.y) > maxDim) maxDim = std::abs(scl.y);
                        if (std::abs(scl.z) > maxDim) maxDim = std::abs(scl.z);
                        float radius = maxDim * 0.866f;
                        if (radius < 0.15f) radius = 0.15f; // minimum pickable size

                        // Ray-sphere intersection
                        XMVECTOR oc   = ro - XMLoadFloat3(&center);
                        float    b    = XMVectorGetX(XMVector3Dot(oc, rd));
                        float    c    = XMVectorGetX(XMVector3Dot(oc, oc)) - radius * radius;
                        float    disc = b * b - c;
                        if (disc < 0.f) continue;
                        float tt = -b - sqrtf(disc);
                        if (tt < 0.f) tt = -b + sqrtf(disc); // inside sphere
                        if (tt > 0.f && tt < minT) { minT = tt; hit = go; }
                    }
                }
                m_SelectedGO = hit;
            }
        }
    }

    // ---- Delete selected GO (Delete key) ------------------------------------
    if (!ImGui::GetIO().WantCaptureKeyboard &&
        InputManager::Get().IsKeyPressed(KeyCode::Delete) &&
        m_SelectedGO)
    {
        // Null camera pointer if we're about to delete the camera's GO
        if (m_Camera && m_Camera->GetGameObject() == m_SelectedGO)
            m_Camera = nullptr;

        if (auto* scene = SceneManager::Get().GetActiveScene())
            scene->MarkForDestroy(m_SelectedGO);
        m_SelectedGO = nullptr;
    }

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

    // PBR: upload camera world-space position so the pixel shader can compute
    // the view direction (V = normalize(CameraPos - WorldPos)) each frame.
    if (m_Camera && m_Camera->GetGameObject())
        if (auto* t = m_Camera->GetGameObject()->GetTransform())
            LightManager::Get().SetCameraPos(t->GetLocalPosition());
}

// ---------------------------------------------------------------------------
void SandboxApp::OnRender()
{
    // Shader hot reload — check BEFORE BeginFrame so the swap happens while
    // the GPU is fully idle (WaitForGPU flushes all in-flight work).
    // This stalls for one frame on save, which is acceptable for a dev tool.
    if (m_Pipeline.HasShaderChanged()) {
        m_DX12.WaitForGPU();
        m_Pipeline.HotReload();
    }

    m_DX12.BeginFrame();

    auto* cmdList = m_DX12.GetCommandList();
    const UINT frameIdx = m_DX12.GetBackBufferIndex();

    // Read last frame's GPU timestamps (safe: BeginFrame() waited for fence).
    m_GPUProfiler.ReadResults(frameIdx);

    // ---- Shadow pass (depth-only, before main 3D scene) -------------------
    // Renders all MeshRenderers into the 2048x2048 shadow map from the light's
    // perspective.  EndShadowPass() transitions the texture to SRV state so
    // Basic.hlsl can sample it (gShadowMap at t1) during the main pass.
    {
        auto* scene = SceneManager::Get().GetActiveScene();
        if (scene) {
            m_ShadowMap.BeginShadowPass(cmdList);
            m_ShadowPipeline.Bind(cmdList);

            const DirectX::XMMATRIX lightVP = m_ShadowMap.GetLightSpaceMatrix();
            auto mrs = scene->FindComponentsOfType<MeshRenderer>();
            for (auto* mr : mrs)
                mr->DrawShadow(cmdList, lightVP);

            m_ShadowMap.EndShadowPass(cmdList);
        }
    }

    // Redirect scene rendering into the HDR float render target so BloomPass
    // can apply luminance threshold, Gaussian blur, and composite (ACES + sRGB).
    m_BloomPass.BeginCapture(cmdList, m_DX12.GetDSV(),
                             m_DX12.GetWidth(), m_DX12.GetHeight());

    // ---- 3D scene (main pass → HDR RT) ------------------------------------
    m_GPUProfiler.BeginSection(cmdList, GPUProfiler::Section::Scene);
    if (auto* scene = SceneManager::Get().GetActiveScene())
        scene->Render();
    m_GPUProfiler.EndSection(cmdList, GPUProfiler::Section::Scene);

    // ---- SSAO pass --------------------------------------------------------
    // Compute AO from the just-filled depth buffer.  The result is read by
    // MeshRenderers next frame (one-frame latency, imperceptible in motion).
    if (m_SSAOPass.IsReady() && m_Camera) {
        using namespace DirectX;

        // Transition depth: DEPTH_WRITE → PIXEL_SHADER_RESOURCE
        auto depthToSRV = TransitionBarrier(m_DX12.GetDepthResource(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->ResourceBarrier(1, &depthToSRV);

        XMMATRIX proj    = m_Camera->GetProjectionMatrix();
        XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
        m_SSAOPass.ComputeAO(cmdList, m_DX12.GetDepthSRV(),
                              proj, invProj,
                              m_DX12.GetWidth(), m_DX12.GetHeight());

        // Transition depth back: PIXEL_SHADER_RESOURCE → DEPTH_WRITE
        // (next frame's BeginFrame needs DEPTH_WRITE for ClearDepthStencilView)
        auto depthToWrite = TransitionBarrier(m_DX12.GetDepthResource(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->ResourceBarrier(1, &depthToWrite);
    }

    // Bloom: BrightPass → BlurH → BlurV → Composite (ACES tone map + sRGB).
    // After Apply() the back-buffer RTV is bound; UI and ImGui render into it.
    m_BloomPass.Apply(cmdList, m_DX12.GetCurrentRTV(),
                     m_DX12.GetWidth(), m_DX12.GetHeight());

    // ---- 2D UI overlay ----------------------------------------------------
    m_GPUProfiler.BeginSection(cmdList, GPUProfiler::Section::UI);
    if (UIRenderer::Get().IsInitialized()) {
        UIRenderer::Get().BeginPass(cmdList,
                                    static_cast<UINT>(GetWindow()->GetWidth()),
                                    static_cast<UINT>(GetWindow()->GetHeight()));
        m_Canvas.Draw(UIRenderer::Get());
        UIRenderer::Get().EndPass();
    }
    m_GPUProfiler.EndSection(cmdList, GPUProfiler::Section::UI);

    // ---- ImGui debug overlay ----------------------------------------------
    // ImGuiLayer::End() re-binds the SRV heap before rendering draw data.
    m_GPUProfiler.BeginSection(cmdList, GPUProfiler::Section::ImGui);
    if (m_ImGuiLayer.IsInitialized()) {
        m_ImGuiLayer.Begin();
        RenderDebugUI();
        m_ImGuiLayer.End(cmdList);
    }
    m_GPUProfiler.EndSection(cmdList, GPUProfiler::Section::ImGui);

    // Resolve timestamps → readback buffer (must happen before EndFrame).
    m_GPUProfiler.Resolve(cmdList, frameIdx);

    m_DX12.EndFrame();
}

// ---------------------------------------------------------------------------
void SandboxApp::OnResize(int w, int h)
{
    m_DX12.Resize(static_cast<UINT>(w), static_cast<UINT>(h));
    m_BloomPass.Resize(m_DX12.GetDevice(), m_DX12,
                       static_cast<UINT>(w), static_cast<UINT>(h));
    if (m_SSAOPass.IsReady())
        m_SSAOPass.Resize(m_DX12.GetDevice(), m_DX12,
                          static_cast<UINT>(w), static_cast<UINT>(h));
    LightManager::Get().SetScreenSize(
        { static_cast<float>(w), static_cast<float>(h) });
    if (m_Camera)
        m_Camera->SetAspect(static_cast<float>(w) / static_cast<float>(h));
}

// ---------------------------------------------------------------------------
// SetupDefaultDockLayout — DockBuilder default panel layout
//
// Splits the dockspace into three regions:
//   Left  22%  → "Scene Hierarchy"
//   Right 28% of the remaining 78%  → "Inspector"
//   Center (transparent) → 3D viewport shows through (PassthruCentralNode)
// ---------------------------------------------------------------------------
void SandboxApp::SetupDefaultDockLayout(unsigned int dockspaceID)
{
    const ImVec2 vpSize = ImGui::GetMainViewport()->WorkSize;

    ImGui::DockBuilderRemoveNode(dockspaceID);
    ImGui::DockBuilderAddNode  (dockspaceID, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceID, vpSize);

    // ── Left column (22% of total width) ───────────────────────────────────
    //   Split into top (Hierarchy) and bottom (GPU Timings).
    ImGuiID leftColID, centerRightID;
    ImGui::DockBuilderSplitNode(dockspaceID, ImGuiDir_Left, 0.22f,
                                &leftColID, &centerRightID);

    ImGuiID hierarchyID, gpuTimingsID;
    ImGui::DockBuilderSplitNode(leftColID, ImGuiDir_Up, 0.60f,
                                &hierarchyID, &gpuTimingsID);

    // ── Right column (28% of the remaining 78%) ────────────────────────────
    ImGuiID centerID, inspectorID;
    ImGui::DockBuilderSplitNode(centerRightID, ImGuiDir_Right, 0.28f,
                                &inspectorID, &centerID);

    // Dock windows to their slots.
    ImGui::DockBuilderDockWindow("Scene Hierarchy", hierarchyID);
    ImGui::DockBuilderDockWindow("GPU Timings",     gpuTimingsID);
    ImGui::DockBuilderDockWindow("Inspector",       inspectorID);
    // centerID is intentionally left empty — 3D scene shows through.

    ImGui::DockBuilderFinish(dockspaceID);
}

// ---------------------------------------------------------------------------
// RenderDebugUI — Dear ImGui debug overlay with dockable panels
//
// Frame structure:
//   1. Fullscreen transparent "##DockHost" window (no decorations, has menu bar)
//      → hosts the DockSpace that every named panel can dock into.
//   2. Menu bar: Demo / Scene / Layout menus + right-aligned FPS counter.
//   3. "Scene Hierarchy" panel — lists every GameObject; click to select.
//   4. "Inspector" panel — Transform DragFloat3 + Active checkbox + component list.
//
// Default layout is built on the first frame via SetupDefaultDockLayout().
// "Layout → Reset to Default" rebuilds it on demand.
// m_SelectedGO is nulled on any restart/switch to avoid dangling pointers.
// ---------------------------------------------------------------------------
void SandboxApp::RenderDebugUI()
{
    // Must be called once per frame, immediately after ImGui::NewFrame()
    // (ImGuiLayer::Begin() calls NewFrame, so this is the right place).
    ImGuizmo::BeginFrame();

    auto* scene = SceneManager::Get().GetActiveScene();

    // =========================================================================
    // Fullscreen transparent dockspace host window
    // =========================================================================
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos (vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    constexpr ImGuiWindowFlags kHostFlags =
        ImGuiWindowFlags_NoTitleBar            |
        ImGuiWindowFlags_NoCollapse            |
        ImGuiWindowFlags_NoResize              |
        ImGuiWindowFlags_NoMove                |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus            |
        ImGuiWindowFlags_NoDocking             |
        ImGuiWindowFlags_MenuBar               |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.f, 0.f));
    ImGui::Begin("##DockHost", nullptr, kHostFlags);
    ImGui::PopStyleVar(3);

    // ---- Menu bar -----------------------------------------------------------
    // Flags for deferred popup opening (must be set before EndMenuBar so the
    // popup can be opened in the same window-level context afterwards).
    static bool s_OpenSaveDialog = false;
    static bool s_OpenLoadDialog = false;

    if (ImGui::BeginMenuBar()) {

        // File ----------------------------------------------------------------
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene",     "S"))   s_OpenSaveDialog = true;
            if (ImGui::MenuItem("Save Scene As..."))      s_OpenSaveDialog = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Load Scene",     "L"))   s_OpenLoadDialog = true;
            ImGui::EndMenu();
        }

        // Edit ----------------------------------------------------------------
        if (ImGui::BeginMenu("Edit")) {
            ImGui::BeginDisabled(!m_UndoStack.CanUndo());
            std::string undoLabel = "Undo";
            if (m_UndoStack.CanUndo())
                undoLabel = "Undo  " + m_UndoStack.GetUndoLabel();
            if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z"))
                m_UndoStack.Undo();
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!m_UndoStack.CanRedo());
            std::string redoLabel = "Redo";
            if (m_UndoStack.CanRedo())
                redoLabel = "Redo  " + m_UndoStack.GetRedoLabel();
            if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y"))
                m_UndoStack.Redo();
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::BeginDisabled(m_SelectedGO == nullptr);
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) DuplicateSelected();
            if (ImGui::MenuItem("Focus",     "F"))       FocusCamera();
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        // Play / Stop ---------------------------------------------------------
        {
            const float btnW = 72.f;
            // Centre the button in the available menu bar space
            float centerX = (ImGui::GetContentRegionMax().x - btnW) * 0.5f;
            if (centerX > ImGui::GetCursorPosX())
                ImGui::SetCursorPosX(centerX);

            if (!m_IsPlaying) {
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(0.20f, 0.55f, 0.20f, 1.f));
                if (ImGui::Button("  Play  "))
                    m_IsPlaying = true;
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(0.70f, 0.20f, 0.20f, 1.f));
                if (ImGui::Button("  Stop  "))
                    m_IsPlaying = false;
                ImGui::PopStyleColor();
            }
        }

        // Demo ----------------------------------------------------------------
        if (ImGui::BeginMenu("Demo")) {
            const bool isPhysics  = (m_DemoMode == DemoMode::Physics);
            const bool isSkeletal = (m_DemoMode == DemoMode::Skeletal);
            if (ImGui::MenuItem("Physics Demo",  nullptr, isPhysics,  !isPhysics))
                SwitchDemo();
            if (ImGui::MenuItem("Skeletal Demo", nullptr, isSkeletal, !isSkeletal))
                SwitchDemo();
            ImGui::EndMenu();
        }

        // Scene ---------------------------------------------------------------
        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::MenuItem("Restart", "R"))
                RestartCurrentDemo();
            ImGui::EndMenu();
        }

        // Layout --------------------------------------------------------------
        if (ImGui::BeginMenu("Layout")) {
            if (ImGui::MenuItem("Reset to Default"))
                m_ResetLayout = true;
            ImGui::EndMenu();
        }

        // Shader --------------------------------------------------------------
        if (ImGui::BeginMenu("Shader")) {
            if (ImGui::MenuItem("Force Hot Reload")) {
                m_DX12.WaitForGPU();
                m_Pipeline.HotReload();
            }
            ImGui::Separator();
            ImGui::TextDisabled("Edit Basic.hlsl and save to auto-reload.");
            ImGui::EndMenu();
        }

        // Right-aligned FPS counter -------------------------------------------
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f FPS", ImGui::GetIO().Framerate);
            const float textW = ImGui::CalcTextSize(buf).x + 16.f;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - textW);
            ImGui::TextDisabled("%s", buf);
        }

        ImGui::EndMenuBar();
    }

    // ---- Save / Load scene dialogs ------------------------------------------
    // Open the popups now that we are back in the ##DockHost window context.
    static char s_ScenePathBuf[512] = {};
    if (s_OpenSaveDialog) {
        strncpy_s(s_ScenePathBuf, sizeof(s_ScenePathBuf),
                  m_SavePath.c_str(), _TRUNCATE);
        ImGui::OpenPopup("Save Scene##modal");
        s_OpenSaveDialog = false;
    }
    if (s_OpenLoadDialog) {
        strncpy_s(s_ScenePathBuf, sizeof(s_ScenePathBuf),
                  m_SavePath.c_str(), _TRUNCATE);
        ImGui::OpenPopup("Load Scene##modal");
        s_OpenLoadDialog = false;
    }
    if (ImGui::BeginPopupModal("Save Scene##modal", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save path:");
        ImGui::SetNextItemWidth(380.f);
        ImGui::InputText("##sp", s_ScenePathBuf, sizeof(s_ScenePathBuf));
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(90.f, 0.f))) {
            m_SavePath = s_ScenePathBuf;
            SaveScene();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.f, 0.f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Load Scene##modal", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Load path:");
        ImGui::SetNextItemWidth(380.f);
        ImGui::InputText("##lp", s_ScenePathBuf, sizeof(s_ScenePathBuf));
        ImGui::Spacing();
        if (ImGui::Button("Load", ImVec2(90.f, 0.f))) {
            m_SavePath = s_ScenePathBuf;
            LoadScene();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.f, 0.f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- DockSpace ----------------------------------------------------------
    const ImGuiID dockspaceID = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspaceID, ImVec2(0.f, 0.f),
                     ImGuiDockNodeFlags_PassthruCentralNode);

    // Build the default layout on the very first frame, or when the user
    // requests a reset via "Layout → Reset to Default".
    if (ImGui::DockBuilderGetNode(dockspaceID) == nullptr || m_ResetLayout) {
        SetupDefaultDockLayout(static_cast<unsigned int>(dockspaceID));
        m_DockLayoutDone = true;
        m_ResetLayout    = false;
    }

    // =========================================================================
    // Transform Gizmo — rendered as an overlay on the central 3D viewport
    // =========================================================================
    if (m_SelectedGO && m_Camera) {
        using namespace DirectX;
        // Try to use the passthrough central node rect; fall back to full viewport.
        ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspaceID);
        ImVec2 gizmoPos  = central ? central->Pos  : vp->WorkPos;
        ImVec2 gizmoSize = central ? central->Size : vp->WorkSize;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(gizmoPos.x, gizmoPos.y, gizmoSize.x, gizmoSize.y);

        // DX row-major → ImGuizmo column-major: XMMatrixTranspose before storing.
        XMFLOAT4X4 viewF, projF, worldF;
        XMStoreFloat4x4(&viewF, XMMatrixTranspose(m_Camera->GetViewMatrix()));
        XMStoreFloat4x4(&projF, XMMatrixTranspose(m_Camera->GetProjectionMatrix()));
        auto* t = m_SelectedGO->GetTransform();
        XMStoreFloat4x4(&worldF, XMMatrixTranspose(t->GetWorldMatrix()));

        // Undo capture: snapshot local transform at drag-start; push on drag-end.
        static bool     s_GizmoWasUsing = false;
        static XMFLOAT3 s_GizmoOldP{}, s_GizmoOldR{}, s_GizmoOldS{};
        bool nowUsing = ImGuizmo::IsUsing();
        if (nowUsing && !s_GizmoWasUsing) {
            s_GizmoOldP = t->GetLocalPosition();
            s_GizmoOldR = t->GetLocalRotation();
            s_GizmoOldS = t->GetLocalScale();
        }

        bool changed = ImGuizmo::Manipulate(
            (float*)&viewF, (float*)&projF,
            m_GizmoOp,
            m_GizmoWorld ? ImGuizmo::WORLD : ImGuizmo::LOCAL,
            (float*)&worldF);

        if (changed) {
            float tr[3], ro[3], sc[3];
            ImGuizmo::DecomposeMatrixToComponents((float*)&worldF, tr, ro, sc);

            if (t->GetParent()) {
                // Compute local matrix: newWorld * inverse(parentWorld)
                XMMATRIX newWorldDX  = XMMatrixTranspose(XMLoadFloat4x4(&worldF));
                XMMATRIX parentWorld = t->GetParent()->GetWorldMatrix();
                XMMATRIX newLocalDX  = newWorldDX * XMMatrixInverse(nullptr, parentWorld);
                XMFLOAT4X4 localF;
                XMStoreFloat4x4(&localF, XMMatrixTranspose(newLocalDX));
                ImGuizmo::DecomposeMatrixToComponents((float*)&localF, tr, ro, sc);
            }

            t->SetPosition({ tr[0], tr[1], tr[2] });
            t->SetRotation({ ro[0], ro[1], ro[2] });
            t->SetScale   ({ sc[0], sc[1], sc[2] });
        }

        // Drag just ended — push undo command with old→new local transform.
        if (!nowUsing && s_GizmoWasUsing) {
            auto cmd   = std::make_unique<TransformCmd>();
            cmd->label = "Move " + m_SelectedGO->GetName();
            cmd->go    = m_SelectedGO;
            cmd->oldP  = s_GizmoOldP; cmd->oldR = s_GizmoOldR; cmd->oldS = s_GizmoOldS;
            cmd->newP  = t->GetLocalPosition();
            cmd->newR  = t->GetLocalRotation();
            cmd->newS  = t->GetLocalScale();
            m_UndoStack.PushPreExecuted(std::move(cmd));
        }
        s_GizmoWasUsing = nowUsing;
    }

    ImGui::End(); // ##DockHost

    // =========================================================================
    // GPU Timings panel
    // =========================================================================
    if (ImGui::Begin("GPU Timings")) {
        if (m_GPUProfiler.IsInitialized()) {
            const float sceneMs = m_GPUProfiler.GetMs(GPUProfiler::Section::Scene);
            const float uiMs    = m_GPUProfiler.GetMs(GPUProfiler::Section::UI);
            const float imguiMs = m_GPUProfiler.GetMs(GPUProfiler::Section::ImGui);
            const float totalMs = sceneMs + uiMs + imguiMs;
            const float scaleMs = (totalMs > 0.f) ? totalMs : 1.f; // avoid /0

            // Helper: label + ms text + progress bar on one line
            auto TimingRow = [&](const char* label, float ms, ImVec4 color) {
                ImGui::TextColored(color, "%-6s", label);
                ImGui::SameLine(60.f);
                ImGui::Text("%5.2f ms", ms);
                ImGui::SameLine(120.f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
                ImGui::ProgressBar(ms / scaleMs, ImVec2(-1.f, 6.f), "");
                ImGui::PopStyleColor();
            };

            TimingRow("Scene",  sceneMs, ImVec4(0.40f, 0.80f, 0.40f, 1.f)); // green
            TimingRow("UI",     uiMs,    ImVec4(0.40f, 0.70f, 1.00f, 1.f)); // blue
            TimingRow("ImGui",  imguiMs, ImVec4(1.00f, 0.75f, 0.30f, 1.f)); // gold
            ImGui::Separator();

            const float gpuFps = (totalMs > 0.f) ? 1000.f / totalMs : 0.f;
            ImGui::Text("Total  %5.2f ms   GPU cap: %.0f FPS", totalMs, gpuFps);

            // Budget indicator: 60 FPS = 16.67 ms, 30 FPS = 33.33 ms
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                totalMs < 16.67f ? ImVec4(0.2f,0.8f,0.2f,1.f) :
                totalMs < 33.33f ? ImVec4(1.0f,0.7f,0.1f,1.f) :
                                   ImVec4(1.0f,0.2f,0.2f,1.f));
            const float budget = totalMs / 33.33f < 1.f ? totalMs / 33.33f : 1.f;
            ImGui::ProgressBar(budget, ImVec2(-1.f, 8.f),
                               totalMs < 16.67f ? "< 16.7ms (60fps)" :
                               totalMs < 33.33f ? "< 33.3ms (30fps)" : "> 33.3ms");
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("GPU profiler not available.");
        }
    }
    ImGui::End();

    // =========================================================================
    // Scene Hierarchy panel
    // =========================================================================
    if (ImGui::Begin("Scene Hierarchy")) {
        // ---- GameObject creation / deletion toolbar -------------------------
        if (scene) {
            if (ImGui::Button("+ Empty")) {
                m_SelectedGO = scene->CreateGameObject("New Object");
            }
            ImGui::SameLine();
            if (ImGui::Button("+ Cube")) {
                auto* go = scene->CreateGameObject("Cube");
                auto* mr = go->AddComponent<MeshRenderer>();
                mr->SetMesh(m_CubeMesh);
                mr->SetCommandList(m_DX12.GetCommandList());
                mr->SetMaterial(&m_BoxMaterial);
                mr->SetShadowMap(&m_ShadowMap);
                mr->SetIBLMap(&m_IBLMap);
                mr->SetFlatNormal(&m_FlatNormal);
                mr->SetAOPass(&m_SSAOPass);
                mr->CreateConstantBuffer(m_DX12.GetDevice());
                m_SelectedGO = go;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(m_SelectedGO == nullptr);
            if (ImGui::Button("Delete")) {
                if (m_Camera && m_Camera->GetGameObject() == m_SelectedGO)
                    m_Camera = nullptr;
                scene->MarkForDestroy(m_SelectedGO);
                m_SelectedGO = nullptr;
            }
            ImGui::EndDisabled();
            ImGui::Separator();
        }

        if (scene) {
            // ---- Per-frame rename state (persistent across frames) ----------
            static GameObject* s_RenameGO    = nullptr;
            static char        s_RenameBuf[256] = {};
            static bool        s_RenameFocus    = false;

            // ---- Ancestor-check helper for drag&drop cycle prevention -------
            auto IsAncestorOf = [](Transform* potentialAncestor,
                                   Transform* t) -> bool {
                t = t->GetParent();
                while (t) {
                    if (t == potentialAncestor) return true;
                    t = t->GetParent();
                }
                return false;
            };

            // ---- Recursive node draw ----------------------------------------
            std::function<void(GameObject*)> DrawNode;
            DrawNode = [&](GameObject* go) {
                if (!go) return;
                auto* t = go->GetTransform();
                if (!t)  return;

                ImGui::PushID(go);

                // Visibility toggle: O = visible, - = hidden (MeshRenderer or GO)
                auto* mr      = go->GetComponent<MeshRenderer>();
                bool  visible = mr ? mr->IsEnabled() : go->IsActive();
                ImGui::PushStyleColor(ImGuiCol_Text,
                    visible ? ImVec4(1.f, 1.f, 1.f, 1.f)
                            : ImVec4(0.45f, 0.45f, 0.45f, 1.f));
                if (ImGui::SmallButton(visible ? "O##vis" : "-##vis")) {
                    if (mr) mr->SetEnabled(!visible);
                    else    go->SetActive(!visible);
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();

                // ---- Rename mode — InputText replaces the tree label --------
                if (s_RenameGO == go) {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (s_RenameFocus) { ImGui::SetKeyboardFocusHere(); s_RenameFocus = false; }
                    bool done = ImGui::InputText("##rename", s_RenameBuf,
                        sizeof(s_RenameBuf),
                        ImGuiInputTextFlags_EnterReturnsTrue |
                        ImGuiInputTextFlags_AutoSelectAll);
                    if (done) {
                        if (s_RenameBuf[0] != '\0') go->SetName(s_RenameBuf);
                        s_RenameGO = nullptr;
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) s_RenameGO = nullptr;
                    ImGui::PopID();
                    return;   // skip tree node while rename text box is open
                }

                // ---- Tree node ----------------------------------------------
                const bool hasChildren = !t->GetChildren().empty();
                const bool isSelected  = (m_SelectedGO == go);

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                           ImGuiTreeNodeFlags_OpenOnArrow;
                if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf |
                                           ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (isSelected)   flags |= ImGuiTreeNodeFlags_Selected;
                if (!go->IsActive())
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));

                bool nodeOpen = ImGui::TreeNodeEx(
                    static_cast<void*>(go), flags, "%s", go->GetName().c_str());

                if (!go->IsActive()) ImGui::PopStyleColor();

                // Click → select (ignore toggle-open clicks)
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
                    !ImGui::IsItemToggledOpen())
                    m_SelectedGO = go;

                // Double-click → start rename
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    s_RenameGO    = go;
                    s_RenameFocus = true;
                    strncpy_s(s_RenameBuf, sizeof(s_RenameBuf),
                              go->GetName().c_str(), _TRUNCATE);
                }

                // Drag source: carry a raw GO* pointer
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("GO_REPARENT", &go, sizeof(go));
                    ImGui::Text("Reparent: %s", go->GetName().c_str());
                    ImGui::EndDragDropSource();
                }

                // Drop target: make dragged GO a child of this GO
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* pl =
                            ImGui::AcceptDragDropPayload("GO_REPARENT")) {
                        GameObject* dragged = *(GameObject**)pl->Data;
                        if (dragged && dragged != go &&
                            !IsAncestorOf(dragged->GetTransform(), go->GetTransform()))
                            dragged->GetTransform()->SetParent(go->GetTransform());
                    }
                    ImGui::EndDragDropTarget();
                }

                // Recurse children (only when the node is open)
                if (hasChildren && nodeOpen) {
                    for (auto* child : t->GetChildren())
                        if (child && child->GetGameObject())
                            DrawNode(child->GetGameObject());
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }; // DrawNode

            // Draw only root GOs (Transform has no parent)
            for (const auto& goPtr : scene->GetGameObjects()) {
                auto* t = goPtr ? goPtr->GetTransform() : nullptr;
                if (t && t->GetParent() == nullptr)
                    DrawNode(goPtr.get());
            }

            // Drop onto blank panel area → unparent (make root-level)
            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.y < 4.f) avail.y = 4.f;
            ImGui::Dummy(avail);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl =
                        ImGui::AcceptDragDropPayload("GO_REPARENT")) {
                    GameObject* dragged = *(GameObject**)pl->Data;
                    if (dragged) dragged->GetTransform()->SetParent(nullptr);
                }
                ImGui::EndDragDropTarget();
            }
        } else {
            ImGui::TextDisabled("(no active scene)");
        }
    }
    ImGui::End();

    // =========================================================================
    // Inspector panel
    // =========================================================================
    if (ImGui::Begin("Inspector")) {
        if (!m_SelectedGO) {
            ImGui::TextDisabled("(select an object in Scene Hierarchy)");
        } else {
            // ---- Header: name + Active checkbox ----------------------------
            ImGui::Text("%s", m_SelectedGO->GetName().c_str());
            ImGui::SameLine();
            bool active = m_SelectedGO->IsActive();
            if (ImGui::Checkbox("Active", &active))
                m_SelectedGO->SetActive(active);

            ImGui::Separator();

            // ---- Transform -------------------------------------------------
            if (ImGui::CollapsingHeader("Transform",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto* tr = m_SelectedGO->GetTransform();

                // Shared pre-edit snapshot for all three DragFloat3 controls.
                static DirectX::XMFLOAT3 s_InspOldP{}, s_InspOldR{}, s_InspOldS{};

                // Helper lambda: capture old state when a drag starts.
                auto CaptureOld = [&]() {
                    s_InspOldP = tr->GetLocalPosition();
                    s_InspOldR = tr->GetLocalRotation();
                    s_InspOldS = tr->GetLocalScale();
                };
                // Helper lambda: push undo command when a drag ends.
                auto PushTransformUndo = [&](const char* lbl) {
                    auto cmd   = std::make_unique<TransformCmd>();
                    cmd->label = std::string(lbl) + " " + m_SelectedGO->GetName();
                    cmd->go    = m_SelectedGO;
                    cmd->oldP  = s_InspOldP; cmd->oldR = s_InspOldR; cmd->oldS = s_InspOldS;
                    cmd->newP  = tr->GetLocalPosition();
                    cmd->newR  = tr->GetLocalRotation();
                    cmd->newS  = tr->GetLocalScale();
                    m_UndoStack.PushPreExecuted(std::move(cmd));
                };

                // Position
                {
                    auto p = tr->GetLocalPosition();
                    float v[3] = { p.x, p.y, p.z };
                    if (ImGui::DragFloat3("Position", v, 0.05f))
                        tr->SetPosition({ v[0], v[1], v[2] });
                    if (ImGui::IsItemActivated())             CaptureOld();
                    if (ImGui::IsItemDeactivatedAfterEdit())  PushTransformUndo("Move");
                }
                // Rotation (Euler degrees — XYZ = pitch/yaw/roll)
                {
                    auto r = tr->GetLocalRotation();
                    float v[3] = { r.x, r.y, r.z };
                    if (ImGui::DragFloat3("Rotation", v, 0.5f))
                        tr->SetRotation({ v[0], v[1], v[2] });
                    if (ImGui::IsItemActivated())             CaptureOld();
                    if (ImGui::IsItemDeactivatedAfterEdit())  PushTransformUndo("Rotate");
                }
                // Scale
                {
                    auto s = tr->GetLocalScale();
                    float v[3] = { s.x, s.y, s.z };
                    if (ImGui::DragFloat3("Scale", v, 0.01f))
                        tr->SetScale({ v[0], v[1], v[2] });
                    if (ImGui::IsItemActivated())             CaptureOld();
                    if (ImGui::IsItemDeactivatedAfterEdit())  PushTransformUndo("Scale");
                }
            }

            // ---- Gizmo operation toolbar -----------------------------------
            ImGui::Separator();
            ImGui::TextDisabled("Gizmo:");
            ImGui::SameLine();
            if (ImGui::RadioButton("Tr", m_GizmoOp == ImGuizmo::TRANSLATE))
                m_GizmoOp = ImGuizmo::TRANSLATE;
            ImGui::SameLine();
            if (ImGui::RadioButton("Rt", m_GizmoOp == ImGuizmo::ROTATE))
                m_GizmoOp = ImGuizmo::ROTATE;
            ImGui::SameLine();
            if (ImGui::RadioButton("Sc", m_GizmoOp == ImGuizmo::SCALE))
                m_GizmoOp = ImGuizmo::SCALE;
            ImGui::SameLine();
            ImGui::Checkbox("World", &m_GizmoWorld);

            // ---- MeshRenderer / Material -----------------------------------
            if (auto* mr = m_SelectedGO->GetComponent<MeshRenderer>()) {
                ImGui::Separator();
                if (ImGui::CollapsingHeader("Material",
                        ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (Material* mat = mr->GetMaterial()) {
                        auto  alb  = mat->GetAlbedo();
                        float rou  = mat->GetRoughness();
                        float met  = mat->GetMetallic();
                        auto  emi  = mat->GetEmissive();
                        float emiI = mat->GetEmissiveIntensity();

                        if (ImGui::ColorEdit4("Albedo",    (float*)&alb))
                            mat->SetAlbedo(alb);
                        if (ImGui::SliderFloat("Roughness", &rou, 0.f, 1.f))
                            mat->SetRoughness(rou);
                        if (ImGui::SliderFloat("Metallic",  &met, 0.f, 1.f))
                            mat->SetMetallic(met);
                        bool emiChanged = ImGui::ColorEdit3("Emissive", (float*)&emi);
                        emiChanged |= ImGui::DragFloat("Emissive Int.", &emiI, 0.05f, 0.f, 50.f);
                        if (emiChanged)
                            mat->SetEmissive(emi, emiI);

                        // ---- Texture slots ----------------------------------
                        ImGui::Spacing();
                        ImGui::TextDisabled("Textures");

                        // Path buffers — cleared when the selected GO changes.
                        static GameObject* s_LastTexGO = nullptr;
                        static char s_AlbedoBuf[512]   = {};
                        static char s_NormalBuf[512]    = {};
                        if (s_LastTexGO != m_SelectedGO) {
                            s_AlbedoBuf[0] = '\0';
                            s_NormalBuf[0] = '\0';
                            s_LastTexGO    = m_SelectedGO;
                        }

                        // Load texture from narrow UTF-8 path via ResourceManager cache.
                        // ResourceManager keeps the shared_ptr alive, so raw ptr is safe.
                        auto LoadAndSet = [&](const char* buf, bool isNormal) {
                            if (!buf[0]) return;
                            int n = MultiByteToWideChar(CP_UTF8, 0, buf, -1, nullptr, 0);
                            if (n <= 1) return;
                            std::wstring wp(n - 1, L'\0');
                            MultiByteToWideChar(CP_UTF8, 0, buf, -1, &wp[0], n);
                            auto tex = ResourceManager::Get().GetOrLoadTexture(wp);
                            if (tex) {
                                if (isNormal) mat->SetNormalMap(tex.get());
                                else          mat->SetTexture  (tex.get());
                            }
                        };

                        // Albedo texture row
                        {
                            bool has = (mat->GetTexture() != nullptr);
                            ImGui::TextColored(
                                has ? ImVec4(0.4f, 0.9f, 0.4f, 1.f)
                                    : ImVec4(0.6f, 0.6f, 0.6f, 1.f),
                                has ? "Albedo [set]" : "Albedo [none]");
                            ImGui::SetNextItemWidth(-58.f);
                            ImGui::InputText("##albpath", s_AlbedoBuf, sizeof(s_AlbedoBuf));
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Load##ta")) LoadAndSet(s_AlbedoBuf, false);
                        }
                        // Normal map row
                        {
                            bool has = (mat->GetNormalMap() != nullptr);
                            ImGui::TextColored(
                                has ? ImVec4(0.4f, 0.9f, 0.4f, 1.f)
                                    : ImVec4(0.6f, 0.6f, 0.6f, 1.f),
                                has ? "Normal  [set]" : "Normal  [none]");
                            ImGui::SetNextItemWidth(-58.f);
                            ImGui::InputText("##norpath", s_NormalBuf, sizeof(s_NormalBuf));
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Load##tn")) LoadAndSet(s_NormalBuf, true);
                        }
                    } else {
                        ImGui::TextDisabled("(no material)");
                    }
                }
            }

            // ---- Camera ----------------------------------------------------
            if (auto* cam = m_SelectedGO->GetComponent<Camera>()) {
                ImGui::Separator();
                if (ImGui::CollapsingHeader("Camera",
                        ImGuiTreeNodeFlags_DefaultOpen))
                {
                    float fov   = cam->GetFOV();
                    float nearZ = cam->GetNearZ();
                    float farZ  = cam->GetFarZ();
                    float mspd  = cam->GetMoveSpeed();
                    if (ImGui::SliderFloat("FOV",        &fov,   10.f, 120.f))
                        cam->SetFOV(fov);
                    if (ImGui::DragFloat("Near",         &nearZ, 0.01f, 0.01f, 10.f))
                        cam->SetNearFar(nearZ, farZ);
                    if (ImGui::DragFloat("Far",          &farZ,  1.f,   10.f,  5000.f))
                        cam->SetNearFar(nearZ, farZ);
                    if (ImGui::DragFloat("Move Speed",   &mspd,  0.1f,  0.1f,  50.f))
                        cam->SetMoveSpeed(mspd);
                }
            }

            // ---- Rigidbody -------------------------------------------------
            if (auto* rb = m_SelectedGO->GetComponent<Rigidbody>()) {
                ImGui::Separator();
                if (ImGui::CollapsingHeader("Rigidbody",
                        ImGuiTreeNodeFlags_DefaultOpen))
                {
                    // Shape type + per-shape dimension controls
                    const char* shapeNames[] = { "Box", "Sphere", "Capsule" };
                    int shapeIdx = static_cast<int>(rb->GetShapeType());
                    ImGui::TextDisabled("Shape: %s", shapeNames[shapeIdx]);

                    if (rb->GetShapeType() == Rigidbody::ShapeType::Box) {
                        auto he = rb->GetHalfExtents();
                        float v[3] = { he.x, he.y, he.z };
                        if (ImGui::DragFloat3("Half Extents", v, 0.01f, 0.01f, 100.f))
                            rb->SetBoxHalfExtents({ v[0], v[1], v[2] });
                    } else if (rb->GetShapeType() == Rigidbody::ShapeType::Sphere) {
                        float r = rb->GetRadius();
                        if (ImGui::DragFloat("Radius", &r, 0.01f, 0.01f, 100.f))
                            rb->SetSphereRadius(r);
                    } else {
                        float r  = rb->GetRadius();
                        float hh = rb->GetCapsuleHalfHeight();
                        if (ImGui::DragFloat("Cap Radius",  &r,  0.01f, 0.01f, 50.f))
                            rb->SetCapsuleShape(r, hh);
                        if (ImGui::DragFloat("Cap HalfH",   &hh, 0.01f, 0.01f, 50.f))
                            rb->SetCapsuleShape(r, hh);
                    }

                    // Physics properties
                    ImGui::Spacing();
                    float mass = rb->GetMass();
                    float rest = rb->GetRestitution();
                    float fric = rb->GetFriction();
                    if (ImGui::DragFloat("Mass",         &mass, 0.1f,  0.f,  1000.f))
                        rb->SetMass(mass);
                    if (ImGui::SliderFloat("Restitution", &rest, 0.f,  1.f))
                        rb->SetRestitution(rest);
                    if (ImGui::SliderFloat("Friction",    &fric, 0.f,  1.f))
                        rb->SetFriction(fric);

                    // State flags
                    ImGui::Spacing();
                    bool isStatic    = rb->IsStatic();
                    bool isKinematic = rb->IsKinematic();
                    bool isTrigger   = rb->IsTrigger();
                    if (ImGui::Checkbox("Static",    &isStatic))    rb->SetStatic(isStatic);
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Kinematic", &isKinematic)) rb->SetKinematic(isKinematic);
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Trigger",   &isTrigger))   rb->SetTrigger(isTrigger);

                    // Runtime velocity — read-only display when body is active
                    if (rb->IsValid()) {
                        ImGui::Spacing();
                        auto lv = rb->GetLinearVelocity();
                        ImGui::TextDisabled("Vel (%.2f, %.2f, %.2f)", lv.x, lv.y, lv.z);
                    }
                }
            }

            // ---- Add Component drop-down -----------------------------------
            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button("+ Add Component", ImVec2(-1.f, 0.f)))
                ImGui::OpenPopup("##add_comp");
            if (ImGui::BeginPopup("##add_comp")) {
                ImGui::TextDisabled("Select component to add");
                ImGui::Separator();
                if (ImGui::MenuItem("Rigidbody") &&
                        !m_SelectedGO->HasComponent<Rigidbody>())
                    m_SelectedGO->AddComponent<Rigidbody>();
                if (ImGui::MenuItem("Camera") &&
                        !m_SelectedGO->HasComponent<Camera>()) {
                    auto* cam = m_SelectedGO->AddComponent<Camera>();
                    const auto* win = GetWindow();
                    if (win && win->GetHeight() > 0)
                        cam->SetAspect(static_cast<float>(win->GetWidth()) /
                                       static_cast<float>(win->GetHeight()));
                }
                if (ImGui::MenuItem("Animator") &&
                        !m_SelectedGO->HasComponent<Animator>())
                    m_SelectedGO->AddComponent<Animator>();
                if (ImGui::MenuItem("HierarchicalAnimator") &&
                        !m_SelectedGO->HasComponent<HierarchicalAnimator>())
                    m_SelectedGO->AddComponent<HierarchicalAnimator>();
                ImGui::EndPopup();
            }
        }

        // ---- Directional Light (global — always shown at Inspector bottom) --
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Directional Light")) {
            auto& lm    = LightManager::Get();
            auto  dir   = lm.GetDirection();
            auto  col   = lm.GetColor();
            float inten = lm.GetIntensity();
            auto  amb   = lm.GetAmbient();

            if (ImGui::DragFloat3("Direction",  (float*)&dir,  0.01f, -1.f, 1.f))
                lm.SetDirection(dir);
            if (ImGui::ColorEdit3("Light Color", (float*)&col))
                lm.SetColor(col);
            if (ImGui::SliderFloat("Intensity",  &inten, 0.f, 10.f))
                lm.SetIntensity(inten);
            if (ImGui::ColorEdit3("Ambient",     (float*)&amb))
                lm.SetAmbient(amb);
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
void SandboxApp::OnShutdown()
{
    if (auto* scene = SceneManager::Get().GetActiveScene())
        scene->Clear();           // ensure Rigidbody::OnDestroy runs before Shutdown
    PhysicsWorld::Get().Shutdown();

    m_DX12.WaitForGPU();         // flush GPU before releasing any DX12 resources
    m_GPUProfiler.Shutdown();    // readback buffers + query heap
    m_ImGuiLayer.Shutdown();     // ImGui DX12 backend release (after WaitForGPU)
    UIRenderer::Get().Shutdown();
    m_Pipeline.Destroy();
    m_ShadowPipeline.Destroy();
    m_ShadowMap.Shutdown();
    m_IBLMap.Shutdown();
    m_BloomPass.Shutdown();
    m_SSAOPass.Shutdown();
    ResourceManager::Get().Shutdown();
    m_DX12.Shutdown();
}
