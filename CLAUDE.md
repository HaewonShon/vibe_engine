# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## Build Commands

### 컴파일러 — Visual Studio 2026 (버전 18) 고정

이 프로젝트는 **Visual Studio 2026 (내부 버전 18)** 을 사용한다.  
MSBuild 경로는 아래로 고정되어 있으며, `vswhere`, `vsdevcmd`, 또는 PATH에서 자동 탐색하면 VS2022(v17) 등 다른 버전이 선택될 수 있으므로 **반드시 전체 경로를 하드코딩**한다.

```
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe
```

> ⚠️ `Microsoft Visual Studio\17\`(VS2022) 경로를 사용하면 toolset 불일치로 빌드가 실패한다.

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"

# Engine (정적 라이브러리)
& $msbuild vibe_engine.sln /p:Configuration=Debug /p:Platform=x64 /t:Engine /m /nologo /v:minimal

# Sandbox (실행 파일) — Engine 빌드 후 실행
& $msbuild vibe_engine.sln /p:Configuration=Debug /p:Platform=x64 /t:Sandbox /m /nologo /v:minimal
```

- 출력물: `bin\Debug-x64\Engine.lib` / `bin\Debug-x64\Sandbox.exe`
- 셰이더·텍스처·모델은 PostBuildEvent가 `Assets\*` → `bin\Debug-x64\` 로 자동 복사한다.
- PowerShell에서 `/t:Engine;Sandbox`처럼 세미콜론 구분자는 오류가 난다. 프로젝트를 분리해서 빌드한다.

vcpkg 최초 설치 (이미 완료된 상태):
```powershell
C:\vcpkg\vcpkg.exe install --triplet x64-windows --x-manifest-root "C:\Users\Laptop2024\Desktop\vibe_engine" --x-install-root "C:\Users\Laptop2024\Desktop\vibe_engine\vcpkg_installed"
```

---

## Architecture Overview

### 프로젝트 구조

```
Engine/          — 정적 라이브러리 (VibeEngine 네임스페이스)
  Core/          — Application 루프, ECS, Scene/SceneManager, 직렬화, 이벤트, Log, Profiler
  Renderer/      — DX12 렌더링 전체 (Context, Pipeline, Mesh, Material, Texture, Shadow, GPU Profiler)
  Input/         — 키보드/마우스 폴링, 포커스 aware pause
  Physics/       — JoltPhysics 5.5.0 래퍼 (Box/Sphere/Capsule, 트리거, 콜리전 콜백)
  Audio/         — XAudio2 (AudioManager, AudioSource, AudioListener, 3D 스페이셜)
  Animation/     — 키프레임 Animator + 골격 HierarchicalAnimator (CrossfadeTo 크로스페이드)
  UI/            — DX12 2D 오버레이 패스 (UICanvas / Panel / Label / Button), GDI 폰트 아틀라스

Sandbox/         — 데모 애플리케이션 (Physics Demo + Skeletal Demo)
Assets/Shaders/  — Basic.hlsl (3D Phong+shadow), UI.hlsl (2D 오버레이), Shadow.hlsl (깊이 패스)
Assets/Textures/ — 런타임 텍스처 (checkerboard.png 등)
Assets/Models/   — 런타임 모델 (FBX, glTF, OBJ)
tools/           — verify_render.py (스크린샷 시각 검증)
```

### Application 생명주기

`Application`을 상속해서 훅을 오버라이드하는 방식이다.

```
Run() → OnInit → [OnPreUpdate → OnUpdate → OnRender] 반복 → OnShutdown
         ↑ 물리 스텝은 OnPreUpdate에서 PhysicsWorld::Get().Step(dt)
         ↑ Scene::Update / Render는 OnUpdate / OnRender 안에서 수동 호출
```

컴포넌트 생명주기: `Awake → OnEnable → Start → Update → OnDisable → OnDestroy`

### DX12 렌더링 파이프라인

매 프레임 순서:

```
[핫 리로드 체크] HasShaderChanged() → WaitForGPU() + HotReload()
BeginFrame()           — 커맨드 리스트 리셋, RT 배리어, 뷰포트/SRV 힙 바인딩
  Shadow pass          — ShadowMap::BeginShadowPass → 씬 MeshRenderer::DrawShadow → EndShadowPass
  BindMainRenderTarget — RT 복구 (뷰포트 포함)
  Scene::Render()      — Camera 탐색 → MeshRenderer::Draw(viewProj) 순회
  UIRenderer pass
  ImGuiLayer pass
  GPUProfiler::Resolve
EndFrame()             — 배리어, Present, MoveToNextFrame
```

### 루트 시그니처 (BasicPipeline — Basic.hlsl)

| 슬롯 | 타입 | 레지스터 | 가시성 | 내용 |
|------|------|----------|--------|------|
| [0] | Root CBV | b0 | VS | PerObjectCB { MVP, World, LightMVP } |
| [1] | Root CBV | b1 | PS | LightCB { Direction, Intensity, Color, Ambient, CameraPos } |
| [2] | Root CBV | b2 | PS | MaterialCB { Albedo, Roughness, Metallic, Emissive } |
| [3] | Table 1 SRV | t0 | PS | 알베도 텍스처 |
| [4] | Table 1 SRV | t1 | PS | 섀도우 맵 (R32_FLOAT) |
| [5] | Table 1 SRV | t2 | PS | IBL Irradiance TextureCube |
| [6] | Table 1 SRV | t3 | PS | IBL Specular (prefiltered) TextureCube |
| [7] | Table 1 SRV | t4 | PS | BRDF LUT Texture2D |
| [8] | Table 1 SRV | t5 | PS | Normal Map Texture2D |
| s0 | Static Sampler | — | PS | Linear Wrap (알베도 + 노멀 맵) |
| s1 | Static Sampler | — | PS | Comparison LESS_EQUAL + Border White (PCF) |
| s2 | Static Sampler | — | PS | Linear Clamp (IBL cubemap + BRDF LUT) |

루트 시그니처를 바꾸면 BasicPipeline.cpp의 `CreateRootSignature()`와 Basic.hlsl cbuffer를 동시에 수정해야 한다.

### 셰이더

- **런타임 컴파일**: `Shader::CompileFromFile(path, entry, "vs_5_0")` — d3dcompiler.lib 사용
- **Hot Reload**: `ShaderWatcher`(백그라운드 스레드)가 Shaders/ 디렉토리 변경 감시, `BasicPipeline::HotReload()` 호출
- **셰이더 행렬 규칙**: HLSL은 `mul(vector, matrix)`, C++ 업로드 시 `XMMatrixTranspose()` 적용 — Basic.hlsl·UI.hlsl·Shadow.hlsl 모두 동일
- **노멀 맵 TBN**: VSMain에서 `WorldTangent/WorldBitangent = mul(T/B, (float3x3)World)`. PSMain에서 Gram-Schmidt 재직교화 후 `mul(tangentNormal, TBN)` — 노멀 맵 없는 경우 `m_FlatNormal`(1×1, 128,128,255) 폴백

### 주요 싱글턴

`ResourceManager`, `SceneManager`, `LightManager`, `PhysicsWorld`, `AudioManager`, `UIRenderer`, `InputManager`, `EventBus`  
모두 `::Get()` 정적 메서드로 접근한다.

### 섀도우 맵 패턴 (DX12 듀얼 뷰)

깊이 텍스처를 DSV(쓰기)와 SRV(읽기)로 동시에 사용하려면 포맷을 `R32_TYPELESS`로 생성해야 한다.
- DSV: `DXGI_FORMAT_D32_FLOAT`
- SRV: `DXGI_FORMAT_R32_FLOAT`

---

## Key Conventions

### 행렬 · 좌표계

- DirectXMath row-major 규칙: `mul(v, M)` (행 벡터)
- GPU 업로드 전 반드시 `XMMatrixTranspose()`
- DX12는 왼손 좌표계(Y-up). FBX 임포트 시 Assimp `aiProcess_FlipUVs` 적용, glTF는 `aiProcess_MakeLeftHanded` + FlipUVs 없음

### DX12 자원 관리

- 새 상수 버퍼: `HEAP_TYPE_UPLOAD` + 영구 Map, `AlignTo256(sizeof(CB))` 크기
- SRV 할당: `DX12Context::AllocateSRV()` — 공유 SRV 힙에서 순차 슬롯 반환
- PSO 교체(Hot Reload, ShadowPipeline 등) 전 반드시 `WaitForGPU()` 호출
- 중간 패스가 `OMSetRenderTargets`를 바꾼 뒤에는 `DX12Context::BindMainRenderTarget(cmdList)`으로 복구

### 새 렌더러 클래스 추가 시

1. `Engine/Renderer/Foo.h`, `Foo.cpp` 생성
2. `Engine/Engine.vcxproj`에 `ClInclude` + `ClCompile` 항목 추가
3. `SandboxApp.h`에 멤버 선언, `OnInit`에서 초기화, `OnShutdown`에서 `Destroy()`/`Shutdown()` 호출
4. MeshRenderer와 씬 오브젝트 연결이 필요하면 `scene->FindComponentsOfType<MeshRenderer>()` 순회

### 외부 의존성

| 라이브러리 | 용도 | 비고 |
|-----------|------|------|
| JoltPhysics 5.5.0 | 강체 물리 | vcpkg |
| Assimp 6.0.4 | FBX / glTF / OBJ 임포트 | vcpkg |
| Dear ImGui 1.92.8 | 인게임 디버거 | vcpkg, `docking-experimental` feature 필수 |
| DirectXMath | 수학 | Windows SDK 내장 |
| WIC (wincodec.h) | 텍스처 디코딩 (PNG/JPEG/GLB 임베드) | Windows SDK 내장, `ole32.lib` |
| XAudio2 | 오디오 | Windows SDK 내장 |
| d3dcompiler | 런타임 셰이더 컴파일 | Windows SDK 내장 |

### 금지 사항

- `git push --force` 금지
- `WaitForGPU()` 없이 in-flight PSO / 리소스 교체 금지
- `Engine/Engine.vcxproj`와 `Sandbox/Sandbox.vcxproj` 직접 편집 후 빌드 확인 없이 커밋 금지
- UI.hlsl에서 `mul(g_OrthoProj, v)` 열 벡터 곱 금지 — 반드시 `mul(v, g_OrthoProj)` 행 벡터 곱

---

## Build Output Verification

빌드 후 렌더링이 정상인지 확인할 때는 `tools/verify_render.py`를 사용한다.  
이 스크립트는 Sandbox.exe를 실행하고, **오른쪽 모니터**로 창을 이동시킨 뒤 스크린샷을 찍어 Claude Vision API로 판정한다.

### 사전 요구사항 (최초 1회)

```powershell
pip install anthropic pillow pywin32
$env:ANTHROPIC_API_KEY = "sk-ant-..."   # 또는 시스템 환경 변수로 등록
```

### 실행 방법

```powershell
# 기본 (빌드 후 바로 검증 — 오른쪽 모니터 사용)
cd C:\Users\Laptop2024\Desktop\vibe_engine
python tools/verify_render.py

# 이미 Sandbox가 실행 중일 때
python tools/verify_render.py --no-launch

# 스크린샷 파일로 저장
python tools/verify_render.py --save-screenshot tools/capture.png

# 왼쪽 모니터로 캡처하고 싶을 때 (기본값은 right)
python tools/verify_render.py --monitor left
```

### 옵션 요약

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--exe` | `bin/Debug-x64/Sandbox.exe` | 실행 파일 경로 |
| `--monitor` | `right` | 캡처할 모니터 (`right` / `left` / `0` / `1` …) |
| `--no-launch` | — | Sandbox를 새로 띄우지 않음 |
| `--timeout` | 15s | 창 탐색 대기 시간 |
| `--save-screenshot` | — | PNG 저장 경로 |

`--monitor right` 는 연결된 모니터들을 X 좌표 기준으로 정렬해 **가장 오른쪽** 모니터를 선택한다. 모니터가 1대뿐이면 자동으로 그 모니터를 사용한다.

### 정상 판정 기준

스크립트가 Claude Vision에 확인하는 항목:
1. 3D 오브젝트(큐브·바닥·모델)가 보이는가
2. 조명·그림자가 적용되어 있는가
3. 배경이 짙은 단색인가
4. ImGui 패널(Hierarchy / Inspector)이 보이는가
5. 아티팩트·깨짐·빈 화면이 없는가

---

## Running the Demo

```powershell
cd C:\Users\Laptop2024\Desktop\vibe_engine\bin\Debug-x64
.\Sandbox.exe
```

데모 조작:
- `WASD / QE` — 카메라 이동, 마우스 — 시점
- `R` — 씬 재시작, `Tab` — Physics ↔ Skeletal 데모 전환
- `T` — 스켈레탈 클립 크로스페이드 (walk ↔ wave)
- `S / L` — 씬 저장/불러오기
- ImGui 메뉴바 → Layout → Reset to Default (패널 레이아웃 초기화)
- ImGui 메뉴바 → Shader → Force Hot Reload
