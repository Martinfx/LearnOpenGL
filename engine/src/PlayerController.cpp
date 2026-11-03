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

    if (climb(input, camera, dt)) {
        launch_ = 0.0f;
        drift_ = knock_ = glm::vec3(0.0f);
        slow_ = 0.0f;
        if (feet_.y < settings.killY)
            respawn();
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

bool PlayerController::climb(const InputState& input, const FpsCamera& camera, float dt)
{
    const bool near = ladderNear_;
    ladderNear_ = false;
    ladderCooldown_ = std::max(0.0f, ladderCooldown_ - dt);
    if (!near || ladderCooldown_ > 0.0f || freshPush_) { // a blast knocks the player off
        climbing_ = false;
        return false;
    }
    const Ladder& L = ladder_;
    const glm::vec3 l = L.local(feet_);
    const float front = -L.thickness * 0.5f; // the climbing side of the rungs (local z)

    glm::vec3 wish(0.0f);
    if (input.isDown(Action::MoveForward)) wish += camera.flatForward();
    if (input.isDown(Action::MoveBack))    wish -= camera.flatForward();
    if (input.isDown(Action::MoveRight))   wish += camera.right();
    if (input.isDown(Action::MoveLeft))    wish -= camera.right();
    if (glm::dot(wish, wish) > 0.0f)
        wish = glm::normalize(wish);
    const float into = glm::dot(wish, L.forward); // > 0 towards the ladder
    const bool grounded = body_.grounded();

    if (!climbing_) {
        const bool inFront = l.z < front + 0.05f;
        // On the floor the ladder leads up to (a roof, or a platform behind a railing up to 1.3 m high).
        const bool atTop = l.y > L.height - 1.4f && l.z > front - 0.1f;
        const bool grab = (inFront && (into > 0.5f || (!grounded && body_.velocity().y < 1.0f))) || (atTop && into < -0.5f);
        if (!grab)
            return false;
        climbing_ = true;
    }

    const bool jumpDown = input.isDown(Action::Jump);
    const bool jumpPressed = jumpDown && !jumpHeld_;
    jumpHeld_ = jumpDown;
    if (jumpPressed) { // let go: a push back off the ladder
        climbing_ = false;
        ladderCooldown_ = 0.5f;
        push(-L.forward * 3.0f + glm::vec3(0.0f, 2.0f, 0.0f));
        return false;
    }

    // Vertical: into the ladder climbs, away goes down; facing it and looking down, forward is down.
    float v = into;
    if (glm::dot(camera.flatForward(), L.forward) > 0.3f && camera.forward().y < -0.5f)
        v = -v;
    float vy = v * L.speed;
    if (input.isDown(Action::Crouch))
        vy = -L.speed * 2.2f; // slide down the rails
    // Down at the bottom: back on the floor, walking again.
    if (grounded && vy <= 0.0f && l.y < 0.3f) {
        climbing_ = false;
        return false;
    }

    glm::vec3 velocity(0.0f);
    const float hang = front - settings.radius - 0.04f; // where the capsule hangs in front of the rungs
    if (vy < 0.0f && l.z > hang + 0.15f) {
        // Getting on from the top: up over the edge or the railing, then out in front of the rungs.
        velocity.y = l.y < L.height + 0.05f ? L.speed : 0.0f;
        velocity -= L.forward * 1.8f;
    } else if (vy > 0.0f && l.y > L.height - 0.5f) {
        // Stepping off at the top: up over the edge, then forward onto the floor behind.
        if (l.z > -front + 0.05f || (grounded && l.y > L.height - 0.05f && l.z > hang + 0.1f)) {
            climbing_ = false;
            ladderCooldown_ = 0.3f;
            return false;
        }
        velocity.y = l.y < L.height + 0.05f ? L.speed : 0.0f;
        velocity += L.forward * 1.8f;
    } else {
        velocity.y = vy;
        velocity += L.forward * std::clamp((hang - l.z) * 6.0f, -1.5f, 1.5f);
        const float side = glm::dot(wish, L.right);
        if (std::abs(side) > 0.3f)
            velocity += L.right * side * 1.0f; // off the side: out of reach, the player lets go
        else
            velocity += L.right * std::clamp(-l.x * 3.0f, -0.8f, 0.8f); // keep to the middle
    }
    body_.moveFree(velocity, dt);
    feet_ = body_.feet();
    inWater_ = water_ && waterLevel_ > feet_.y;
    swimming_ = false;
    water_ = false;
    return true;
}

glm::vec3 PlayerController::interpolatedEye(float alpha) const
{
    const glm::vec3 feet = previousFeet_ + (feet_ - previousFeet_) * alpha;
    return feet + glm::vec3(0.0f, settings.eyeHeight, 0.0f);
}

} // namespace engine
