#pragma once
#include <glm/vec3.hpp>
#include <optional>
#include <vector>

// Math of the editor's transform gizmo (kept in the engine so it can be tested
// without a window).
namespace engine {

// Unit vector of axis 0 (X), 1 (Y) or 2 (Z).
glm::vec3 gizmoAxis(int axis);

// Points on a circle of `radius` around `center`, in the plane perpendicular to `axis`.
std::vector<glm::vec3> ringPoints(glm::vec3 center, glm::vec3 axis, float radius, int segments);

// Ray against a rotation ring: hit when the ray passes within `tolerance` of the
// circle. Works at every viewing angle, including rings seen edge-on.
// Returns the distance along the ray.
std::optional<float> pickRing(glm::vec3 origin, glm::vec3 dir, glm::vec3 center, glm::vec3 axis, float radius,
                              float tolerance);

// Angle (radians, right-handed around `axis`) of the point where the ray meets the
// ring's plane; empty when the plane is seen almost edge-on.
std::optional<float> ringAngle(glm::vec3 origin, glm::vec3 dir, glm::vec3 center, glm::vec3 axis);

// Difference b - a wrapped to (-pi, pi].
float wrapAngle(float radians);

} // namespace engine
