#include "engine/PlayerController.h"
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace engine {

namespace {
constexpr float kSkin = 0.001f; // gap kept between player and walls
}

void PlayerController::spawn(glm::vec3 feet)
{
    feet_ = previousFeet_ = feet;
    velocity_ = glm::vec3(0.0f);
    grounded_ = false;
}

Aabb PlayerController::boundsAt(glm::vec3 feet) const
{
    const float w = settings.halfWidth;
    return {feet - glm::vec3(w, 0.0f, w), feet + glm::vec3(w, settings.height, w)};
}

bool PlayerController::collides(const Scene& scene, glm::vec3 feet) const
{
    const Aabb me = boundsAt(feet);
    for (const Box& b : scene.boxes)
        if (me.overlaps(b.bounds()))
            return true;
    return false;
}

// Moves along one axis and stops flush against the first box in the way.
void PlayerController::moveAxis(const Scene& scene, int axis, float amount)
{
    if (amount == 0.0f)
        return;
    glm::vec3 target = feet_;
    target[axis] += amount;
    const Aabb me = boundsAt(target);
    for (const Box& b : scene.boxes) {
        const Aabb other = b.bounds();
        if (!me.overlaps(other))
            continue;
        if (amount > 0.0f) {
            const float limit = other.min[axis] - (axis == 1 ? settings.height : settings.halfWidth) - kSkin;
            target[axis] = std::min(target[axis], limit);
        } else {
            const float limit = other.max[axis] + (axis == 1 ? 0.0f : settings.halfWidth) + kSkin;
            target[axis] = std::max(target[axis], limit);
        }
        if (axis == 1) {
            if (amount < 0.0f)
                grounded_ = true;
            velocity_.y = 0.0f;
        }
    }
    feet_ = target;
}

void PlayerController::fixedUpdate(const InputState& input, const FpsCamera& camera, const Scene& scene, float dt)
{
    previousFeet_ = feet_;

    glm::vec3 wish(0.0f);
    if (input.isDown(Action::MoveForward)) wish += camera.flatForward();
    if (input.isDown(Action::MoveBack))    wish -= camera.flatForward();
    if (input.isDown(Action::MoveRight))   wish += camera.right();
    if (input.isDown(Action::MoveLeft))    wish -= camera.right();
    if (glm::dot(wish, wish) > 0.0f)
        wish = glm::normalize(wish);

    const float speed = input.isDown(Action::Sprint) ? settings.sprintSpeed : settings.walkSpeed;
    // Full control on the ground, reduced in the air.
    const float control = grounded_ ? 1.0f : 0.15f;
    velocity_.x += (wish.x * speed - velocity_.x) * control;
    velocity_.z += (wish.z * speed - velocity_.z) * control;

    const bool jump = input.isDown(Action::Jump);
    if (jump && !jumpHeld_ && grounded_)
        velocity_.y = settings.jumpSpeed;
    jumpHeld_ = jump;

    velocity_.y -= settings.gravity * dt;

    // Horizontal moves; if blocked while grounded, try again lifted by stepHeight.
    const bool wasGrounded = grounded_;
    for (int axis : {0, 2}) {
        const float amount = velocity_[axis] * dt;
        const glm::vec3 before = feet_;
        moveAxis(scene, axis, amount);
        const bool blocked = std::abs((feet_[axis] - before[axis]) - amount) > 1e-4f;
        if (blocked && wasGrounded) {
            glm::vec3 lifted = before;
            lifted.y += settings.stepHeight;
            lifted[axis] += amount;
            if (!collides(scene, lifted)) {
                feet_ = lifted; // snapped down by the vertical move below
            }
        }
    }

    grounded_ = false;
    moveAxis(scene, 1, velocity_.y * dt);
    // Stick to the ground when walking down steps or slopes.
    if (!grounded_ && wasGrounded && velocity_.y <= 0.0f) {
        glm::vec3 probe = feet_;
        probe.y -= settings.stepHeight;
        if (collides(scene, probe)) {
            moveAxis(scene, 1, -settings.stepHeight);
        }
    }

    if (feet_.y < settings.killY)
        spawn(scene.playerStart);
}

glm::vec3 PlayerController::interpolatedEye(float alpha) const
{
    const glm::vec3 feet = previousFeet_ + (feet_ - previousFeet_) * alpha;
    return feet + glm::vec3(0.0f, settings.eyeHeight, 0.0f);
}

} // namespace engine
