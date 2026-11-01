#include "engine/Water.h"

#include "engine/EntityIO.h"
#include "engine/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

namespace engine {

namespace {
float number(const Entity& e, const char* key, float fallback)
{
    const auto it = e.properties.find(key);
    if (it == e.properties.end() || it->second.empty())
        return fallback;
    return std::strtof(it->second.c_str(), nullptr);
}
} // namespace

bool WaterBody::contains(glm::vec3 p) const
{
    if (p.y > level || p.y < bottom)
        return false;
    const glm::vec4 local = glm::inverse(surface) * glm::vec4(p.x, level, p.z, 1.0f);
    return std::abs(local.x) <= 0.5f && std::abs(local.z) <= 0.5f;
}

WaterBody waterBody(const Entity& e)
{
    WaterBody w;
    w.entity = &e;
    w.bottom = e.position.y;
    w.level = e.position.y + e.size.y;
    // Same yaw convention as the entity box: 0 faces +X.
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(e.position.x, w.level, e.position.z));
    m = glm::rotate(m, -glm::radians(e.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    w.surface = glm::scale(m, glm::vec3(std::max(e.size.x, 0.01f), 1.0f, std::max(e.size.z, 0.01f)));
    w.color = glm::pow(glm::max(e.color, glm::vec3(0.0f)), glm::vec3(2.2f));
    w.clarity = std::max(0.05f, number(e, "clarity", 2.0f));
    const float speed = number(e, "flow", 0.0f), yaw = glm::radians(e.yawDegrees);
    w.flow = glm::vec2(std::cos(yaw), std::sin(yaw)) * speed;
    w.waves = std::clamp(number(e, "waves", 0.3f), 0.0f, 1.0f);
    w.foam = std::clamp(number(e, "foam", 0.5f), 0.0f, 1.0f);
    w.reflection = std::clamp(number(e, "reflection", 1.0f), 0.0f, 1.0f);
    return w;
}

std::vector<WaterBody> waterBodies(const Scene& scene)
{
    std::vector<WaterBody> out;
    for (const Entity& e : scene.entities)
        if (e.type == "water" && e.alive && entityEnabled(e))
            out.push_back(waterBody(e));
    return out;
}

bool waterAt(const Scene& scene, glm::vec3 p, WaterBody* out)
{
    for (const Entity& e : scene.entities) {
        if (e.type != "water" || !e.alive || !entityEnabled(e))
            continue;
        const WaterBody w = waterBody(e);
        if (w.contains(p)) {
            if (out)
                *out = w;
            return true;
        }
    }
    return false;
}

} // namespace engine
