#include "engine/PlayerController.h"
#include "PhysicsImpl.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <algorithm>

namespace engine {

struct PlayerController::Impl {
    PhysicsWorld::Impl* world = nullptr;
    JPH::Ref<JPH::CharacterVirtual> character;
};

PlayerController::PlayerController() : impl_(std::make_unique<Impl>()) {}
PlayerController::~PlayerController() = default;

void PlayerController::spawn(PhysicsWorld& world, glm::vec3 feet)
{
    spawnPoint_ = feet;
    feet_ = previousFeet_ = feet;
    velocity_ = glm::vec3(0.0f);
    grounded_ = false;
    impl_->world = &world.impl();

    // Capsule standing on the origin, so the character position is the feet.
    const float halfCylinder = std::max(0.01f, (settings.height - 2.0f * settings.radius) * 0.5f);
    JPH::RefConst<JPH::Shape> capsule =
        JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, halfCylinder + settings.radius, 0.0f), JPH::Quat::sIdentity(),
                                            new JPH::CapsuleShape(halfCylinder, settings.radius))
            .Create()
            .Get();

    JPH::Ref<JPH::CharacterVirtualSettings> cs = new JPH::CharacterVirtualSettings();
    cs->mShape = capsule;
    cs->mMaxSlopeAngle = JPH::DegreesToRadians(settings.maxSlopeDegrees);
    // Only contacts below the lower hemisphere's center count as ground.
    cs->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -settings.radius);
    impl_->character = new JPH::CharacterVirtual(cs, toJolt(feet), JPH::Quat::sIdentity(), 0, &impl_->world->system);
}

void PlayerController::fixedUpdate(const InputState& input, const FpsCamera& camera, float dt)
{
    if (!impl_->character)
        return;
    JPH::CharacterVirtual& ch = *impl_->character;
    previousFeet_ = feet_;

    glm::vec3 wish(0.0f);
    if (input.isDown(Action::MoveForward)) wish += camera.flatForward();
    if (input.isDown(Action::MoveBack))    wish -= camera.flatForward();
    if (input.isDown(Action::MoveRight))   wish += camera.right();
    if (input.isDown(Action::MoveLeft))    wish -= camera.right();
    if (glm::dot(wish, wish) > 0.0f)
        wish = glm::normalize(wish);
    const float speed = input.isDown(Action::Sprint) ? settings.sprintSpeed : settings.walkSpeed;
    const glm::vec3 desired = wish * speed;

    const bool jumpDown = input.isDown(Action::Jump);
    const bool jumpPressed = jumpDown && !jumpHeld_;
    jumpHeld_ = jumpDown;

    // Velocity handling follows Jolt's CharacterVirtual sample: inherit the
    // ground velocity while standing, keep vertical momentum in the air.
    ch.UpdateGroundVelocity();
    const JPH::Vec3 gravity(0.0f, -settings.gravity, 0.0f);
    const JPH::Vec3 current = ch.GetLinearVelocity();
    const JPH::Vec3 groundVelocity = ch.GetGroundVelocity();
    const bool onGround = ch.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;

    JPH::Vec3 velocity;
    glm::vec3 horizontal;
    if (onGround && current.GetY() - groundVelocity.GetY() < 0.1f) {
        velocity = groundVelocity;
        if (jumpPressed)
            velocity += JPH::Vec3(0.0f, settings.jumpSpeed, 0.0f);
        horizontal = desired;
    } else {
        velocity = JPH::Vec3(0.0f, current.GetY(), 0.0f);
        const glm::vec3 currentHorizontal(current.GetX(), 0.0f, current.GetZ());
        horizontal = currentHorizontal + (desired - currentHorizontal) * settings.airControl;
    }
    velocity += gravity * dt;
    velocity += toJolt(horizontal);
    ch.SetLinearVelocity(velocity);

    JPH::CharacterVirtual::ExtendedUpdateSettings update;
    update.mStickToFloorStepDown = JPH::Vec3(0.0f, -settings.stepHeight, 0.0f);
    update.mWalkStairsStepUp = JPH::Vec3(0.0f, settings.stepHeight, 0.0f);
    JPH::PhysicsSystem& system = impl_->world->system;
    ch.ExtendedUpdate(dt, gravity, update,
                      system.GetDefaultBroadPhaseLayerFilter(Layers::Moving),
                      system.GetDefaultLayerFilter(Layers::Moving),
                      JPH::BodyFilter(), JPH::ShapeFilter(), *impl_->world->tempAllocator);

    feet_ = toGlm(ch.GetPosition());
    velocity_ = toGlm(ch.GetLinearVelocity());
    grounded_ = ch.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;

    if (feet_.y < settings.killY) {
        ch.SetPosition(toJolt(spawnPoint_));
        ch.SetLinearVelocity(JPH::Vec3::sZero());
        feet_ = previousFeet_ = spawnPoint_;
    }
}

glm::vec3 PlayerController::interpolatedEye(float alpha) const
{
    const glm::vec3 feet = previousFeet_ + (feet_ - previousFeet_) * alpha;
    return feet + glm::vec3(0.0f, settings.eyeHeight, 0.0f);
}

} // namespace engine
