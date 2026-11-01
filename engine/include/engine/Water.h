#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <vector>

namespace engine {

struct Entity;
struct Scene;

// A body of water: an entity of type "water". Its box is the water (position = bottom
// centre, size = width x depth x length, yaw turns it), the top face is the surface.
// Properties: clarity (m), flow (m/s along the entity's facing: rivers), waves (0..1),
// foam (0..1), reflection (0..1); the entity colour is the colour of deep water.
struct WaterBody {
    const Entity* entity = nullptr;
    glm::mat4 surface{1.0f}; // unit quad (-0.5..0.5 on X/Z) -> the surface
    float level = 0.0f;      // surface height
    float bottom = 0.0f;
    glm::vec3 color{0.05f, 0.09f, 0.08f}; // linear
    float clarity = 2.0f;
    glm::vec2 flow{0.0f};    // m/s on X/Z
    float waves = 0.3f;
    float foam = 0.5f;
    float reflection = 1.0f;
    bool contains(glm::vec3 p) const; // inside the box (under the surface)
    float depthAt(glm::vec3 p) const { return level - p.y; }
};

WaterBody waterBody(const Entity& e);
std::vector<WaterBody> waterBodies(const Scene& scene); // live, enabled ones
// The body the point is in (under its surface), or none.
bool waterAt(const Scene& scene, glm::vec3 p, WaterBody* out = nullptr);

} // namespace engine
