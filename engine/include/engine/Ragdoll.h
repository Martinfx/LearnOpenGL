#pragma once
#include "engine/Animation.h"
#include <glm/mat4x4.hpp>
#include <vector>

namespace engine {

class PhysicsWorld;
struct Entity;

// A character's ragdoll: physics bodies built from its animation graph's `ragdoll` bones at the
// pose it has now, and the pose read back from them every frame.
struct Ragdoll {
    int handle = -1;                  // PhysicsWorld ragdoll, -1 = none
    std::vector<int> nodes;           // skeleton node per part
    std::vector<glm::mat4> bodyToBone; // per part: bone world transform relative to its body
    std::vector<glm::mat4> start;     // model-space pose when the ragdoll started (bones without a body follow their parent)
    glm::mat4 model{1.0f};            // the character's model transform then (world from model space)
};

// Creates the ragdoll from the entity's current pose (its graph must list ragdoll bones).
// `velocity` is given to every body (the character's motion when it died).
bool startRagdoll(PhysicsWorld& physics, const Entity& entity, Ragdoll& ragdoll, glm::vec3 velocity = glm::vec3(0.0f));
// Model-space pose of every skeleton node from the bodies (for Animator::poseOverride).
bool ragdollPose(const PhysicsWorld& physics, const Ragdoll& ragdoll, const Skeleton& skeleton, std::vector<glm::mat4>& globals);
void stopRagdoll(PhysicsWorld& physics, Ragdoll& ragdoll);

} // namespace engine
