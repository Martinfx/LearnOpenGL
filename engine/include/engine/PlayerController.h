#pragma once
#include "engine/Camera.h"
#include "engine/Input.h"
#include <glm/vec3.hpp>
#include <memory>

namespace engine {

class PhysicsWorld;

struct PlayerSettings {
    float walkSpeed = 5.0f;     // m/s
    float sprintSpeed = 8.5f;
    float jumpSpeed = 5.5f;
    float gravity = 18.0f;      // m/s^2, a bit above earth for snappy jumps
    float airControl = 0.15f;   // 0 = none, 1 = full
    float radius = 0.3f;
    float height = 1.8f;
    float eyeHeight = 1.65f;
    float stepHeight = 0.4f;    // stairs up to this height are climbed automatically
    float maxSlopeDegrees = 45.0f;
    float killY = -50.0f;       // respawn when falling below this
};

// First-person character on top of Jolt's CharacterVirtual (capsule).
class PlayerController {
public:
    PlayerSettings settings;

    PlayerController();
    ~PlayerController();
    PlayerController(const PlayerController&) = delete;
    PlayerController& operator=(const PlayerController&) = delete;

    // Creates the character in the world; also the respawn point.
    void spawn(PhysicsWorld& world, glm::vec3 feet);
    void fixedUpdate(const InputState& input, const FpsCamera& camera, float dt);

    glm::vec3 feet() const { return feet_; }
    bool grounded() const { return grounded_; }
    glm::vec3 velocity() const { return velocity_; }

    // Eye position blended between the last two simulation steps.
    glm::vec3 interpolatedEye(float alpha) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    glm::vec3 spawnPoint_{0.0f};
    glm::vec3 feet_{0.0f};
    glm::vec3 previousFeet_{0.0f};
    glm::vec3 velocity_{0.0f};
    bool grounded_ = false;
    bool jumpHeld_ = false;
};

} // namespace engine
