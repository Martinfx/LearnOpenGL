#pragma once
// Private header: Jolt setup shared by Physics.cpp and PlayerController.cpp.
#include <glm/gtc/quaternion.hpp>
#include "engine/Physics.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Constraints/Constraint.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace engine {

namespace Layers {
constexpr JPH::ObjectLayer Static = 0;
constexpr JPH::ObjectLayer Moving = 1;
} // namespace Layers

namespace BroadPhaseLayers {
inline constexpr JPH::BroadPhaseLayer Static{0};
inline constexpr JPH::BroadPhaseLayer Moving{1};
} // namespace BroadPhaseLayers

class BroadPhaseLayersImpl final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override;
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override;
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override;
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override;
};

inline JPH::Vec3 toJolt(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
inline glm::vec3 toGlm(JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }

struct PhysicsWorld::Impl {
    Impl();
    ~Impl();
    void clearStatic();
    void clearDynamic();
    void addDynamic(const Scene& scene, std::size_t model);
    void addStatic(const JPH::ShapeSettings& settings, glm::vec3 position, const std::string& name,
                   glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), const std::string& surface = {});
    void addStatic(const JPH::ShapeRefC& shape, glm::vec3 position, const std::string& name,
                   glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), const std::string& surface = {});
    void bodyLimitWarning(const std::string& name); // once per level: Jolt is out of bodies
    bool bodyLimitWarned = false;
    JPH::uint64 surfaceId(const std::string& surface); // body user data: index + 1 into `surfaces`, 0 = none
    // Static collision of one level model (a Jolt mesh per part, shared by copies); bodies kept per model
    // so world streaming can remove them again.
    void addStaticModel(const Model& m, std::size_t index);
    // A tree's trunk (engine::trunkOf) as an upright cylinder, placed like the model.
    void addTrunk(const Model& m, glm::vec3 position, glm::quat rotation, float scale, const std::string& name);
    void removeStaticModel(std::size_t index);
    std::unordered_map<std::size_t, std::vector<JPH::BodyID>> modelBodies;
    std::unordered_map<std::uint64_t, JPH::ShapeRefC> meshShapes; // MeshPart::id -> shape

    BroadPhaseLayersImpl broadPhaseLayers;
    ObjectVsBroadPhaseFilter objectVsBroadPhase;
    ObjectPairFilter objectPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobs;
    JPH::PhysicsSystem system;
    std::vector<JPH::BodyID> staticBodies;
    std::vector<std::string> surfaces;
    std::shared_ptr<const Terrain> terrain; // surface of the ground under a hit
    JPH::BodyID terrainBody;
    struct Prop {
        std::size_t model = 0;
        JPH::BodyID body;
        glm::vec3 center{0.0f}; // collider centre in model space (scaled)
        bool awake = true;      // synced once more after the body falls asleep (its final pose)
    };
    std::vector<Prop> props;
    struct RagdollBodies {
        std::vector<JPH::BodyID> bodies;
        std::vector<JPH::Ref<JPH::Constraint>> constraints;
        bool used = false;
    };
    std::vector<RagdollBodies> ragdolls; // by handle; freed slots are reused
    void clearRagdolls();
    std::unordered_map<std::size_t, std::size_t> propOfModel; // model index -> props index
};

} // namespace engine
