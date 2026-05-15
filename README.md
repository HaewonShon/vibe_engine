# VibeEngine

A DirectX 12 game engine with Unity-style architecture, built from scratch.  
No external dependencies — Win32 + DirectX 12 SDK only.

---

## Features

### Rendering
| Feature | Details |
|---|---|
| **DX12 Context** | Device / swap chain / command queue / fence / depth buffer |
| **BasicPipeline** | Root signature (CBV + SRV), PSO, POSITION + NORMAL + TEXCOORD vertex format |
| **Mesh** | Upload-heap VB/IB, `CreateCube` / `CreatePlane` factories, `uint32` indices |
| **OBJ Loader** | Fan triangulation, vertex deduplication, flat-normal fallback, V-flip for DX12 |
| **Texture** | PNG/DDS → WIC → DX12 default-heap, SRV descriptor heap |
| **Material** | `albedo / roughness / metallic / emissive` cbuffer at b2; pipeline + texture pair |
| **Lighting** | Phong diffuse directional light (`LightManager`, `Basic.hlsl`) |
| **Camera** | FPS-style WASD/QE + mouse-look; aspect ratio auto-updated on window resize |
| **MeshRenderer** | Per-object MVP constant buffer; material-aware draw call |

### Core / ECS
| Feature | Details |
|---|---|
| **GameObject** | Named entity with active flag, layer, tag container, component map |
| **Component** | `Awake → OnEnable → Start → Update` lifecycle; `SetEnabled` / `OnDisable` hooks |
| **Transform** | Position / **quaternion** rotation (gimbal-lock-free) / scale; parent–child hierarchy; `GetForward/Right/Up`, `LookAt`, `RotateAxisAngle` |
| **Scene** | `CreateGameObject`, `MarkForDestroy` (deferred, iteration-safe), full query API |
| **SceneManager** | Singleton; named scene registry + `LoadScene` |

### Scene Query API
```cpp
// Name
scene->FindByName("Player")
scene->FindAllByName("Bullet")

// Layer
scene->FindByLayer(Layer::Enemy)
scene->FindAllByLayerMask(LayerMask::From(Layer::Enemy) | LayerMask::From(Layer::Player))

// GameplayTag  (hierarchical — "Enemy" matches "Enemy.Skill.Attack1")
scene->FindByTag(GameplayTag::FromString("Enemy"))
scene->FindAllByTag(...)
scene->FindAllByTagExact(...)

// Component
scene->FindComponentOfType<Camera>()
scene->FindAllByComponent<MeshRenderer>()

// Predicate
scene->FindByPredicate([](const GameObject* g) { return g->GetName().starts_with("NPC"); })

// Stats
scene->CountActiveGameObjects()
scene->GetActiveGameObjects()
```

### Layer System
```cpp
go->SetLayer(Layer::Enemy);          // assign

// Predefined: Default(0) UI(1) Player(2) Enemy(3) Environment(4) IgnoreRaycast(5)
// Slots 6-31 are user-defined

LayerMask mask = LayerMask::From(Layer::Enemy) | LayerMask::From(Layer::Player);
mask.Contains(Layer::Enemy);         // true
LayerMask noUI = ~LayerMask::From(Layer::UI);
```

### GameplayTag System  *(Unreal-style hierarchical)*
```cpp
go->AddTag(GameplayTag::FromString("Enemy.Skill.Attack1"));

go->HasTag(GameplayTag::FromString("Enemy"))               // true  (ancestor match)
go->HasTagExact(GameplayTag::FromString("Enemy"))          // false (exact only)

tag.RequestParent()   // "Enemy.Skill"
tag.Depth()           // 3

// Container queries
container.HasAllTags(required)
container.HasAnyTags(optional)
```

### Event Systems
```cpp
// Per-object multicast  (Delegate.h — Unity style)
Event<int, float> OnDamage;
DelegateHandle h = OnDamage.AddListener([](int dmg, float dir) { ... });
OnDamage.Invoke(10, 1.f);
OnDamage.RemoveListener(h);

// Single-cast
Delegate<float(int)> d;
d.Bind([](int x) { return x * 1.5f; });
float r = d(4);

// Global pub/sub  (EventBus.h — type-erased)
struct PlayerDiedEvent { std::string name; int score; };

DelegateHandle h = EventBus::Get().Subscribe<PlayerDiedEvent>(
    [](const PlayerDiedEvent& e) { ... });
EventBus::Get().Subscribe<PlayerDiedEvent>(this, &MyClass::OnPlayerDied);

EventBus::Get().Emit(PlayerDiedEvent{ "Alice", 9999 });
EventBus::Get().Unsubscribe(h);
EventBus::Get().ClearAll();          // safe on scene transitions
```

### Input
```cpp
InputManager::Get().IsKeyDown(KeyCode::W)
InputManager::Get().IsMouseButtonDown(0)
InputManager::Get().GetMouseDelta()   // POINT delta since last frame
// Input paused automatically when window loses focus
```

### Diagnostics
```cpp
// Profiler — CPU scope timing with EMA smoothing
PROFILE_SCOPE("Update");             // RAII; auto-records to Profiler singleton
Profiler::Get().GetSmoothedMs("Render")   // EMA α=0.15
Profiler::Get().GetAllScopes()            // min / max / smooth / last

// Log — timestamps + source file:line, file output
LOG_TRACE("dt = %.4f", dt);
LOG_INFO ("Loaded %d assets", n);
LOG_WARN ("Texture missing: %s", path);
LOG_ERROR("DX12 hr = 0x%08X", hr);
// Output: [14:23:45.123] [INFO ] [Application.cpp:39]  ...
// File:   vibe_engine.log (next to executable)
```

### Resource Manager
```cpp
ResourceManager::Get().Initialize(&dx12);

auto cube    = ResourceManager::Get().GetCube();
auto plane   = ResourceManager::Get().GetPlane();
auto model   = ResourceManager::Get().LoadModel(L"Assets/sword.obj");
auto texture = ResourceManager::Get().GetOrLoadTexture(L"Textures/diffuse.png");

ResourceManager::Get().ReleaseUploadBuffers();  // call after WaitForGPU
```

---

## Requirements

| | |
|---|---|
| OS | Windows 10 / 11 |
| GPU | DirectX 12 compatible |
| IDE | Visual Studio 2022 |
| SDK | Windows SDK 10.0+ |
| Dependencies | **None** — Win32 + DX12 + D3DCompiler only |

---

## Project Structure

```
vibe_engine/
├── Engine/
│   ├── Core/
│   │   ├── Application.h/.cpp      — main loop, OnInit/Update/Render/Resize hooks
│   │   ├── Window.h/.cpp           — Win32 window, focus tracking
│   │   ├── GameObject.h/.cpp       — entity with layer + tag + components
│   │   ├── Component.h             — base class, SetEnabled lifecycle
│   │   ├── Transform.h/.cpp        — quaternion rotation, hierarchy, LookAt
│   │   ├── Scene.h/.cpp            — full query API, deferred destroy
│   │   ├── SceneManager.h/.cpp     — scene registry singleton
│   │   ├── ResourceManager.h/.cpp  — Mesh/Texture cache
│   │   ├── Layer.h                 — LayerMask bitmask system
│   │   ├── GameplayTag.h/.cpp      — hierarchical dot-tag system
│   │   ├── Delegate.h              — Delegate<> + Event<> (Unity style)
│   │   ├── EventBus.h              — global type-erased pub/sub
│   │   ├── Profiler.h/.cpp         — PROFILE_SCOPE, EMA smoothing
│   │   └── Log.h/.cpp              — LOG_INFO/WARN/ERROR/TRACE
│   ├── Renderer/
│   │   ├── DX12Context.h/.cpp      — device, swap chain, frame loop
│   │   ├── BasicPipeline.h/.cpp    — root signature + PSO
│   │   ├── Mesh.h/.cpp             — VB/IB, geometry factories
│   │   ├── MeshRenderer.h/.cpp     — component, MVP cbuffer, draw
│   │   ├── Material.h/.cpp         — PBR params cbuffer + texture
│   │   ├── Texture.h/.cpp          — WIC loader, SRV heap
│   │   ├── Camera.h/.cpp           — FPS camera component
│   │   ├── LightManager.h/.cpp     — directional light cbuffer
│   │   └── OBJLoader.h/.cpp        — OBJ mesh importer
│   └── Input/
│       └── InputManager.h/.cpp     — keyboard + mouse polling
├── Assets/
│   ├── Shaders/Basic.hlsl          — Phong lit + texture shader
│   └── Textures/checkerboard.png
├── Sandbox/
│   ├── SandboxApp.h/.cpp           — demo: cube + floor, FPS camera
│   └── main.cpp
└── tools/
    └── verify_render.py            — screenshot visual verification
```

---

## Roadmap

### ✅ Tier 1 — Rendering Foundation
- [x] DX12 initialization (device / swap chain / fence / depth buffer)
- [x] Texture system (PNG → WIC → DX12 SRV)
- [x] Phong directional lighting
- [x] Material system (albedo / roughness / metallic / emissive)
- [x] Camera aspect ratio on resize
- [x] OBJ model loading

### ✅ Tier 2 — Engine Core
- [x] Resource manager (Mesh + Texture cache)
- [x] Delegate / Event system (Unity naming)
- [x] Global EventBus (type-erased pub/sub)
- [x] Profiler (PROFILE_SCOPE, EMA smoothing)
- [x] Log system (4 levels, timestamps, file output)
- [x] Component enable / disable (OnEnable / OnDisable)
- [x] Layer system (LayerMask bitmask, 0-31)
- [x] GameplayTag (Unreal-style hierarchical dot-tags)
- [x] Scene query API (name / tag / layer / component / predicate)
- [x] Transform quaternion (gimbal-lock-free, LookAt, GetForward)

### 🔲 Tier 3 — Gameplay Layer
- [ ] Physics / AABB collision + Rigidbody
- [ ] Audio (XAudio2, AudioSource / AudioListener)
- [ ] UI system (2D overlay, text, Button / Label)
- [ ] Animation (Transform keyframe tween)
- [ ] Scene serialization (JSON save / load)

### 🔲 Tier 4 — Editor / Tools
- [ ] ImGui in-game debugger (scene hierarchy, inspector)
- [ ] Shader hot-reload
- [ ] GPU timestamp queries
- [ ] glTF / FBX importer
