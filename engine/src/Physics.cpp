#include "engine/Log.h"
#include "engine/Physics.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include "PhysicsImpl.h"
#include "engine/Surfaces.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
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
#include <unordered_set>

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
    system.Init(/*maxBodies*/ 65536, // generated cities have tens of thousands of static pieces
                /*mutexes*/ 0, /*maxBodyPairs*/ 8192, /*maxContactConstraints*/ 4096,
                broadPhaseLayers, objectVsBroadPhase, objectPairs);
    system.SetGravity(JPH::Vec3(0.0f, -18.0f, 0.0f));
}

PhysicsWorld::Impl::~Impl()
{
    clearRagdolls();
    clearDynamic();
    clearStatic();
}

void PhysicsWorld::Impl::clearDynamic()
{
    JPH::BodyInterface& bodies = system.GetBodyInterface();
    for (const Prop& p : props) {
        bodies.RemoveBody(p.body);
        bodies.DestroyBody(p.body);
    }
    props.clear();
    propOfModel.clear();
}

namespace {
glm::quat modelRotation(const Model& m)
{
    return glm::normalize(glm::quat(glm::eulerAngleYXZ(glm::radians(m.rotation.y), glm::radians(m.rotation.x), glm::radians(m.rotation.z))));
}
} // namespace

void PhysicsWorld::Impl::addDynamic(const Scene& scene, std::size_t index)
{
    const Model& m = scene.models[index];
    const float scale = m.scale > 0.0f ? m.scale : 1.0f;
    const glm::vec3 size = glm::max((m.localBounds.max - m.localBounds.min) * scale, glm::vec3(0.02f));
    const glm::vec3 center = (m.localBounds.min + m.localBounds.max) * 0.5f * scale;
    JPH::ShapeSettings::ShapeResult shape;
    const float minHalf = std::min({size.x, size.y, size.z}) * 0.5f;
    if (m.collider == "cylinder") {
        shape = JPH::CylinderShapeSettings(size.y * 0.5f, std::max(size.x, size.z) * 0.5f,
                                           std::min(JPH::cDefaultConvexRadius, minHalf * 0.5f))
                    .Create();
    } else if (m.collider == "sphere") {
        shape = JPH::SphereShapeSettings(std::max({size.x, size.y, size.z}) * 0.5f).Create();
    } else {
        shape = JPH::BoxShapeSettings(toJolt(size * 0.5f), std::min(JPH::cDefaultConvexRadius, minHalf * 0.5f)).Create();
    }
    if (shape.HasError()) {
        LOG_WARN("physics", "prop '%s': %s", m.name.c_str(), shape.GetError().c_str());
        return;
    }
    const glm::quat rotation = modelRotation(m);
    JPH::BodyCreationSettings body(shape.Get(), toJolt(m.position + rotation * center),
                                   JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), JPH::EMotionType::Dynamic, Layers::Moving);
    body.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    body.mMassPropertiesOverride.mMass = std::max(0.05f, m.massKg);
    body.mFriction = 0.7f;
    body.mRestitution = 0.15f;
    body.mLinearDamping = 0.1f;
    body.mAngularDamping = 0.2f;
    // The world gravity is tuned for the player's jump; props fall at 1 g.
    body.mGravityFactor = 9.81f / std::max(1.0f, -system.GetGravity().GetY());
    // Activated so props placed in the air settle; resting ones go to sleep after a moment.
    std::string surface;
    for (const MeshPart& part : m.parts)
        if (surface = surfaceOf(m.materialFor(part), part.materialName); !surface.empty())
            break;
    if (surface.empty())
        surface = guessSurface(m.name + " " + m.sourcePath.stem().string());
    // User data: surface id in the low 32 bits, model index + 1 in the high ones.
    body.mUserData = surfaceId(surface.empty() ? "wood" : surface) | (static_cast<JPH::uint64>(index + 1) << 32);
    const JPH::BodyID id = system.GetBodyInterface().CreateAndAddBody(body, JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        bodyLimitWarning(m.name);
        return;
    }
    propOfModel[index] = props.size();
    props.push_back({index, id, center});
}

void PhysicsWorld::Impl::clearStatic()
{
    JPH::BodyInterface& bodies = system.GetBodyInterface();
    for (const JPH::BodyID& id : staticBodies) {
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
    }
    staticBodies.clear();
    modelBodies.clear();
    meshShapes.clear();
    surfaces.clear();
    terrain.reset();
    terrainBody = JPH::BodyID();
    bodyLimitWarned = false;
}

void PhysicsWorld::Impl::bodyLimitWarning(const std::string& name)
{
    if (bodyLimitWarned)
        return;
    bodyLimitWarned = true;
    LOG_WARN("physics", "out of bodies at '%s' (%u in use): the rest of the level has no collision\n", name.c_str(),
             system.GetNumBodies());
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
    const JPH::BodyID id = system.GetBodyInterface().CreateAndAddBody(body, JPH::EActivation::Activate);
    if (!id.IsInvalid())
        staticBodies.push_back(id);
    else
        bodyLimitWarning(name);
}

void PhysicsWorld::Impl::addStatic(const JPH::ShapeRefC& shape, glm::vec3 position, const std::string& name, glm::quat rotation,
                                   const std::string& surface)
{
    if (!shape)
        return;
    JPH::BodyCreationSettings body(shape, toJolt(position), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
                                   JPH::EMotionType::Static, Layers::Static);
    body.mUserData = surfaceId(surface);
    const JPH::BodyID id = system.GetBodyInterface().CreateAndAddBody(body, JPH::EActivation::Activate);
    if (!id.IsInvalid())
        staticBodies.push_back(id);
    else
        bodyLimitWarning(name);
}

void PhysicsWorld::Impl::addTrunk(const Model& m, glm::vec3 position, glm::quat rotation, float scale, const std::string& name)
{
    const Trunk t = trunkOf(m);
    const float half = t.height * 0.5f * scale, radius = t.radius * scale;
    const glm::vec3 center = position + rotation * (glm::vec3(t.center.x, t.height * 0.5f, t.center.y) * scale);
    addStatic(JPH::CylinderShapeSettings(half, radius, std::min(JPH::cDefaultConvexRadius, radius * 0.5f)), center, name, rotation, "wood");
}

void PhysicsWorld::Impl::addStaticModel(const Model& m, std::size_t index)
{
    removeStaticModel(index);
    const glm::mat3 rotScale(m.transform());
    const float scale = m.scale != 0.0f ? m.scale : 1.0f;
    const glm::quat rotation = glm::normalize(glm::quat_cast(rotScale / scale));
    const std::string name = m.name + " " + m.sourcePath.stem().string();
    const std::size_t first = staticBodies.size();
    if (const PlantCollider plant = plantCollider(m.sway); plant != PlantCollider::Mesh) { // trees: trunk, bushes: nothing
        if (plant == PlantCollider::Trunk && !m.parts.empty())
            addTrunk(m, m.position, rotation, scale, name);
        if (staticBodies.size() > first)
            modelBodies[index].assign(staticBodies.begin() + static_cast<std::ptrdiff_t>(first), staticBodies.end());
        return;
    }
    for (const MeshPart& part : m.parts) {
        std::string surface = surfaceOf(m.materialFor(part), part.materialName);
        if (surface.empty())
            surface = guessSurface(name);
        if (surface.empty())
            surface = "concrete";
        JPH::ShapeRefC& shape = meshShapes[part.id];
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
                meshShapes.erase(part.id);
                continue;
            }
            shape = result.Get();
        }
        if (scale == 1.0f)
            addStatic(shape, m.position, name, rotation, surface);
        else
            addStatic(JPH::ScaledShapeSettings(shape, JPH::Vec3::sReplicate(scale)), m.position, name, rotation, surface);
    }
    if (staticBodies.size() > first)
        modelBodies[index].assign(staticBodies.begin() + static_cast<std::ptrdiff_t>(first), staticBodies.end());
}

void PhysicsWorld::Impl::removeStaticModel(std::size_t index)
{
    const auto it = modelBodies.find(index);
    if (it == modelBodies.end())
        return;
    JPH::BodyInterface& bodies = system.GetBodyInterface();
    for (const JPH::BodyID& id : it->second) {
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
        staticBodies.erase(std::remove(staticBodies.begin(), staticBodies.end(), id), staticBodies.end());
    }
    modelBodies.erase(it);
}

void PhysicsWorld::addStaticModel(const Scene& scene, std::size_t model)
{
    if (model < scene.models.size() && scene.models[model].collision && !scene.models[model].dynamic)
        impl_->addStaticModel(scene.models[model], model);
}

void PhysicsWorld::removeStaticModel(std::size_t model)
{
    impl_->removeStaticModel(model);
}

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::setLevel(const Scene& scene)
{
    impl_->clearRagdolls();
    impl_->clearDynamic();
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
    for (std::size_t i = 0; i < scene.models.size(); ++i) {
        const Model& m = scene.models[i];
        if (!m.collision)
            continue;
        if (m.dynamic && !m.skeleton) {
            impl_->addDynamic(scene, i);
            continue;
        }
        impl_->addStaticModel(m, i);
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
            const std::size_t before = impl_->staticBodies.size();
            impl_->addStatic(result.Get(), glm::vec3(0.0f), "Terrain", glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            if (impl_->staticBodies.size() > before) {
                impl_->terrain = scene.terrain;
                impl_->terrainBody = impl_->staticBodies.back();
            }
        }
        for (const FoliageInstance& f : t.foliage) {
            if (f.type >= t.foliageTypes.size())
                continue;
            const FoliageType& type = t.foliageTypes[f.type];
            if (!type.collision || !type.model)
                continue;
            glm::quat rotation = glm::angleAxis(f.tiltX, glm::vec3(1, 0, 0)) * glm::angleAxis(f.tiltZ, glm::vec3(0, 0, 1)) *
                                 glm::angleAxis(glm::radians(f.yawDegrees), glm::vec3(0, 1, 0)); // as drawn
            const float scale = f.scale > 0.0f ? f.scale : 1.0f;
            // Foliage collides only when its type says so (trees): rigid types with the mesh, the rest with the trunk.
            switch (type.sway > 0.0f ? PlantCollider::Trunk : PlantCollider::Mesh) {
            case PlantCollider::Mesh:
                addModelParts(*type.model, type.modelPath.stem().string(), f.position, glm::normalize(rotation), scale, "wood");
                break;
            case PlantCollider::Trunk:
                impl_->addTrunk(*type.model, f.position, glm::normalize(rotation), scale, type.modelPath.stem().string());
                break;
            case PlantCollider::None: break;
            }
        }
    }
    impl_->system.OptimizeBroadPhase();
}

void PhysicsWorld::resetDynamic(const Scene& scene)
{
    impl_->clearDynamic();
    for (std::size_t i = 0; i < scene.models.size(); ++i)
        if (scene.models[i].dynamic && scene.models[i].collision && !scene.models[i].skeleton)
            impl_->addDynamic(scene, i);
}

int PhysicsWorld::syncDynamic(Scene& scene)
{
    int moved = 0;
    const JPH::BodyInterface& bodies = impl_->system.GetBodyInterfaceNoLock();
    for (Impl::Prop& p : impl_->props) {
        const bool active = bodies.IsActive(p.body);
        if (p.model >= scene.models.size() || (!active && !p.awake))
            continue;
        p.awake = active;
        JPH::RVec3 position;
        JPH::Quat q;
        bodies.GetPositionAndRotation(p.body, position, q);
        const glm::quat rotation(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
        Model& m = scene.models[p.model];
        m.position = toGlm(position) - rotation * p.center;
        float y = 0.0f, x = 0.0f, z = 0.0f;
        glm::extractEulerAngleYXZ(glm::mat4_cast(rotation), y, x, z);
        m.rotation = glm::degrees(glm::vec3(x, y, z));
        ++moved;
    }
    return moved;
}

void PhysicsWorld::applyImpulse(int model, glm::vec3 point, glm::vec3 impulse)
{
    const auto it = impl_->propOfModel.find(static_cast<std::size_t>(model));
    if (model < 0 || it == impl_->propOfModel.end())
        return;
    impl_->system.GetBodyInterface().AddImpulse(impl_->props[it->second].body, toJolt(impulse), toJolt(point));
}

void PhysicsWorld::explode(glm::vec3 center, float radius, float impulse)
{
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    for (const Impl::Prop& p : impl_->props) {
        const glm::vec3 at = toGlm(bodies.GetCenterOfMassPosition(p.body));
        const glm::vec3 d = at - center;
        const float dist = glm::length(d);
        if (dist >= radius)
            continue;
        const glm::vec3 dir = dist > 1e-3f ? d / dist : glm::vec3(0.0f, 1.0f, 0.0f);
        bodies.AddImpulse(p.body, toJolt((glm::normalize(dir + glm::vec3(0.0f, 0.3f, 0.0f))) * impulse * (1.0f - dist / radius)));
    }
}

int PhysicsWorld::dynamicCount() const
{
    return static_cast<int>(impl_->props.size());
}

void PhysicsWorld::Impl::clearRagdolls()
{
    JPH::BodyInterface& bodies = system.GetBodyInterface();
    for (RagdollBodies& r : ragdolls) {
        for (const auto& c : r.constraints)
            system.RemoveConstraint(c);
        for (JPH::BodyID b : r.bodies) {
            bodies.RemoveBody(b);
            bodies.DestroyBody(b);
        }
    }
    ragdolls.clear();
}

int PhysicsWorld::addRagdoll(const std::vector<RagdollPartDesc>& parts)
{
    if (parts.empty())
        return -1;
    int handle = 0;
    while (handle < static_cast<int>(impl_->ragdolls.size()) && impl_->ragdolls[static_cast<std::size_t>(handle)].used)
        ++handle;
    if (handle == static_cast<int>(impl_->ragdolls.size()))
        impl_->ragdolls.emplace_back();
    Impl::RagdollBodies& r = impl_->ragdolls[static_cast<std::size_t>(handle)];
    r = {};
    r.used = true;
    // Neighbouring parts overlap at their joints: they must not collide.
    JPH::Ref<JPH::GroupFilterTable> filter = new JPH::GroupFilterTable(static_cast<JPH::uint>(parts.size()));
    for (std::size_t i = 0; i < parts.size(); ++i)
        if (parts[i].parent >= 0)
            filter->DisableCollision(static_cast<JPH::CollisionGroup::SubGroupID>(i), static_cast<JPH::CollisionGroup::SubGroupID>(parts[i].parent));
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    const float gravityFactor = 9.81f / std::max(1.0f, -impl_->system.GetGravity().GetY());
    std::vector<glm::quat> rotations;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const RagdollPartDesc& p = parts[i];
        const glm::vec3 origin(p.bone[3]);
        const glm::quat rot = glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(p.bone[0])), glm::normalize(glm::vec3(p.bone[1])),
                                                                      glm::normalize(glm::vec3(p.bone[2])))));
        rotations.push_back(rot);
        // Capsule along the bone (Jolt capsules run along Y), in the body's frame.
        glm::vec3 axis = glm::inverse(rot) * (p.end - origin);
        float length = glm::length(axis);
        if (length < 1e-3f) {
            axis = glm::vec3(0.0f, 1.0f, 0.0f);
            length = p.radius * 2.0f;
        }
        axis /= length;
        const float radius = std::max(0.01f, p.radius);
        const float half = std::max(0.005f, length * 0.5f - radius * 0.5f);
        glm::quat align = glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), axis);
        if (glm::dot(axis, glm::vec3(0.0f, 1.0f, 0.0f)) < -0.9999f)
            align = glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
        JPH::RotatedTranslatedShapeSettings shape(toJolt(axis * (length * 0.5f)), JPH::Quat(align.x, align.y, align.z, align.w),
                                                  new JPH::CapsuleShapeSettings(half, radius));
        JPH::ShapeSettings::ShapeResult result = shape.Create();
        if (result.HasError()) {
            LOG_WARN("physics", "ragdoll part %zu: %s", i, result.GetError().c_str());
            r.bodies.push_back(JPH::BodyID());
            continue;
        }
        JPH::BodyCreationSettings body(result.Get(), toJolt(origin), JPH::Quat(rot.x, rot.y, rot.z, rot.w), JPH::EMotionType::Dynamic,
                                       Layers::Moving);
        body.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        body.mMassPropertiesOverride.mMass = std::max(0.2f, p.mass);
        body.mFriction = 0.8f;
        body.mRestitution = 0.05f;
        body.mLinearDamping = 0.15f;
        body.mAngularDamping = 0.6f;
        body.mGravityFactor = gravityFactor;
        body.mCollisionGroup = JPH::CollisionGroup(filter, static_cast<JPH::CollisionGroup::GroupID>(handle),
                                                   static_cast<JPH::CollisionGroup::SubGroupID>(i));
        body.mUserData = (static_cast<JPH::uint64>(0x80000000u | static_cast<JPH::uint32>(handle + 1)) << 32) | impl_->surfaceId("flesh");
        body.mLinearVelocity = toJolt(p.velocity);
        body.mMotionQuality = JPH::EMotionQuality::LinearCast; // thin limbs at falling speed
        r.bodies.push_back(bodies.CreateAndAddBody(body, JPH::EActivation::Activate));
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const RagdollPartDesc& p = parts[i];
        if (p.parent < 0 || r.bodies[i].IsInvalid() || r.bodies[static_cast<std::size_t>(p.parent)].IsInvalid())
            continue;
        const glm::vec3 joint(p.bone[3]);
        glm::vec3 twist = p.end - joint;
        if (glm::dot(twist, twist) < 1e-8f)
            twist = rotations[i] * glm::vec3(0.0f, 1.0f, 0.0f);
        twist = glm::normalize(twist);
        glm::vec3 plane = glm::cross(twist, std::abs(twist.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f));
        plane = glm::normalize(plane);
        JPH::SwingTwistConstraintSettings c;
        c.mSpace = JPH::EConstraintSpace::WorldSpace;
        c.mPosition1 = c.mPosition2 = toJolt(joint);
        c.mTwistAxis1 = c.mTwistAxis2 = toJolt(twist);
        c.mPlaneAxis1 = c.mPlaneAxis2 = toJolt(plane);
        c.mNormalHalfConeAngle = c.mPlaneHalfConeAngle = glm::radians(std::clamp(p.swingDegrees, 0.0f, 170.0f));
        c.mTwistMinAngle = -glm::radians(std::clamp(p.twistDegrees, 0.0f, 170.0f));
        c.mTwistMaxAngle = glm::radians(std::clamp(p.twistDegrees, 0.0f, 170.0f));
        c.mMaxFrictionTorque = 2.0f; // limbs settle instead of swinging on
        JPH::Ref<JPH::Constraint> constraint = bodies.CreateConstraint(&c, r.bodies[static_cast<std::size_t>(p.parent)], r.bodies[i]);
        if (constraint) {
            impl_->system.AddConstraint(constraint);
            r.constraints.push_back(constraint);
        }
    }
    return handle;
}

bool PhysicsWorld::ragdollTransforms(int ragdoll, std::vector<glm::mat4>& out) const
{
    if (ragdoll < 0 || ragdoll >= static_cast<int>(impl_->ragdolls.size()) || !impl_->ragdolls[static_cast<std::size_t>(ragdoll)].used)
        return false;
    const JPH::BodyInterface& bodies = impl_->system.GetBodyInterfaceNoLock();
    const auto& r = impl_->ragdolls[static_cast<std::size_t>(ragdoll)];
    out.resize(r.bodies.size());
    for (std::size_t i = 0; i < r.bodies.size(); ++i) {
        if (r.bodies[i].IsInvalid()) {
            out[i] = glm::mat4(1.0f);
            continue;
        }
        JPH::RVec3 position;
        JPH::Quat q;
        bodies.GetPositionAndRotation(r.bodies[i], position, q);
        out[i] = glm::translate(glm::mat4(1.0f), toGlm(position)) * glm::mat4_cast(glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()));
    }
    return true;
}

void PhysicsWorld::ragdollImpulse(int ragdoll, glm::vec3 point, glm::vec3 impulse)
{
    if (ragdoll < 0 || ragdoll >= static_cast<int>(impl_->ragdolls.size()) || !impl_->ragdolls[static_cast<std::size_t>(ragdoll)].used)
        return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    JPH::BodyID best;
    float bestDist = 1e9f;
    for (JPH::BodyID b : impl_->ragdolls[static_cast<std::size_t>(ragdoll)].bodies) {
        if (b.IsInvalid())
            continue;
        const float d = glm::distance(toGlm(bodies.GetCenterOfMassPosition(b)), point);
        if (d < bestDist) {
            bestDist = d;
            best = b;
        }
    }
    if (!best.IsInvalid())
        bodies.AddImpulse(best, toJolt(impulse), toJolt(point));
}

void PhysicsWorld::removeRagdoll(int ragdoll)
{
    if (ragdoll < 0 || ragdoll >= static_cast<int>(impl_->ragdolls.size()))
        return;
    auto& r = impl_->ragdolls[static_cast<std::size_t>(ragdoll)];
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    for (const auto& c : r.constraints)
        impl_->system.RemoveConstraint(c);
    for (JPH::BodyID b : r.bodies)
        if (!b.IsInvalid()) {
            bodies.RemoveBody(b);
            bodies.DestroyBody(b);
        }
    r = {};
}

int PhysicsWorld::ragdollCount() const
{
    return static_cast<int>(std::count_if(impl_->ragdolls.begin(), impl_->ragdolls.end(), [](const auto& r) { return r.used; }));
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
            const JPH::uint64 id = body.GetUserData() & 0xffffffffull;
            if (id > 0 && id <= impl_->surfaces.size())
                result.surface = impl_->surfaces[static_cast<std::size_t>(id - 1)];
            if (const JPH::uint64 high = body.GetUserData() >> 32; high & 0x80000000ull)
                result.ragdoll = static_cast<int>((high & 0x7fffffffull) - 1);
            else if (high > 0)
                result.model = static_cast<int>(high - 1);
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

std::size_t PhysicsWorld::memoryBytes() const
{
    JPH::BodyIDVector ids;
    impl_->system.GetBodies(ids);
    std::size_t bytes = 16u * 1024u * 1024u; // TempAllocatorImpl
    bytes += ids.size() * sizeof(JPH::Body);
    std::unordered_set<const JPH::Shape*> seen;
    const JPH::BodyLockInterfaceNoLock& locks = impl_->system.GetBodyLockInterfaceNoLock();
    for (const JPH::BodyID& id : ids) {
        JPH::BodyLockRead lock(locks, id);
        if (!lock.Succeeded())
            continue;
        const JPH::Shape* shape = lock.GetBody().GetShape();
        if (shape && seen.insert(shape).second)
            bytes += shape->GetStats().mSizeBytes;
    }
    return bytes;
}

} // namespace engine
