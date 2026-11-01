#pragma once
#include "engine/Scene.h"
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace engine {

struct RayHit {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
    std::string surface; // what was hit is made of (engine/Surfaces.h), empty = unknown
    int model = -1;      // dynamic model (index into Scene::models) that was hit, -1 = static or none
    int ragdoll = -1;    // ragdoll handle that was hit, -1 = none
};

// One body of a ragdoll: a capsule from the bone's joint to `end` (world space), jointed to
// its parent part with a swing-twist limit.
struct RagdollPartDesc {
    glm::mat4 bone{1.0f};       // the bone's world transform (rotation used as the body frame)
    glm::vec3 end{0.0f};        // world end of the capsule (the next bone's joint)
    int parent = -1;            // index of the parent part, -1 = root
    float radius = 0.08f;       // m
    float mass = 5.0f;          // kg
    float swingDegrees = 45.0f; // cone half angle around the bone
    float twistDegrees = 20.0f; // twist either way along the bone
    glm::vec3 velocity{0.0f};   // initial linear velocity, m/s
};

// Jolt Physics world. Jolt types stay inside Physics.cpp, so the game and
// the editor never include Jolt headers.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Rebuilds all static collision (boxes and glTF meshes) and the dynamic props
    // (models with `dynamic`) from the scene.
    void setLevel(const Scene& scene);
    // Collision shapes (each shared shape once) and bodies, plus the solver's scratch memory.
    std::size_t memoryBytes() const;
    // Recreates only the dynamic props at their models' current transforms (after loading a save).
    void resetDynamic(const Scene& scene);
    // Copies the props' simulated transforms back to their models; returns how many moved.
    int syncDynamic(Scene& scene);
    // Pushes a prop at a world point (N*s), e.g. a bullet hit (RayHit::model).
    void applyImpulse(int model, glm::vec3 point, glm::vec3 impulse);
    // Radial push on every prop within `radius` (falls off to 0 at the edge), N*s at the centre.
    void explode(glm::vec3 center, float radius, float impulse);
    int dynamicCount() const;
    // World streaming: static collision of one model added when its mesh arrives, removed when released.
    void addStaticModel(const Scene& scene, std::size_t model);
    void removeStaticModel(std::size_t model);

    // Ragdolls (dead characters): bodies collide with the level, props and other ragdolls, not
    // with their own neighbouring parts. Returns a handle (>= 0) or -1.
    int addRagdoll(const std::vector<RagdollPartDesc>& parts);
    // World transform (rotation + position, no scale) of every part's body.
    bool ragdollTransforms(int ragdoll, std::vector<glm::mat4>& out) const;
    void ragdollImpulse(int ragdoll, glm::vec3 point, glm::vec3 impulse); // to the part nearest the point
    void removeRagdoll(int ragdoll);
    int ragdollCount() const;

    // Advances dynamic bodies; call once per fixed step.
    void step(float dt);

    std::optional<RayHit> raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance) const;

    struct Impl;
    Impl& impl() { return *impl_; }

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
