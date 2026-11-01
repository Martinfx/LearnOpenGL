#include "engine/Fauna.h"
#include "engine/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>

namespace engine {

int expandFlocks(Scene& scene)
{
    std::vector<Entity> added;
    for (Entity& flock : scene.entities) {
        if (flock.type != "crow_flock")
            continue;
        auto number = [&](const char* key, double fallback) {
            const auto it = flock.properties.find(key);
            return it == flock.properties.end() ? fallback : std::strtod(it->second.c_str(), nullptr);
        };
        const int count = std::clamp(static_cast<int>(number("count", 6.0)), 0, 64);
        const float radius = std::max(0.0f, static_cast<float>(number("radius", 5.0)));
        std::mt19937 rng(static_cast<std::uint32_t>(flock.id ^ (flock.id >> 32)) + 7u);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        for (int k = 0; k < count; ++k) {
            Entity bird = flock; // model, graph, script and properties
            bird.type = "crow";
            bird.name = flock.name + " " + std::to_string(k + 1);
            bird.id = flock.id ? flock.id ^ (0x9E3779B97F4A7C15ull * static_cast<ObjectId>(k + 1)) : 0;
            bird.outputs.clear();
            bird.parent = flock.name;
            bird.properties["flock"] = flock.name;
            const float a = unit(rng) * 6.2831853f, r = radius * std::sqrt(unit(rng));
            bird.position = flock.position + glm::vec3(std::cos(a) * r, 0.0f, std::sin(a) * r);
            bird.yawDegrees = unit(rng) * 360.0f;
            added.push_back(std::move(bird));
        }
        flock.scriptPath.clear(); // the copies run the script, not the flock marker
    }
    const int n = static_cast<int>(added.size());
    for (Entity& e : added)
        scene.entities.push_back(std::move(e));
    return n;
}

} // namespace engine
