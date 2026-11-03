#include "engine/CharacterBody.h"
#include "PhysicsImpl.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <algorithm>

namespace engine {

struct CharacterBody::Impl {
    PhysicsWorld::Impl* world = nullptr;
    CharacterShape shape;
    JPH::Ref<JPH::CharacterVirtual> character;
};

CharacterBody::CharacterBody() : impl_(std::make_unique<Impl>()) {}
CharacterBody::~CharacterBody() = default;
CharacterBody::CharacterBody(CharacterBody&&) noexcept = default;
CharacterBody& CharacterBody::operator=(CharacterBody&&) noexcept = default;

void CharacterBody::create(PhysicsWorld& world, const CharacterShape& shape, glm::vec3 feet)
{
    impl_->world = &world.impl();
    impl_->shape = shape;

    // Capsule standing on the origin, so the character position is the feet.
    const float halfCylinder = std::max(0.01f, (shape.height - 2.0f * shape.radius) * 0.5f);
    JPH::RefConst<JPH::Shape> capsule =
        JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, halfCylinder + shape.radius, 0.0f), JPH::Quat::sIdentity(),
                                            new JPH::CapsuleShape(halfCylinder, shape.radius))
            .Create()
            .Get();

    JPH::Ref<JPH::CharacterVirtualSettings> cs = new JPH::CharacterVirtualSettings();
    cs->mShape = capsule;
    cs->mMaxSlopeAngle = JPH::DegreesToRadians(shape.maxSlopeDegrees);
    // Only contacts below the lower hemisphere's center count as ground.
    cs->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -shape.radius);
    impl_->character = new JPH::CharacterVirtual(cs, toJolt(feet), JPH::Quat::sIdentity(), 0, &impl_->world->system);
}

bool CharacterBody::valid() const
{
    return impl_ && impl_->character != nullptr;
}

void CharacterBody::step(glm::vec3 desiredVelocity, float jumpSpeed, float gravity, float airControl, float dt, glm::vec3 impulse)
{
    if (!valid())
        return;
    JPH::CharacterVirtual& ch = *impl_->character;

    // Velocity handling follows Jolt's CharacterVirtual sample: inherit the
    // ground velocity while standing, keep vertical momentum in the air.
    ch.UpdateGroundVelocity();
    const JPH::Vec3 g(0.0f, -gravity, 0.0f);
    const JPH::Vec3 current = ch.GetLinearVelocity();
    const JPH::Vec3 groundVelocity = ch.GetGroundVelocity();
    const bool onGround = ch.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;

    JPH::Vec3 velocity;
    glm::vec3 horizontal(desiredVelocity.x, 0.0f, desiredVelocity.z);
    if (onGround && current.GetY() - groundVelocity.GetY() < 0.1f) {
        velocity = groundVelocity;
        if (jumpSpeed > 0.0f)
            velocity += JPH::Vec3(0.0f, jumpSpeed, 0.0f);
    } else {
        velocity = JPH::Vec3(0.0f, current.GetY(), 0.0f);
        const glm::vec3 currentHorizontal(current.GetX(), 0.0f, current.GetZ());
        horizontal = currentHorizontal + (horizontal - currentHorizontal) * airControl;
    }
    velocity += g * dt;
    velocity += toJolt(horizontal);
    velocity += toJolt(impulse);
    ch.SetLinearVelocity(velocity);

    JPH::CharacterVirtual::ExtendedUpdateSettings update;
    update.mStickToFloorStepDown = JPH::Vec3(0.0f, -impl_->shape.stepHeight, 0.0f);
    update.mWalkStairsStepUp = JPH::Vec3(0.0f, impl_->shape.stepHeight, 0.0f);
    JPH::PhysicsSystem& system = impl_->world->system;
    ch.ExtendedUpdate(dt, g, update,
                      system.GetDefaultBroadPhaseLayerFilter(Layers::Moving),
                      system.GetDefaultLayerFilter(Layers::Moving),
                      JPH::BodyFilter(), JPH::ShapeFilter(), *impl_->world->tempAllocator);
}

void CharacterBody::moveFree(glm::vec3 velocity, float dt)
{
    if (!valid())
        return;
    JPH::CharacterVirtual& ch = *impl_->character;
    ch.SetLinearVelocity(toJolt(velocity));
    JPH::CharacterVirtual::ExtendedUpdateSettings update;
    update.mStickToFloorStepDown = JPH::Vec3::sZero();
    update.mWalkStairsStepUp = JPH::Vec3::sZero();
    JPH::PhysicsSystem& system = impl_->world->system;
    ch.ExtendedUpdate(dt, JPH::Vec3::sZero(), update,
                      system.GetDefaultBroadPhaseLayerFilter(Layers::Moving),
                      system.GetDefaultLayerFilter(Layers::Moving),
                      JPH::BodyFilter(), JPH::ShapeFilter(), *impl_->world->tempAllocator);
}

void CharacterBody::teleport(glm::vec3 feet)
{
    if (!valid())
        return;
    impl_->character->SetPosition(toJolt(feet));
    impl_->character->SetLinearVelocity(JPH::Vec3::sZero());
}

glm::vec3 CharacterBody::feet() const
{
    return valid() ? toGlm(impl_->character->GetPosition()) : glm::vec3(0.0f);
}

glm::vec3 CharacterBody::velocity() const
{
    return valid() ? toGlm(impl_->character->GetLinearVelocity()) : glm::vec3(0.0f);
}

bool CharacterBody::grounded() const
{
    return valid() && impl_->character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

} // namespace engine
