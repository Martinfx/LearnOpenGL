#pragma once
#include "engine/Scene.h"
#include <glm/vec3.hpp>
#include <memory>
#include <optional>
#include <string>

namespace engine {

struct RayHit {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
    std::string surface; // what was hit is made of (engine/Surfaces.h), empty = unknown
};

// Jolt Physics world. Jolt types stay inside Physics.cpp, so the game and
// the editor never include Jolt headers.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Rebuilds all static collision (boxes and glTF meshes) from the scene.
    void setLevel(const Scene& scene);

    // Advances dynamic bodies; call once per fixed step.
    void step(float dt);

    std::optional<RayHit> raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance) const;

    struct Impl;
    Impl& impl() { return *impl_; }

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
