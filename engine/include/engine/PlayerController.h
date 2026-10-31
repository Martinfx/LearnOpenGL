#pragma once
#include "engine/Camera.h"
#include "engine/Input.h"
#include "engine/Scene.h"

namespace engine {

struct PlayerSettings {
    float walkSpeed = 5.0f;     // m/s
    float sprintSpeed = 8.5f;
    float jumpSpeed = 5.5f;
    float gravity = 18.0f;      // m/s^2, a bit above earth for snappy jumps
    float halfWidth = 0.3f;
    float height = 1.8f;
    float eyeHeight = 1.65f;
    float stepHeight = 0.35f;   // ledges up to this height are climbed automatically
    float killY = -50.0f;       // respawn when falling below this
};

// Kinematic first-person character with AABB collision against the level.
// Milestone 1 placeholder; Jolt's CharacterVirtual replaces the collision code
// later while this interface stays.
class PlayerController {
public:
    PlayerSettings settings;

    void spawn(glm::vec3 feet);
    void fixedUpdate(const InputState& input, const FpsCamera& camera, const Scene& scene, float dt);

    glm::vec3 feet() const { return feet_; }
    bool grounded() const { return grounded_; }
    glm::vec3 velocity() const { return velocity_; }

    // Eye position blended between the last two simulation steps.
    glm::vec3 interpolatedEye(float alpha) const;

private:
    Aabb boundsAt(glm::vec3 feet) const;
    bool collides(const Scene& scene, glm::vec3 feet) const;
    void moveAxis(const Scene& scene, int axis, float amount);

    glm::vec3 feet_{0.0f};
    glm::vec3 previousFeet_{0.0f};
    glm::vec3 velocity_{0.0f};
    bool grounded_ = false;
    bool jumpHeld_ = false;
};

} // namespace engine
