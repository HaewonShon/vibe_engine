#pragma once
#include "Core/Application.h"
#include "Core/ResourceManager.h"
#include "Core/Scene.h"
#include "Core/UndoStack.h"
#include "Renderer/DX12Context.h"
#include "Renderer/BasicPipeline.h"
#include "Renderer/GPUProfiler.h"
#include "Renderer/ShadowMap.h"
#include "Renderer/ShadowPipeline.h"
#include "Renderer/IBLMap.h"
#include "Renderer/BloomPass.h"
#include "Renderer/SSAOPass.h"
#include "Renderer/Texture.h"
#include "Renderer/Material.h"
#include "Renderer/Camera.h"
#include "Renderer/FBXLoader.h"
#include "Renderer/Mesh.h"
#include "Core/GameObject.h"
#include "UI/UICanvas.h"
#include "UI/UILabel.h"
#include "UI/ImGuiLayer.h"
#include <imgui.h>          // must be included before ImGuizmo
#include <ImGuizmo.h>
#include "Animation/HierarchicalAnimationClip.h"
#include <memory>
#include <string>
#include <vector>

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
    void OnWindowMessage(UINT msg, WPARAM wp, LPARAM lp) override; // ImGui WndProc forward

private:
    // ---- Demo modes ---------------------------------------------------------
    enum class DemoMode { Physics, Skeletal };
    DemoMode m_DemoMode = DemoMode::Physics;

    // Restart whichever demo is active (R key).
    void RestartCurrentDemo();

    // Toggle between Physics ↔ Skeletal demo (Tab key).
    void SwitchDemo();

    // ---- Physics demo -------------------------------------------------------
    void SetupScene(VibeEngine::Scene* scene);
    void RestartScene();   // legacy helper used by UI Restart button
    void SaveScene();
    void LoadScene();

    // ---- Skeletal demo ------------------------------------------------------
    void SetupSkeletalScene(VibeEngine::Scene* scene);

    // ---- Debug UI -----------------------------------------------------------
    VibeEngine::ImGuiLayer   m_ImGuiLayer;
    VibeEngine::GameObject*  m_SelectedGO     = nullptr; // inspector selection
    bool                     m_DockLayoutDone = false;   // default layout built?
    bool                     m_ResetLayout    = false;   // user requested reset?
    void RenderDebugUI();
    void SetupDefaultDockLayout(unsigned int dockspaceID); // unsigned int == ImGuiID

    // ---- Gizmo state --------------------------------------------------------
    ImGuizmo::OPERATION m_GizmoOp    = ImGuizmo::TRANSLATE;
    bool                m_GizmoWorld = true;   // true = WORLD space, false = LOCAL

    // ---- Undo / Redo --------------------------------------------------------
    VibeEngine::UndoStack m_UndoStack;

    // ---- Play / Stop --------------------------------------------------------
    bool m_IsPlaying = false;   // false = editor (physics paused), true = play

    // ---- Editor helpers -----------------------------------------------------
    void DuplicateSelected();   // Ctrl+D
    void FocusCamera();         // F key — frame selected GO

    // ---- Graphics -----------------------------------------------------------
    VibeEngine::DX12Context    m_DX12;
    VibeEngine::BasicPipeline  m_Pipeline;
    VibeEngine::GPUProfiler    m_GPUProfiler;
    VibeEngine::ShadowMap      m_ShadowMap;
    VibeEngine::ShadowPipeline m_ShadowPipeline;
    VibeEngine::IBLMap         m_IBLMap;
    VibeEngine::BloomPass      m_BloomPass;
    VibeEngine::SSAOPass       m_SSAOPass;

    // 1×1 flat normal (128,128,255,255) used as a fallback when no normal map
    // is assigned to a material.  Shared by all MeshRenderers via SetFlatNormal().
    VibeEngine::Texture        m_FlatNormal;

    VibeEngine::Material m_BoxMaterial;      // red-orange cube (physics)
    VibeEngine::Material m_FloorMaterial;    // grey floor (both demos)
    VibeEngine::Material m_SkinMaterial;     // peach bone segments (skeletal)
    VibeEngine::Material m_JointMaterial;    // gold joint markers (skeletal)

    // Cached meshes — lifetime tied to ResourceManager
    std::shared_ptr<VibeEngine::Mesh> m_CubeMesh;
    std::shared_ptr<VibeEngine::Mesh> m_FloorMesh;  // subdivided grid (no diagonal seam)

    // ---- UI -----------------------------------------------------------------
    VibeEngine::UICanvas m_Canvas;
    VibeEngine::UILabel* m_FpsLabel      = nullptr;   // updated each frame
    VibeEngine::UILabel* m_SubtitleLabel = nullptr;   // "Physics Demo" / "Skeletal Demo"
    VibeEngine::UILabel* m_ClipLabel     = nullptr;   // current clip name (skeletal only)
    void SetupHUD();

    // ---- Live scene state ---------------------------------------------------
    VibeEngine::Camera* m_Camera = nullptr;   // raw ptr; nulled before Clear

    // ---- Serialization ------------------------------------------------------
    std::string m_SavePath;

    // ---- FBX per-object materials (heap-allocated, kept alive for scene lifetime)
    std::vector<std::shared_ptr<VibeEngine::Material>> m_FbxMaterials;

    // ---- Skeletal clip cache (kept alive for crossfade) ---------------------
    std::shared_ptr<VibeEngine::HierarchicalAnimationClip> m_WalkClip;
    std::shared_ptr<VibeEngine::HierarchicalAnimationClip> m_WaveClip;
    bool m_SkeletalPlayingWalk = true;   // tracks which clip is active
};
