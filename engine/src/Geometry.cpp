#include "engine/Geometry.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace engine {

Aabb Aabb::fromCenterSize(glm::vec3 center, glm::vec3 size)
{
    const glm::vec3 half = size * 0.5f;
    return {center - half, center + half};
}

bool Aabb::overlaps(const Aabb& o) const
{
    return min.x < o.max.x && max.x > o.min.x
        && min.y < o.max.y && max.y > o.min.y
        && min.z < o.max.z && max.z > o.min.z;
}

std::optional<float> rayAabb(glm::vec3 origin, glm::vec3 dir, const Aabb& box)
{
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        if (dir[axis] == 0.0f) {
            if (origin[axis] < box.min[axis] || origin[axis] > box.max[axis])
                return std::nullopt;
            continue;
        }
        const float inv = 1.0f / dir[axis];
        float t0 = (box.min[axis] - origin[axis]) * inv;
        float t1 = (box.max[axis] - origin[axis]) * inv;
        if (t0 > t1)
            std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMin > tMax)
            return std::nullopt;
    }
    return tMin;
}

} // namespace engine
