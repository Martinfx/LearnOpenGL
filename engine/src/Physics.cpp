#include "engine/Log.h"
#include "engine/Physics.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "PhysicsImpl.h"
#include "engine/Surfaces.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>

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
    surfaces.clear();
    terrain.reset();
    terrainBody = JPH::BodyID();
}

JPH::uint64 PhysicsWorld::Impl::surfaceId(const std::string& surface)
{
    if (surface.empty())
        return 0;
    const auto it = std::find(surfaces.begin(), surfaces.end(), surface);
    if (it != surfaces.end())
        return static_cast<JPH::uint64>(it - surfaces.begin()) + 1;
    surfaces.push_back(surface);
    return surfaces.size();
}

void PhysicsWorld::Impl::addStatic(const JPH::ShapeSettings& settings, glm::vec3 position, const std::string& name, glm::quat rotation,
                                   const std::string& surface)
{
    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError()) {
        LOG_WARN("physics", "skipping '%s': %s\n", name.c_str(), result.GetError().c_str());
        return;
    }
    JPH::BodyCreationSettings body(result.Get(), toJolt(position), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
                                   JPH::EMotionType::Static, Layers::Static);
    body.mUserData = surfaceId(surface);
    const JPH::BodyID id = system.GetBodyInterface().CreateAndAddBody(body, JPH::EActivation::DontActivate);
    if (!id.IsInvalid())
        staticBodies.push_back(id);
}

void PhysicsWorld::Impl::addStatic(const JPH::ShapeRefC& shape, glm::vec3 position, const std::string& name, glm::quat rotation,
                                   const std::string& surface)
{
    if (!shape)
        return;
    (void)name;
    JPH::BodyCreationSettings body(shape, toJolt(position), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
                                   JPH::EMotionType::Static, Layers::Static);
    body.mUserData = surfaceId(surface);
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
        std::string surface = surfaceOf(b.material.get(), b.name);
        impl_->addStatic(JPH::BoxShapeSettings(toJolt(half), convexRadius), b.position, b.name,
                         glm::quat_cast(glm::mat3(glm::scale(b.transform(), 1.0f / glm::max(b.size, glm::vec3(1e-6f))))),
                         surface.empty() ? "concrete" : surface);
    }
    // One Jolt mesh per mesh part, shared by every copy of the model (rotation and
    // scale go on the body / a scaled-shape wrapper instead of into the vertices).
    std::unordered_map<std::uint64_t, JPH::ShapeRefC> meshes;
    auto addModelParts = [&](const Model& m, const std::string& name, glm::vec3 position, glm::quat rotation, float scale,
                             const std::string& defaultSurface) {
        for (const MeshPart& part : m.parts) {
            std::string surface = surfaceOf(m.materialFor(part), part.materialName);
            if (surface.empty())
                surface = guessSurface(name);
            if (surface.empty())
                surface = defaultSurface;
            JPH::ShapeRefC& shape = meshes[part.id];
            if (!shape) {
                JPH::VertexList vertices;
                vertices.reserve(part.positions.size());
                for (const glm::vec3& p : part.positions)
                    vertices.push_back(JPH::Float3(p.x, p.y, p.z));
                JPH::IndexedTriangleList triangles;
                triangles.reserve(part.indices.size() / 3);
                for (std::size_t i = 0; i + 2 < part.indices.size(); i += 3)
                    triangles.push_back(JPH::IndexedTriangle(part.indices[i], part.indices[i + 1], part.indices[i + 2], 0));
                JPH::ShapeSettings::ShapeResult result = JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create();
                if (result.HasError()) {
                    LOG_WARN("physics", "skipping '%s': %s\n", name.c_str(), result.GetError().c_str());
                    continue;
                }
                shape = result.Get();
            }
            if (scale == 1.0f)
                impl_->addStatic(shape, position, name, rotation, surface);
            else
                impl_->addStatic(JPH::ScaledShapeSettings(shape, JPH::Vec3::sReplicate(scale)), position, name, rotation, surface);
        }
    };
    for (const Model& m : scene.models) {
        if (!m.collision)
            continue;
        const glm::mat3 rotScale(m.transform());
        const float scale = m.scale != 0.0f ? m.scale : 1.0f;
        addModelParts(m, m.name + " " + m.sourcePath.stem().string(), m.position, glm::normalize(glm::quat_cast(rotScale / scale)), scale,
                      "concrete");
    }
    // Terrain: a Jolt height field (same samples as the render mesh), plus painted
    // foliage whose type has collision (trees), sharing one mesh per type.
    if (scene.terrain && scene.terrain->valid()) {
        const Terrain& t = *scene.terrain;
        const float d = t.spacing();
        JPH::HeightFieldShapeSettings settings(t.heights.data(), JPH::Vec3(t.origin.x, 0.0f, t.origin.y), JPH::Vec3(d, 1.0f, d),
                                               static_cast<JPH::uint32>(t.resolution));
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (result.HasError())
            LOG_WARN("physics", "terrain: %s\n", result.GetError().c_str());
        else
        {
            impl_->addStatic(result.Get(), glm::vec3(0.0f), "Terrain", glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            impl_->terrain = scene.terrain;
            impl_->terrainBody = impl_->staticBodies.back();
        }
        for (const FoliageInstance& f : t.foliage) {
            if (f.type >= t.foliageTypes.size())
                continue;
            const FoliageType& type = t.foliageTypes[f.type];
            if (!type.collision || !type.model)
                continue;
            glm::quat rotation = glm::angleAxis(f.tiltX, glm::vec3(1, 0, 0)) * glm::angleAxis(f.tiltZ, glm::vec3(0, 0, 1)) *
                                 glm::angleAxis(glm::radians(f.yawDegrees), glm::vec3(0, 1, 0)); // as drawn
            addModelParts(*type.model, type.modelPath.stem().string(), f.position, glm::normalize(rotation), f.scale > 0.0f ? f.scale : 1.0f,
                          "wood");
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
    RayHit result;
    result.point = toGlm(ray.GetPointOnRay(hit.mFraction));
    result.distance = hit.mFraction * maxDistance;
    {
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterfaceNoLock(), hit.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& body = lock.GetBody();
            result.normal = toGlm(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction)));
            const JPH::uint64 id = body.GetUserData();
            if (id > 0 && id <= impl_->surfaces.size())
                result.surface = impl_->surfaces[static_cast<std::size_t>(id - 1)];
        }
    }
    if (hit.mBodyID == impl_->terrainBody && impl_->terrain) {
        // The layer painted strongest at the hit decides (grass, dirt, gravel, asphalt).
        const Terrain& t = *impl_->terrain;
        int best = -1;
        float bestWeight = 0.0f;
        for (int i = 0; i < Terrain::kLayers; ++i) {
            const float w = t.layerWeight(i, result.point.x, result.point.z);
            if (w > bestWeight && !t.layers[static_cast<std::size_t>(i)].materialPath.empty()) {
                best = i;
                bestWeight = w;
            }
        }
        if (best >= 0) {
            const TerrainLayer& layer = t.layers[static_cast<std::size_t>(best)];
            result.surface = surfaceOf(layer.material.get(), layer.materialPath.stem().string());
        }
        if (result.surface.empty())
            result.surface = "dirt";
    }
    return result;
}

} // namespace engine
