#include "engine/PlayerController.h"

#include <glm/geometric.hpp>

#include <algorithm>

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
    teleport(spawnPoint_);
}

void PlayerController::teleport(glm::vec3 feet)
{
    body_.teleport(feet);
    feet_ = previousFeet_ = feet;
    knock_ = drift_ = glm::vec3(0.0f);
    launch_ = 0.0f;
}

void PlayerController::push(glm::vec3 velocityChange)
{
    knock_ += glm::vec3(velocityChange.x, 0.0f, velocityChange.z);
    launch_ += velocityChange.y;
    freshPush_ = true;
}

void PlayerController::fixedUpdate(const InputState& input, const FpsCamera& camera, float dt)
{
    if (!body_.valid())
        return;
    previousFeet_ = feet_;

    if (noclip) {
        glm::vec3 fly(0.0f);
        if (input.isDown(Action::MoveForward)) fly += camera.forward();
        if (input.isDown(Action::MoveBack))    fly -= camera.forward();
        if (input.isDown(Action::MoveRight))   fly += camera.right();
        if (input.isDown(Action::MoveLeft))    fly -= camera.right();
        if (input.isDown(Action::Jump))        fly.y += 1.0f;
        if (input.isDown(Action::Crouch))      fly.y -= 1.0f;
        if (glm::dot(fly, fly) > 0.0f)
            fly = glm::normalize(fly);
        const float speed = (input.isDown(Action::Sprint) ? 4.0f : 2.0f) * settings.walkSpeed;
        feet_ += fly * speed * dt;
        body_.teleport(feet_);
        knock_ = drift_ = glm::vec3(0.0f); // anomalies do not push a noclipping player
        launch_ = slow_ = 0.0f;
        return;
    }

    glm::vec3 wish(0.0f);
    if (input.isDown(Action::MoveForward)) wish += camera.flatForward();
    if (input.isDown(Action::MoveBack))    wish -= camera.flatForward();
    if (input.isDown(Action::MoveRight))   wish += camera.right();
    if (input.isDown(Action::MoveLeft))    wish -= camera.right();
    if (glm::dot(wish, wish) > 0.0f)
        wish = glm::normalize(wish);
    float speed = (input.isDown(Action::Sprint) ? settings.sprintSpeed : settings.walkSpeed) * (1.0f - std::clamp(slow_, 0.0f, 1.0f));

    bool jumpDown = input.isDown(Action::Jump);
    bool jumpPressed = jumpDown && !jumpHeld_;
    jumpHeld_ = jumpDown;

    // Water: wading slows walking, chest deep and more the player swims.
    inWater_ = water_ && waterLevel_ > feet_.y;
    const float depth = inWater_ ? waterLevel_ - feet_.y : 0.0f;
    const float swimDepth = settings.eyeHeight * 0.8f;
    swimming_ = depth > swimDepth || (swimming_ && depth > swimDepth - 0.15f && !body_.grounded());
    float swimLift = 0.0f;
    glm::vec3 current(0.0f);
    if (inWater_) {
        current = glm::vec3(waterFlow_.x, 0.0f, waterFlow_.y) * std::min(1.0f, depth / swimDepth) * 0.7f;
        if (!swimming_) {
            speed *= 1.0f - 0.55f * std::clamp(depth / swimDepth, 0.0f, 1.0f);
        } else {
            speed = std::min(speed, settings.swimSpeed * (input.isDown(Action::Sprint) ? 1.4f : 1.0f));
            // Vertical: up / down keys, diving where the view points, else float with the head out.
            float targetVy = std::clamp((waterLevel_ - settings.eyeHeight * 0.88f - feet_.y) * 2.0f, -1.5f, 1.2f);
            if (jumpDown)
                targetVy = settings.swimSpeed * 0.8f;
            else if (input.isDown(Action::Crouch))
                targetVy = -settings.swimSpeed * 0.8f;
            else if (input.isDown(Action::MoveForward) && eye().y < waterLevel_ + 0.1f)
                targetVy += camera.forward().y * speed;
            const float headOut = feet_.y + settings.eyeHeight - waterLevel_;
            if (jumpPressed && headOut > -0.2f) { // climb out over a bank
                targetVy = settings.jumpSpeed * 0.9f;
            }
            swimLift = (targetVy - body_.velocity().y) * std::min(1.0f, 6.0f * dt) + settings.gravity * dt;
            jumpPressed = false;
        }
    }
    water_ = false;

    // Outside forces: the push and pull go into the wished velocity (so walking can
    // fight a pull but the push carries on in the air); the vertical part is one impulse.
    glm::vec3 desired = wish * speed + knock_ + glm::vec3(drift_.x, 0.0f, drift_.z) + current;
    glm::vec3 impulse(0.0f, launch_ + swimLift, 0.0f);
    if (drift_.y > 0.0f) // lift: hold against gravity
        impulse.y += (drift_.y - std::max(0.0f, body_.velocity().y)) * std::min(1.0f, 8.0f * dt) + settings.gravity * dt;
    const bool pushed = freshPush_; // the push step takes the full velocity, later steps keep it as momentum
    freshPush_ = false;
    const float control = swimming_ ? std::min(1.0f, 4.0f * dt + 0.25f) : settings.airControl; // water: slow but full control
    body_.step(desired, jumpPressed ? settings.jumpSpeed : 0.0f, settings.gravity, pushed ? 1.0f : control, dt, impulse);
    feet_ = body_.feet();
    launch_ = 0.0f;
    drift_ = glm::vec3(0.0f);
    slow_ = 0.0f;
    knock_ *= std::max(0.0f, 1.0f - 2.5f * dt);

    if (feet_.y < settings.killY)
        respawn();
}

glm::vec3 PlayerController::interpolatedEye(float alpha) const
{
    const glm::vec3 feet = previousFeet_ + (feet_ - previousFeet_) * alpha;
    return feet + glm::vec3(0.0f, settings.eyeHeight, 0.0f);
}

} // namespace engine
