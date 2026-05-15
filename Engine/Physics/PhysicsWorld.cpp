#pragma warning(push, 0)   // suppress Jolt's MSVC warnings
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#pragma warning(pop)

#include "PhysicsWorld.h"
#include "../Core/Log.h"
#include <thread>

JPH_SUPPRESS_WARNINGS

using namespace JPH;
using namespace DirectX;

namespace VibeEngine {

// ============================================================================
// Physics object layers
// Two layers only: STATIC (floor, walls) and DYNAMIC (anything that moves).
// ============================================================================
namespace PhysicsLayers {
    static constexpr ObjectLayer STATIC  = 0;
    static constexpr ObjectLayer DYNAMIC = 1;
}

// ---- BroadPhase layer interface --------------------------------------------
class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override { return 2; }

    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {
        return BroadPhaseLayer(layer == PhysicsLayers::STATIC ? 0u : 1u);
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer layer) const override {
        return (layer == BroadPhaseLayer(0)) ? "STATIC" : "DYNAMIC";
    }
#endif
};

// ---- Object vs BroadPhase filter -------------------------------------------
class ObjectVsBPFilterImpl final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer obj, BroadPhaseLayer bp) const override {
        // Static objects only need to collide against dynamic broadphase.
        // Dynamic objects collide with everything.
        if (obj == PhysicsLayers::STATIC)
            return bp == BroadPhaseLayer(1); // dynamic BP only
        return true;
    }
};

// ---- Object layer pair filter -----------------------------------------------
class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
        // Static-static: no collision.
        if (a == PhysicsLayers::STATIC && b == PhysicsLayers::STATIC)
            return false;
        return true;
    }
};

// ============================================================================
// Impl — owns all Jolt objects
// ============================================================================
struct PhysicsWorld::Impl {
    BPLayerInterfaceImpl           bpLayerInterface;
    ObjectVsBPFilterImpl           objectVsBPFilter;
    ObjectLayerPairFilterImpl      objectLayerFilter;

    std::unique_ptr<TempAllocatorImpl>    tempAllocator;
    std::unique_ptr<JobSystemThreadPool>  jobSystem;
    std::unique_ptr<PhysicsSystem>        physicsSystem;

    BodyInterface* bodyInterface = nullptr;
};

// ============================================================================
// PhysicsWorld
// ============================================================================

PhysicsWorld& PhysicsWorld::Get()
{
    static PhysicsWorld inst;
    return inst;
}

void PhysicsWorld::Initialize(float gravity)
{
    if (m_Initialized) Shutdown();

    // Jolt global state — safe to init once per process lifetime.
    static bool s_JoltGlobalInit = false;
    if (!s_JoltGlobalInit) {
        RegisterDefaultAllocator();
        Factory::sInstance = new Factory();
        RegisterTypes();
        s_JoltGlobalInit = true;
        LOG_INFO("Jolt global state initialised.");
    }

    m_Impl = std::make_unique<Impl>();

    // 16 MB scratch buffer for the broadphase / contact solver
    m_Impl->tempAllocator = std::make_unique<TempAllocatorImpl>(16u * 1024u * 1024u);

    // Job pool: use all cores minus one for the physics worker threads
    const int workerThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    m_Impl->jobSystem = std::make_unique<JobSystemThreadPool>(
        cMaxPhysicsJobs, cMaxPhysicsBarriers, workerThreads);

    // Physics system
    m_Impl->physicsSystem = std::make_unique<PhysicsSystem>();
    m_Impl->physicsSystem->Init(
        /*maxBodies*/            1024,
        /*numBodyMutexes*/       0,     // 0 = Jolt picks automatically
        /*maxBodyPairs*/         4096,
        /*maxContactConstraints*/1024,
        m_Impl->bpLayerInterface,
        m_Impl->objectVsBPFilter,
        m_Impl->objectLayerFilter);

    m_Impl->physicsSystem->SetGravity(Vec3(0.0f, gravity, 0.0f));
    m_Impl->bodyInterface = &m_Impl->physicsSystem->GetBodyInterface();

    m_Initialized = true;
    LOG_INFO("PhysicsWorld initialised (gravity=%.2f, threads=%d).", gravity, workerThreads);
}

void PhysicsWorld::Shutdown()
{
    if (!m_Initialized) return;
    m_Impl.reset();
    m_Initialized = false;
    LOG_INFO("PhysicsWorld shut down.");
}

void PhysicsWorld::Update(float dt, int collisionSteps)
{
    if (!m_Initialized) return;
    m_Impl->physicsSystem->Update(
        dt, collisionSteps,
        m_Impl->tempAllocator.get(),
        m_Impl->jobSystem.get());
}

// ---- Helpers ---------------------------------------------------------------
static inline BodyID ToBodyID(uint32_t id) { return BodyID(id); }

// ---- Body factory ----------------------------------------------------------

uint32_t PhysicsWorld::CreateBox(const XMFLOAT3& halfExtents, const XMFLOAT3& pos,
                                  const XMFLOAT4& rot, bool isStatic,
                                  float restitution, float friction)
{
    if (!m_Initialized) return INVALID_PHYSICS_BODY;

    BoxShapeSettings shapeSettings(Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
    auto shapeResult = shapeSettings.Create();
    if (shapeResult.HasError()) {
        LOG_ERROR("PhysicsWorld::CreateBox — shape error: %s", shapeResult.GetError().c_str());
        return INVALID_PHYSICS_BODY;
    }

    EMotionType motionType = isStatic ? EMotionType::Static : EMotionType::Dynamic;
    ObjectLayer  layer     = isStatic ? PhysicsLayers::STATIC : PhysicsLayers::DYNAMIC;

    BodyCreationSettings settings(
        shapeResult.Get(),
        RVec3(pos.x, pos.y, pos.z),
        Quat(rot.x, rot.y, rot.z, rot.w),
        motionType, layer);

    settings.mRestitution = restitution;
    settings.mFriction    = friction;

    Body* body = m_Impl->bodyInterface->CreateBody(settings);
    if (!body) { LOG_ERROR("PhysicsWorld::CreateBox — body limit reached."); return INVALID_PHYSICS_BODY; }

    EActivation act = isStatic ? EActivation::DontActivate : EActivation::Activate;
    m_Impl->bodyInterface->AddBody(body->GetID(), act);
    return body->GetID().GetIndexAndSequenceNumber();
}

uint32_t PhysicsWorld::CreateSphere(float radius, const XMFLOAT3& pos, bool isStatic,
                                     float restitution, float friction)
{
    if (!m_Initialized) return INVALID_PHYSICS_BODY;

    SphereShapeSettings shapeSettings(radius);
    auto shapeResult = shapeSettings.Create();
    if (shapeResult.HasError()) {
        LOG_ERROR("PhysicsWorld::CreateSphere — shape error: %s", shapeResult.GetError().c_str());
        return INVALID_PHYSICS_BODY;
    }

    EMotionType motionType = isStatic ? EMotionType::Static : EMotionType::Dynamic;
    ObjectLayer  layer     = isStatic ? PhysicsLayers::STATIC : PhysicsLayers::DYNAMIC;

    BodyCreationSettings settings(
        shapeResult.Get(),
        RVec3(pos.x, pos.y, pos.z),
        Quat::sIdentity(),
        motionType, layer);

    settings.mRestitution = restitution;
    settings.mFriction    = friction;

    Body* body = m_Impl->bodyInterface->CreateBody(settings);
    if (!body) { LOG_ERROR("PhysicsWorld::CreateSphere — body limit reached."); return INVALID_PHYSICS_BODY; }

    EActivation act = isStatic ? EActivation::DontActivate : EActivation::Activate;
    m_Impl->bodyInterface->AddBody(body->GetID(), act);
    return body->GetID().GetIndexAndSequenceNumber();
}

void PhysicsWorld::DestroyBody(uint32_t id)
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return;
    BodyID bid = ToBodyID(id);
    m_Impl->bodyInterface->RemoveBody(bid);
    m_Impl->bodyInterface->DestroyBody(bid);
}

// ---- Transform readback ----------------------------------------------------

XMFLOAT3 PhysicsWorld::GetPosition(uint32_t id) const
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return {};
    RVec3 p = m_Impl->bodyInterface->GetCenterOfMassPosition(ToBodyID(id));
    return XMFLOAT3(static_cast<float>(p.GetX()),
                    static_cast<float>(p.GetY()),
                    static_cast<float>(p.GetZ()));
}

XMFLOAT4 PhysicsWorld::GetRotation(uint32_t id) const
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return { 0,0,0,1 };
    Quat q = m_Impl->bodyInterface->GetRotation(ToBodyID(id));
    return XMFLOAT4(q.GetX(), q.GetY(), q.GetZ(), q.GetW());
}

// ---- Forces ----------------------------------------------------------------

void PhysicsWorld::AddForce(uint32_t id, const XMFLOAT3& force)
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return;
    m_Impl->bodyInterface->AddForce(ToBodyID(id), Vec3(force.x, force.y, force.z));
}

void PhysicsWorld::AddImpulse(uint32_t id, const XMFLOAT3& impulse)
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return;
    m_Impl->bodyInterface->AddImpulse(ToBodyID(id), Vec3(impulse.x, impulse.y, impulse.z));
}

void PhysicsWorld::SetLinearVelocity(uint32_t id, const XMFLOAT3& vel)
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return;
    m_Impl->bodyInterface->SetLinearVelocity(ToBodyID(id), Vec3(vel.x, vel.y, vel.z));
}

void PhysicsWorld::SetAngularVelocity(uint32_t id, const XMFLOAT3& vel)
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return;
    m_Impl->bodyInterface->SetAngularVelocity(ToBodyID(id), Vec3(vel.x, vel.y, vel.z));
}

} // namespace VibeEngine
