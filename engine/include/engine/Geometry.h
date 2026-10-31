#pragma once
#include <glm/vec3.hpp>
#include <optional>

namespace engine {

struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    static Aabb fromCenterSize(glm::vec3 center, glm::vec3 size);
    bool overlaps(const Aabb& o) const;
};

// Distance along the (normalized) ray to the first hit, if any.
std::optional<float> rayAabb(glm::vec3 origin, glm::vec3 dir, const Aabb& box);

} // namespace engine
