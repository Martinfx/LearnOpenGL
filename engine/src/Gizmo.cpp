#include "engine/Gizmo.h"

#include <glm/geometric.hpp>
#include <cmath>

namespace engine {

namespace {

constexpr float kPi = 3.14159265358979f;

// Two unit vectors spanning the plane perpendicular to `axis`, right-handed:
// cross(u, v) == axis.
void planeBasis(glm::vec3 axis, glm::vec3& u, glm::vec3& v)
{
    const glm::vec3 helper = std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    u = glm::normalize(glm::cross(helper, axis));
    v = glm::cross(axis, u);
}

} // namespace

glm::vec3 gizmoAxis(int axis)
{
    glm::vec3 a(0.0f);
    a[axis] = 1.0f;
    return a;
}

std::vector<glm::vec3> ringPoints(glm::vec3 center, glm::vec3 axis, float radius, int segments)
{
    glm::vec3 u, v;
    planeBasis(glm::normalize(axis), u, v);
    std::vector<glm::vec3> points;
    points.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const float a = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * kPi;
        points.push_back(center + (u * std::cos(a) + v * std::sin(a)) * radius);
    }
    return points;
}

std::optional<float> pickRing(glm::vec3 origin, glm::vec3 dir, glm::vec3 center, glm::vec3 axis, float radius,
                              float tolerance)
{
    dir = glm::normalize(dir);
    // Closest approach of the ray to each short segment of the circle.
    const std::vector<glm::vec3> pts = ringPoints(center, axis, radius, 96);
    std::optional<float> best;
    float bestDistance = tolerance;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const glm::vec3 a = pts[i], b = pts[(i + 1) % pts.size()];
        // Closest points between the ray and segment ab.
        const glm::vec3 d2 = b - a, r = origin - a;
        const float aa = glm::dot(dir, dir), bb = glm::dot(dir, d2), cc = glm::dot(d2, d2);
        const float dd = glm::dot(dir, r), ee = glm::dot(d2, r);
        const float denom = aa * cc - bb * bb;
        float s = denom > 1e-9f ? (bb * ee - cc * dd) / denom : 0.0f; // along the ray
        float t = cc > 1e-9f ? (ee + bb * s) / cc : 0.0f;              // along the segment
        if (t < 0.0f || t > 1.0f) {
            t = glm::clamp(t, 0.0f, 1.0f);
            s = glm::dot(a + d2 * t - origin, dir);
        }
        if (s <= 0.0f)
            continue;
        const float distance = glm::length(origin + dir * s - (a + d2 * t));
        if (distance < bestDistance) {
            bestDistance = distance;
            best = s;
        }
    }
    return best;
}

std::optional<float> ringAngle(glm::vec3 origin, glm::vec3 dir, glm::vec3 center, glm::vec3 axis)
{
    axis = glm::normalize(axis);
    dir = glm::normalize(dir);
    const float facing = glm::dot(dir, axis);
    if (std::abs(facing) < 0.12f) // plane seen edge-on: angles would jump
        return std::nullopt;
    const float t = glm::dot(center - origin, axis) / facing;
    if (t <= 0.0f)
        return std::nullopt;
    const glm::vec3 p = origin + dir * t - center;
    glm::vec3 u, v;
    planeBasis(axis, u, v);
    return std::atan2(glm::dot(p, v), glm::dot(p, u));
}

float wrapAngle(float radians)
{
    while (radians > kPi)
        radians -= 2.0f * kPi;
    while (radians <= -kPi)
        radians += 2.0f * kPi;
    return radians;
}

} // namespace engine
