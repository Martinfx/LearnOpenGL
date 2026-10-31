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

    // Outside forces (anomalies, explosions). push(): a one-off velocity change; its
    // horizontal part wears off over a second or so. drift(): extra velocity for the
    // next step only (a pull). slow(): 0..1 of the walking speed taken away next step.
    void push(glm::vec3 velocityChange);
    void drift(glm::vec3 velocity) { drift_ += velocity; }
    void slow(float amount) { slow_ = amount; }
    // Moves the player (keeps the spawn point).
    void teleport(glm::vec3 feet);

    // Eye position blended between the last two simulation steps.
    glm::vec3 interpolatedEye(float alpha) const;

private:
    CharacterBody body_;
    glm::vec3 spawnPoint_{0.0f};
    glm::vec3 feet_{0.0f};
    glm::vec3 previousFeet_{0.0f};
    bool jumpHeld_ = false;
    glm::vec3 knock_{0.0f};   // horizontal push, decays
    float launch_ = 0.0f;     // vertical push for the next step
    glm::vec3 drift_{0.0f};
    float slow_ = 0.0f;
    bool freshPush_ = false;
};

} // namespace engine
