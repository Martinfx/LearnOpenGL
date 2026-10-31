#pragma once
#include "engine/Camera.h"
#include "engine/CharacterBody.h"
#include "engine/Input.h"
#include "engine/Scene.h"
#include <glm/vec3.hpp>

namespace engine {

class PhysicsWorld;


// First-person character on top of Jolt's CharacterVirtual (capsule).
class PlayerController {
public:
    PlayerSettings settings;

    // Creates the character in the world; also the respawn point.
    void spawn(PhysicsWorld& world, glm::vec3 feet);
    void respawn();
    void fixedUpdate(const InputState& input, const FpsCamera& camera, float dt);

    glm::vec3 feet() const { return feet_; }
    glm::vec3 eye() const { return feet_ + glm::vec3(0.0f, settings.eyeHeight, 0.0f); }
    bool grounded() const { return body_.grounded(); }
    glm::vec3 velocity() const { return body_.velocity(); }

    // Eye position blended between the last two simulation steps.
    glm::vec3 interpolatedEye(float alpha) const;

private:
    CharacterBody body_;
    glm::vec3 spawnPoint_{0.0f};
    glm::vec3 feet_{0.0f};
    glm::vec3 previousFeet_{0.0f};
    bool jumpHeld_ = false;
};

} // namespace engine
