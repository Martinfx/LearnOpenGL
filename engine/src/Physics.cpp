#include "engine/Physics.h"

#include <glm/gtc/matrix_transform.hpp>
#include "PhysicsImpl.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <thread>

namespace engine {

namespace {

void initJoltOnce()
{
    static std::once_flag flag;
    std::call_once(flag, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

} // namespace

JPH::BroadPhaseLayer BroadPhaseLayersImpl::GetBroadPhaseLayer(JPH::ObjectLayer layer) const
{
    return layer == Layers::Static ? BroadPhaseLayers::Static : BroadPhaseLayers::Moving;
}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
const char* BroadPhaseLayersImpl::GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const
{
    return layer == BroadPhaseLayers::Static ? "Static" : "Moving";
}
#endif

bool ObjectVsBroadPhaseFilter::ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const
{
    // Static never collides with static.
    return layer != Layers::Static || bp != BroadPhaseLayers::Static;
}

bool ObjectPairFilter::ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const
{
    return a != Layers::Static || b != Layers::Static;
}

PhysicsWorld::Impl::Impl()
{
    initJoltOnce();
    tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
    system.Init(/*maxBodies*/ 8192, /*mutexes*/ 0, /*maxBodyPairs*/ 8192, /*maxContactConstraints*/ 4096,
                broadPhaseLayers, objectVsBroadPhase, objectPairs);
    system.SetGravity(JPH::Vec3(0.0f, -18.0f, 0.0f));
}

PhysicsWorld::Impl::~Impl()
{
    clearStatic();
}

void PhysicsWorld::Impl::clearStatic()
{
    JPH::BodyInterface& bodies = system.GetBodyInterface();
    for (const JPH::BodyID& id : staticBodies) {
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
    }
    staticBodies.clear();
}

void PhysicsWorld::Impl::addStatic(const JPH::ShapeSettings& settings, glm::vec3 position, const std::string& name)
{
    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError()) {
        std::fprintf(stderr, "Physics: skipping '%s': %s\n", name.c_str(), result.GetError().c_str());
        return;
    }
    JPH::BodyCreationSettings body(result.Get(), toJolt(position), JPH::Quat::sIdentity(),
                                   JPH::EMotionType::Static, Layers::Static);
    const JPH::BodyID id = system.GetBodyInterface().CreateAndAddBody(body, JPH::EActivation::DontActivate);
    if (!id.IsInvalid())
        staticBodies.push_back(id);
}

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::setLevel(const Scene& scene)
{
    impl_->clearStatic();
    for (const Box& b : scene.boxes) {
        const glm::vec3 half = glm::max(b.size * 0.5f, glm::vec3(0.005f));
        const float convexRadius = std::min(JPH::cDefaultConvexRadius, std::min({half.x, half.y, half.z}) * 0.5f);
        impl_->addStatic(JPH::BoxShapeSettings(toJolt(half), convexRadius), b.position, b.name);
    }
    for (const Model& m : scene.models) {
        for (const MeshPart& part : m.parts) {
            JPH::VertexList vertices;
            vertices.reserve(part.positions.size());
            // Rotation and scale are baked into the vertices; the body sits at the model position.
            const glm::mat3 rotScale(m.transform());
            for (const glm::vec3& p0 : part.positions) {
                const glm::vec3 p = rotScale * p0;
                vertices.push_back(JPH::Float3(p.x, p.y, p.z));
            }
            JPH::IndexedTriangleList triangles;
            triangles.reserve(part.indices.size() / 3);
            for (std::size_t i = 0; i + 2 < part.indices.size(); i += 3)
                triangles.push_back(JPH::IndexedTriangle(part.indices[i], part.indices[i + 1], part.indices[i + 2], 0));
            impl_->addStatic(JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)), m.position, m.name);
        }
    }
    impl_->system.OptimizeBroadPhase();
}

void PhysicsWorld::step(float dt)
{
    impl_->system.Update(dt, 1, impl_->tempAllocator.get(), impl_->jobs.get());
}

std::optional<RayHit> PhysicsWorld::raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance) const
{
    const glm::vec3 dir = glm::normalize(direction) * maxDistance;
    JPH::RRayCast ray(toJolt(origin), toJolt(dir));
    JPH::RayCastResult hit;
    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit))
        return std::nullopt;
    return RayHit{toGlm(ray.GetPointOnRay(hit.mFraction)), hit.mFraction * maxDistance};
}

} // namespace engine
