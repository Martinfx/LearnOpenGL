#include "engine/Ragdoll.h"

#include "engine/Physics.h"
#include "engine/Scene.h"

#include <glm/gtc/quaternion.hpp>

namespace engine {

namespace {

glm::mat4 rigid(const glm::mat4& m) // rotation and translation of a matrix that may carry scale
{
    glm::mat4 r(1.0f);
    for (int c = 0; c < 3; ++c)
        r[c] = glm::vec4(glm::normalize(glm::vec3(m[c])), 0.0f);
    r[3] = m[3];
    return r;
}

} // namespace

bool startRagdoll(PhysicsWorld& physics, const Entity& e, Ragdoll& ragdoll, glm::vec3 velocity)
{
    ragdoll = {};
    if (!e.model || !e.model->skeleton || !e.animator.graph || e.animator.graph->ragdoll.empty())
        return false;
    const Skeleton& sk = *e.model->skeleton;
    const AnimGraph& g = *e.animator.graph;
    evaluatePose(sk, &e.animator, ragdoll.start);
    ragdoll.model = e.modelTransform();
    // Parts: listed bones found in the skeleton, in skeleton order (parents first).
    std::vector<int> partOfNode(sk.nodes.size(), -1);
    std::vector<const AnimGraph::RagdollBone*> bones;
    for (std::size_t n = 0; n < sk.nodes.size(); ++n)
        for (const AnimGraph::RagdollBone& b : g.ragdoll)
            if (sk.nodes[n].name == b.bone) {
                partOfNode[n] = static_cast<int>(ragdoll.nodes.size());
                ragdoll.nodes.push_back(static_cast<int>(n));
                bones.push_back(&b);
                break;
            }
    if (ragdoll.nodes.empty())
        return false;
    std::vector<RagdollPartDesc> parts(ragdoll.nodes.size());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const int node = ragdoll.nodes[i];
        RagdollPartDesc& p = parts[i];
        const glm::mat4 world = ragdoll.model * ragdoll.start[static_cast<std::size_t>(node)];
        p.bone = world;
        // Parent part: the nearest listed ancestor.
        for (int a = sk.nodes[static_cast<std::size_t>(node)].parent; a >= 0; a = sk.nodes[static_cast<std::size_t>(a)].parent)
            if (partOfNode[static_cast<std::size_t>(a)] >= 0) {
                p.parent = partOfNode[static_cast<std::size_t>(a)];
                break;
            }
        // End: the joints of the listed bones directly below (averaged: a pelvis or chest with
        // several limbs gets a short, central capsule), else `length` along the bone.
        const AnimGraph::RagdollBone& b = *bones[i];
        glm::vec3 end(0.0f);
        int children = 0;
        if (b.length <= 0.0f)
            for (std::size_t c = static_cast<std::size_t>(node) + 1; c < sk.nodes.size(); ++c) {
                if (partOfNode[c] < 0)
                    continue;
                int a = sk.nodes[c].parent;
                while (a >= 0 && a != node && partOfNode[static_cast<std::size_t>(a)] < 0)
                    a = sk.nodes[static_cast<std::size_t>(a)].parent;
                if (a == node) {
                    end += glm::vec3(ragdoll.model * ragdoll.start[c][3]);
                    ++children;
                }
            }
        bool found = children > 0;
        if (found)
            end /= static_cast<float>(children);
        if (!found) {
            // End bones: towards their only child (foot -> toe); with several children (a head
            // with face bones) or none, continue the parent's direction.
            glm::vec3 dir(0.0f);
            int childCount = 0;
            for (std::size_t c = 0; c < sk.nodes.size(); ++c)
                if (sk.nodes[c].parent == node) {
                    dir = glm::vec3(ragdoll.model * ragdoll.start[c][3]) - glm::vec3(world[3]);
                    ++childCount;
                }
            if (childCount != 1)
                dir = glm::vec3(0.0f);
            if (glm::dot(dir, dir) < 1e-8f && p.parent >= 0)
                dir = glm::vec3(world[3]) - glm::vec3(parts[static_cast<std::size_t>(p.parent)].bone[3]);
            if (glm::dot(dir, dir) < 1e-8f)
                dir = glm::vec3(0.0f, 1.0f, 0.0f);
            const float length = b.length > 0.0f ? b.length : b.radius * 2.0f;
            end = glm::vec3(world[3]) + glm::normalize(dir) * length;
        }
        p.end = end;
        p.radius = b.radius;
        p.mass = b.mass;
        p.swingDegrees = b.swing;
        p.twistDegrees = b.twist;
        p.velocity = velocity;
    }
    ragdoll.handle = physics.addRagdoll(parts);
    if (ragdoll.handle < 0)
        return false;
    // Body frames are the bones' rigid transforms at creation.
    for (std::size_t i = 0; i < parts.size(); ++i)
        ragdoll.bodyToBone.push_back(glm::inverse(rigid(parts[i].bone)) * parts[i].bone);
    return true;
}

bool ragdollPose(const PhysicsWorld& physics, const Ragdoll& ragdoll, const Skeleton& skeleton, std::vector<glm::mat4>& globals)
{
    std::vector<glm::mat4> bodies;
    if (ragdoll.handle < 0 || !physics.ragdollTransforms(ragdoll.handle, bodies) || bodies.size() != ragdoll.nodes.size() ||
        ragdoll.start.size() != skeleton.nodes.size())
        return false;
    const glm::mat4 toModel = glm::inverse(ragdoll.model);
    globals.resize(skeleton.nodes.size());
    std::vector<char> done(skeleton.nodes.size(), 0);
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const auto n = static_cast<std::size_t>(ragdoll.nodes[i]);
        globals[n] = toModel * bodies[i] * ragdoll.bodyToBone[i];
        done[n] = 1;
    }
    // Bones above the first part (the skeleton root) move with it; other bones keep their pose
    // relative to their parent (fingers, face, spine bones between parts).
    const auto root = static_cast<std::size_t>(ragdoll.nodes.front());
    const glm::mat4 delta = globals[root] * glm::inverse(ragdoll.start[root]);
    for (int a = skeleton.nodes[root].parent; a >= 0; a = skeleton.nodes[static_cast<std::size_t>(a)].parent) {
        globals[static_cast<std::size_t>(a)] = delta * ragdoll.start[static_cast<std::size_t>(a)];
        done[static_cast<std::size_t>(a)] = 1;
    }
    for (std::size_t n = 0; n < skeleton.nodes.size(); ++n) {
        if (done[n])
            continue;
        const int parent = skeleton.nodes[n].parent;
        globals[n] = parent >= 0 ? globals[static_cast<std::size_t>(parent)] * glm::inverse(ragdoll.start[static_cast<std::size_t>(parent)]) * ragdoll.start[n]
                                 : delta * ragdoll.start[n];
    }
    return true;
}

void stopRagdoll(PhysicsWorld& physics, Ragdoll& ragdoll)
{
    if (ragdoll.handle >= 0)
        physics.removeRagdoll(ragdoll.handle);
    ragdoll = {};
}

} // namespace engine
