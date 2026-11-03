#pragma once
#include <glm/vec3.hpp>
#include <memory>

namespace engine {

class PhysicsWorld;

struct CharacterShape {
    float radius = 0.3f;
    float height = 1.8f;
    float stepHeight = 0.4f;      // stairs up to this height are climbed automatically
    float maxSlopeDegrees = 45.0f;
};

// Capsule that walks on the level with collision (Jolt CharacterVirtual).
// Used by the player and by AI-controlled entities.
class CharacterBody {
public:
    CharacterBody();
    ~CharacterBody();
    CharacterBody(CharacterBody&&) noexcept;
    CharacterBody& operator=(CharacterBody&&) noexcept;

    void create(PhysicsWorld& world, const CharacterShape& shape, glm::vec3 feet);
    bool valid() const;

    // One fixed step. desiredVelocity is horizontal (m/s); jumpSpeed > 0 jumps when grounded.
    // `impulse` is a one-off velocity change (explosions, anomalies), applied on the ground and in the air.
    void step(glm::vec3 desiredVelocity, float jumpSpeed, float gravity, float airControl, float dt,
              glm::vec3 impulse = glm::vec3(0.0f));
    // One fixed step at exactly this velocity, no gravity (ladders): collides and slides
    // along walls but neither sticks to the floor nor climbs stairs on its own.
    void moveFree(glm::vec3 velocity, float dt);
    void teleport(glm::vec3 feet);

    glm::vec3 feet() const;
    glm::vec3 velocity() const;
    bool grounded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
