#include "engine/PlayerController.h"

#include <glm/geometric.hpp>

namespace engine {

void PlayerController::spawn(PhysicsWorld& world, glm::vec3 feet)
{
    spawnPoint_ = feet;
    feet_ = previousFeet_ = feet;
    CharacterShape shape;
    shape.radius = settings.radius;
    shape.height = settings.height;
    shape.stepHeight = settings.stepHeight;
    shape.maxSlopeDegrees = settings.maxSlopeDegrees;
    body_.create(world, shape, feet);
}

void PlayerController::respawn()
{
    body_.teleport(spawnPoint_);
    feet_ = previousFeet_ = spawnPoint_;
}

void PlayerController::fixedUpdate(const InputState& input, const FpsCamera& camera, float dt)
{
    if (!body_.valid())
        return;
    previousFeet_ = feet_;

    glm::vec3 wish(0.0f);
    if (input.isDown(Action::MoveForward)) wish += camera.flatForward();
    if (input.isDown(Action::MoveBack))    wish -= camera.flatForward();
    if (input.isDown(Action::MoveRight))   wish += camera.right();
    if (input.isDown(Action::MoveLeft))    wish -= camera.right();
    if (glm::dot(wish, wish) > 0.0f)
        wish = glm::normalize(wish);
    const float speed = input.isDown(Action::Sprint) ? settings.sprintSpeed : settings.walkSpeed;

    const bool jumpDown = input.isDown(Action::Jump);
    const bool jumpPressed = jumpDown && !jumpHeld_;
    jumpHeld_ = jumpDown;

    body_.step(wish * speed, jumpPressed ? settings.jumpSpeed : 0.0f, settings.gravity, settings.airControl, dt);
    feet_ = body_.feet();

    if (feet_.y < settings.killY)
        respawn();
}

glm::vec3 PlayerController::interpolatedEye(float alpha) const
{
    const glm::vec3 feet = previousFeet_ + (feet_ - previousFeet_) * alpha;
    return feet + glm::vec3(0.0f, settings.eyeHeight, 0.0f);
}

} // namespace engine
