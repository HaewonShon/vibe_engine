# VibeEngine

A DirectX 12-based game engine with a Unity-style architecture, built through vibe coding.

## Features (Planned)
- DirectX 12 rendering pipeline
- Component-based entity system (GameObject / Component)
- Scene management
- Transform hierarchy
- Input system
- Asset management

## Requirements
- Windows 10/11
- DirectX 12 compatible GPU
- Visual Studio 2022
- Windows SDK 10.0+

## Project Structure
```
vibe_engine/
├── Engine/         # Core engine library
│   ├── Core/       # ECS, Scene, GameObject
│   ├── Renderer/   # DX12 rendering backend
│   ├── Input/      # Input system
│   └── Assets/     # Asset management
├── Editor/         # Engine editor (future)
└── Sandbox/        # Test application
```
