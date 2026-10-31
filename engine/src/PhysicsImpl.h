#pragma once
// Private header: Jolt setup shared by Physics.cpp and PlayerController.cpp.
#include <glm/gtc/quaternion.hpp>
#include "engine/Physics.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <memory>
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
    void addStatic(const JPH::ShapeSettings& settings, glm::vec3 position, const std::string& name,
                   glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    void addStatic(const JPH::ShapeRefC& shape, glm::vec3 position, const std::string& name,
                   glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    BroadPhaseLayersImpl broadPhaseLayers;
    ObjectVsBroadPhaseFilter objectVsBroadPhase;
    ObjectPairFilter objectPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobs;
    JPH::PhysicsSystem system;
    std::vector<JPH::BodyID> staticBodies;
};

} // namespace engine
