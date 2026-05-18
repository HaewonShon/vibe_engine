#pragma once
#include <string>

namespace VibeEngine {

class Scene;

// ============================================================================
// SceneSerializer
//
// Saves and loads a Scene's runtime state as a human-readable JSON file.
// No external dependencies — uses a self-contained minimal JSON library.
//
// ---- Save -------------------------------------------------------------------
//   SceneSerializer::Save(*scene, "saves/level1.json");
//
// ---- Load -------------------------------------------------------------------
//   SceneSerializer::Load("saves/level1.json", *scene);
//
// ---- What is serialized -----------------------------------------------------
//   • All GameObjects: name, active, layer
//   • Transform      : position, rotation (quaternion x,y,z,w), scale
//   • Camera         : yaw, pitch, fov, nearZ, farZ, moveSpeed, lookSpeed
//   • Rigidbody      : shape + dims, mass, restitution, friction,
//                      isStatic, isKinematic, isTrigger
//
// ---- What is NOT serialized -------------------------------------------------
//   • MeshRenderer  (GPU resources — always created in code)
//   • Animator clips (runtime clip objects built in code)
//   • AudioSource   (clip references)
//   • Parent-child Transform hierarchy
//
// ---- Intended workflow ------------------------------------------------------
//   // Save:
//   SceneSerializer::Save(*scene, savePath);
//
//   // Load — call AFTER SetupScene but BEFORE the first Update so that
//   //         Rigidbody::Start() uses the restored transforms:
//   scene->Clear();
//   PhysicsWorld::Get().Shutdown(); PhysicsWorld::Get().Initialize();
//   SetupScene(scene);                  // recreate GO structure
//   SceneSerializer::Load(savePath, *scene);   // restore state
//   // next Update() → Awake/Start runs with loaded transforms
// ============================================================================
class SceneSerializer {
public:
    // Save all serializable state to a JSON file.
    // Creates intermediate directories if needed.
    // Returns true on success.
    static bool Save(const Scene& scene, const std::string& filepath);

    // Load state from a JSON file and apply it to matching GameObjects in the
    // scene. GameObjects are matched by name; unmatched names are silently
    // skipped. Does NOT create or destroy GameObjects.
    // Returns true on success.
    static bool Load(const std::string& filepath, Scene& scene);
};

} // namespace VibeEngine
