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
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#pragma warning(pop)

#include "PhysicsWorld.h"
#include "Rigidbody.h"
#include "../Core/Log.h"
#include <thread>
#include <mutex>
#include <unordered_map>

JPH_SUPPRESS_WARNINGS

using namespace JPH;
using namespace DirectX;

namespace VibeEngine {

// ============================================================================
// Physics object layers
// STATIC  — immovable geometry (floor, walls)
// DYNAMIC — anything that moves (includes kinematic)
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
        if (obj == PhysicsLayers::STATIC)
            return bp == BroadPhaseLayer(1);
        return true;
    }
};

// ---- Object layer pair filter -----------------------------------------------
class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
        if (a == PhysicsLayers::STATIC && b == PhysicsLayers::STATIC)
            return false;
        return true;
    }
};

// ============================================================================
// Contact listener — collects events from the Jolt physics thread.
// Events are dispatched to Rigidbody components after Update() returns.
// ============================================================================
class ContactListenerImpl final : public ContactListener {
public:
    struct Event {
        uint32_t body1;
        uint32_t body2;
        bool     isEnter;    // true = OnContactAdded, false = OnContactRemoved
        bool     isTrigger;  // true = at least one body is a sensor
    };

    // Called from physics threads — protect with mutex.
    std::mutex         m_Mutex;
    std::vector<Event> m_Events;

    // Tracks whether a contact pair involves a sensor; needed on Remove because
    // OnContactRemoved gives only SubShapeIDPair (no Body objects).
    std::unordered_map<uint64_t, bool> m_ActivePairs;

    static uint64_t PairKey(uint32_t a, uint32_t b)
    {
        // Normalise so {a,b} and {b,a} map to the same key.
        return a < b ? (uint64_t(a) << 32 | uint64_t(b))
                     : (uint64_t(b) << 32 | uint64_t(a));
    }

    ValidateResult OnContactValidate(
        const Body&, const Body&, RVec3Arg, const CollideShapeResult&) override
    {
        return ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(
        const Body& b1, const Body& b2,
        const ContactManifold&, ContactSettings&) override
    {
        uint32_t id1 = b1.GetID().GetIndexAndSequenceNumber();
        uint32_t id2 = b2.GetID().GetIndexAndSequenceNumber();
        bool isTrigger = b1.IsSensor() || b2.IsSensor();

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ActivePairs[PairKey(id1, id2)] = isTrigger;
        m_Events.push_back({ id1, id2, true, isTrigger });
    }

    void OnContactPersisted(
        const Body&, const Body&,
        const ContactManifold&, ContactSettings&) override {}

    void OnContactRemoved(const SubShapeIDPair& pair) override
    {
        uint32_t id1 = pair.GetBody1ID().GetIndexAndSequenceNumber();
        uint32_t id2 = pair.GetBody2ID().GetIndexAndSequenceNumber();

        std::lock_guard<std::mutex> lock(m_Mutex);
        uint64_t key = PairKey(id1, id2);
        bool isTrigger = false;
        auto it = m_ActivePairs.find(key);
        if (it != m_ActivePairs.end()) {
            isTrigger = it->second;
            m_ActivePairs.erase(it);
        }
        m_Events.push_back({ id1, id2, false, isTrigger });
    }
};

// ============================================================================
// Impl — owns all Jolt objects
// ============================================================================
struct PhysicsWorld::Impl {
    BPLayerInterfaceImpl           bpLayerInterface;
    ObjectVsBPFilterImpl           objectVsBPFilter;
    ObjectLayerPairFilterImpl      objectLayerFilter;
    ContactListenerImpl            contactListener;

    std::unique_ptr<TempAllocatorImpl>    tempAllocator;
    std::unique_ptr<JobSystemThreadPool>  jobSystem;
    std::unique_ptr<PhysicsSystem>        physicsSystem;

    BodyInterface* bodyInterface = nullptr;

    // Maps uint32_t body handle → Rigidbody component (registered in Start, removed in OnDestroy)
    std::unordered_map<uint32_t, Rigidbody*> bodyRegistry;
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

    static bool s_JoltGlobalInit = false;
    if (!s_JoltGlobalInit) {
        RegisterDefaultAllocator();
        Factory::sInstance = new Factory();
        RegisterTypes();
        s_JoltGlobalInit = true;
        LOG_INFO("Jolt global state initialised.");
    }

    m_Impl = std::make_unique<Impl>();

    m_Impl->tempAllocator = std::make_unique<TempAllocatorImpl>(16u * 1024u * 1024u);

    const int workerThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    m_Impl->jobSystem = std::make_unique<JobSystemThreadPool>(
        cMaxPhysicsJobs, cMaxPhysicsBarriers, workerThreads);

    m_Impl->physicsSystem = std::make_unique<PhysicsSystem>();
    m_Impl->physicsSystem->Init(
        /*maxBodies*/            1024,
        /*numBodyMutexes*/       0,
        /*maxBodyPairs*/         4096,
        /*maxContactConstraints*/1024,
        m_Impl->bpLayerInterface,
        m_Impl->objectVsBPFilter,
        m_Impl->objectLayerFilter);

    m_Impl->physicsSystem->SetGravity(Vec3(0.0f, gravity, 0.0f));
    m_Impl->physicsSystem->SetContactListener(&m_Impl->contactListener);
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

    // Dispatch contact events accumulated during this step.
    DispatchContactEvents();
}

// ---- Helpers ---------------------------------------------------------------
static inline BodyID ToBodyID(uint32_t id) { return BodyID(id); }

// Helper shared by all Create* functions: applies mass override and sensor flag.
static void ApplyCommonSettings(BodyCreationSettings& settings,
                                float mass, bool isSensor, bool isStatic)
{
    settings.mIsSensor = isSensor;
    if (mass > 0.f && !isStatic) {
        settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = mass;
    }
}

// Helper: create body, add to world, return handle.
static uint32_t FinaliseBody(BodyInterface* bi, const BodyCreationSettings& settings,
                              bool isStatic, const char* caller)
{
    Body* body = bi->CreateBody(settings);
    if (!body) {
        LOG_ERROR("%s — body limit reached.", caller);
        return INVALID_PHYSICS_BODY;
    }
    EActivation act = isStatic ? EActivation::DontActivate : EActivation::Activate;
    bi->AddBody(body->GetID(), act);
    return body->GetID().GetIndexAndSequenceNumber();
}

// ---- Body factory ----------------------------------------------------------

uint32_t PhysicsWorld::CreateBox(const XMFLOAT3& halfExtents, const XMFLOAT3& pos,
                                  const XMFLOAT4& rot, bool isStatic,
                                  float mass, float restitution, float friction, bool isSensor)
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
    ApplyCommonSettings(settings, mass, isSensor, isStatic);

    return FinaliseBody(m_Impl->bodyInterface, settings, isStatic, "PhysicsWorld::CreateBox");
}

uint32_t PhysicsWorld::CreateSphere(float radius, const XMFLOAT3& pos, bool isStatic,
                                     float mass, float restitution, float friction, bool isSensor)
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
    ApplyCommonSettings(settings, mass, isSensor, isStatic);

    return FinaliseBody(m_Impl->bodyInterface, settings, isStatic, "PhysicsWorld::CreateSphere");
}

uint32_t PhysicsWorld::CreateCapsule(float radius, float halfHeight,
                                      const XMFLOAT3& pos, const XMFLOAT4& rot,
                                      bool isStatic,
                                      float mass, float restitution, float friction, bool isSensor)
{
    if (!m_Initialized) return INVALID_PHYSICS_BODY;

    // CapsuleShape(halfHeight, radius) — total height = 2*(halfHeight + radius)
    CapsuleShapeSettings shapeSettings(halfHeight, radius);
    auto shapeResult = shapeSettings.Create();
    if (shapeResult.HasError()) {
        LOG_ERROR("PhysicsWorld::CreateCapsule — shape error: %s", shapeResult.GetError().c_str());
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
    ApplyCommonSettings(settings, mass, isSensor, isStatic);

    return FinaliseBody(m_Impl->bodyInterface, settings, isStatic, "PhysicsWorld::CreateCapsule");
}

void PhysicsWorld::DestroyBody(uint32_t id)
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return;
    BodyID bid = ToBodyID(id);
    m_Impl->bodyInterface->RemoveBody(bid);
    m_Impl->bodyInterface->DestroyBody(bid);
}

// ---- Motion type -----------------------------------------------------------

void PhysicsWorld::SetBodyKinematic(uint32_t id, bool kinematic)
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return;
    EMotionType type = kinematic ? EMotionType::Kinematic : EMotionType::Dynamic;
    m_Impl->bodyInterface->SetMotionType(ToBodyID(id), type, EActivation::Activate);
}

// ---- Transform (runtime) ---------------------------------------------------

void PhysicsWorld::SetBodyPosition(uint32_t id, const XMFLOAT3& pos)
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return;
    m_Impl->bodyInterface->SetPosition(
        ToBodyID(id), RVec3(pos.x, pos.y, pos.z), EActivation::Activate);
}

void PhysicsWorld::SetBodyRotation(uint32_t id, const XMFLOAT4& rot)
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return;
    m_Impl->bodyInterface->SetRotation(
        ToBodyID(id), Quat(rot.x, rot.y, rot.z, rot.w), EActivation::Activate);
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

// ---- Velocity --------------------------------------------------------------

XMFLOAT3 PhysicsWorld::GetLinearVelocity(uint32_t id) const
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return {};
    Vec3 v = m_Impl->bodyInterface->GetLinearVelocity(ToBodyID(id));
    return XMFLOAT3(v.GetX(), v.GetY(), v.GetZ());
}

XMFLOAT3 PhysicsWorld::GetAngularVelocity(uint32_t id) const
{
    if (!m_Initialized || id == INVALID_PHYSICS_BODY) return {};
    Vec3 v = m_Impl->bodyInterface->GetAngularVelocity(ToBodyID(id));
    return XMFLOAT3(v.GetX(), v.GetY(), v.GetZ());
}

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

// ---- Raycasting ------------------------------------------------------------

RaycastHit PhysicsWorld::CastRay(
    const XMFLOAT3& origin, const XMFLOAT3& direction, float maxDistance) const
{
    RaycastHit out;
    if (!m_Initialized) return out;

    // direction should be normalised; scale by maxDistance to get the end point.
    RRayCast ray{
        RVec3(origin.x, origin.y, origin.z),
        Vec3(direction.x, direction.y, direction.z) * maxDistance
    };

    RayCastResult result;
    bool hit = m_Impl->physicsSystem->GetNarrowPhaseQuery().CastRay(ray, result);
    if (!hit) return out;

    out.hit      = true;
    out.bodyID   = result.mBodyID.GetIndexAndSequenceNumber();
    out.distance = result.mFraction * maxDistance;

    // World-space hit position
    RVec3 hitPt = ray.GetPointOnRay(result.mFraction);
    out.point = XMFLOAT3(static_cast<float>(hitPt.GetX()),
                         static_cast<float>(hitPt.GetY()),
                         static_cast<float>(hitPt.GetZ()));

    // Surface normal (requires a body lock for thread safety)
    {
        BodyLockRead lock(m_Impl->physicsSystem->GetBodyLockInterface(), result.mBodyID);
        if (lock.Succeeded()) {
            Vec3 n = lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, hitPt);
            out.normal = XMFLOAT3(n.GetX(), n.GetY(), n.GetZ());
        }
    }

    // Attach the registered Rigidbody component (may be nullptr for static floors etc.)
    out.rigidbody = GetRigidbody(out.bodyID);

    return out;
}

// ---- Body registry ---------------------------------------------------------

void PhysicsWorld::RegisterBody(uint32_t id, Rigidbody* rb)
{
    if (id != INVALID_PHYSICS_BODY)
        m_Impl->bodyRegistry[id] = rb;
}

void PhysicsWorld::UnregisterBody(uint32_t id)
{
    if (id != INVALID_PHYSICS_BODY)
        m_Impl->bodyRegistry.erase(id);
}

Rigidbody* PhysicsWorld::GetRigidbody(uint32_t id) const
{
    auto it = m_Impl->bodyRegistry.find(id);
    return it != m_Impl->bodyRegistry.end() ? it->second : nullptr;
}

// ---- Contact dispatch ------------------------------------------------------

void PhysicsWorld::DispatchContactEvents()
{
    // Swap out the event list without holding the lock during dispatch.
    std::vector<ContactListenerImpl::Event> events;
    {
        std::lock_guard<std::mutex> lock(m_Impl->contactListener.m_Mutex);
        std::swap(events, m_Impl->contactListener.m_Events);
    }

    for (auto& e : events) {
        Rigidbody* rb1 = GetRigidbody(e.body1);
        Rigidbody* rb2 = GetRigidbody(e.body2);
        if (!rb1 && !rb2) continue;   // neither side is a Rigidbody component

        if (e.isEnter) {
            if (rb1) rb1->DispatchContactBegin(rb2, e.isTrigger);
            if (rb2) rb2->DispatchContactBegin(rb1, e.isTrigger);
        } else {
            if (rb1) rb1->DispatchContactEnd(rb2, e.isTrigger);
            if (rb2) rb2->DispatchContactEnd(rb1, e.isTrigger);
        }
    }
}

} // namespace VibeEngine
