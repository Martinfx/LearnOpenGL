#include "engine/Ladder.h"

#include "engine/EntityIO.h"
#include "engine/Log.h"
#include "engine/Material.h"
#include "engine/Scene.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>

namespace engine {

namespace {
std::string text(const Entity& e, const char* key, const char* fallback)
{
    const auto it = e.properties.find(key);
    return it == e.properties.end() || it->second.empty() ? std::string(fallback) : it->second;
}

float number(const Entity& e, const char* key, float fallback)
{
    const auto it = e.properties.find(key);
    if (it == e.properties.end() || it->second.empty())
        return fallback;
    return std::strtof(it->second.c_str(), nullptr);
}

const char* defaultMaterial(const std::string& style)
{
    return style == "wood" ? "materials/pbr/rough_wood.material" : "materials/pbr/rusty_metal_02.material";
}

const char* defaultSound(const std::string& style)
{
    return style == "wood" ? "sounds/player/ladder_wood.sound" : "sounds/player/ladder_metal.sound";
}

// Materials shared by all ladders (a level has dozens on its fire escapes).
std::shared_ptr<Material> sharedMaterial(const std::filesystem::path& file)
{
    static std::mutex mutex;
    // Never destroyed: at exit the texture caches the materials point into may already be gone.
    static auto& cache = *new std::map<std::filesystem::path, std::shared_ptr<Material>>();
    const std::lock_guard lock(mutex);
    if (const auto it = cache.find(file); it != cache.end())
        return it->second;
    std::string error;
    std::shared_ptr<Material> m = loadMaterial(file, &error);
    if (!m)
        LOG_WARN("ladder", "%s", error.c_str());
    cache[file] = m;
    return m;
}

// Axis-aligned box (min..max) with its own faces; UVs in metres along the faces.
void addBox(MeshPart& part, glm::vec3 lo, glm::vec3 hi)
{
    static const glm::vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const glm::vec3& n : normals) {
        // Two axes along the face, chosen so the quad winds counter-clockwise seen from outside.
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::normalize(glm::cross(glm::vec3(0, 1, 0), n));
        const glm::vec3 v = glm::cross(n, u);
        const glm::vec3 c = (lo + hi) * 0.5f + n * (hi - lo) * 0.5f;
        const glm::vec3 half = (hi - lo) * 0.5f;
        const float hu = std::abs(glm::dot(u, half)), hv = std::abs(glm::dot(v, half));
        const auto base = static_cast<std::uint32_t>(part.positions.size());
        const glm::vec3 corners[4] = {c - u * hu - v * hv, c + u * hu - v * hv, c + u * hu + v * hv, c - u * hu + v * hv};
        for (int k = 0; k < 4; ++k) {
            part.positions.push_back(corners[k]);
            part.normals.push_back(n);
            part.texcoords.push_back({glm::dot(corners[k], u), glm::dot(corners[k], v)});
        }
        part.indices.insert(part.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}
} // namespace

glm::vec3 Ladder::local(glm::vec3 p) const
{
    const glm::vec3 d = p - base;
    return {glm::dot(d, right), d.y, glm::dot(d, forward)};
}

glm::vec3 Ladder::world(glm::vec3 l) const
{
    return base + right * l.x + glm::vec3(0.0f, l.y, 0.0f) + forward * l.z;
}

bool Ladder::reachable(glm::vec3 feet, float reach) const
{
    const glm::vec3 l = local(feet);
    // Behind the ladder only at the top: on the floor it leads up to, up to a railing's width away.
    const float behind = l.y > height - 1.4f ? 0.7f : 0.2f;
    return std::abs(l.x) <= width * 0.5f + 0.25f && l.y >= -0.3f && l.y <= height + 0.15f && l.z >= -thickness * 0.5f - reach &&
           l.z <= thickness * 0.5f + behind;
}

Ladder ladderOf(const Entity& e)
{
    Ladder l;
    l.entity = &e;
    // Same yaw convention as the entity box and water: 0 faces +X, 90 faces +Z.
    const float yaw = glm::radians(e.yawDegrees);
    l.forward = {std::cos(yaw), 0.0f, std::sin(yaw)};
    l.right = {-l.forward.z, 0.0f, l.forward.x};
    l.base = e.position;
    l.width = std::max(0.2f, e.size.x);
    l.height = std::max(0.3f, e.size.y);
    l.thickness = std::max(0.02f, e.size.z);
    l.speed = std::clamp(number(e, "speed", 2.0f), 0.2f, 10.0f);
    l.rung = std::clamp(number(e, "rung", 0.3f), 0.15f, 1.0f);
    l.style = text(e, "style", "metal");
    l.sound = text(e, "sound", defaultSound(l.style));
    return l;
}

bool ladderAt(const Scene& scene, glm::vec3 feet, float reach, Ladder* out)
{
    bool found = false;
    float best = 1e9f;
    for (const Entity& e : scene.entities) {
        if (e.type != "ladder" || !e.alive || !entityEnabled(e))
            continue;
        const Ladder l = ladderOf(e);
        if (!l.reachable(feet, reach))
            continue;
        const glm::vec3 p = l.local(feet);
        const float d = std::abs(p.z) + std::abs(p.x) * 0.5f; // the one right in front wins
        if (d < best) {
            best = d;
            found = true;
            if (out)
                *out = l;
        }
    }
    return found;
}

std::shared_ptr<Model> buildLadderModel(float width, float height, float thickness, float rung, const std::string& style)
{
    const bool wood = style == "wood";
    MeshPart part;
    part.id = newResourceId();
    part.materialName = "ladder";
    part.color = wood ? glm::vec3(0.42f, 0.32f, 0.22f) : glm::vec3(0.36f, 0.24f, 0.17f); // without the material
    part.metallic = wood ? 0.0f : 0.6f;
    part.roughness = wood ? 0.85f : 0.7f;
    const float hw = width * 0.5f, ht = thickness * 0.5f;
    // Rails: flat steel bars or square timber, the full height.
    const float rail = wood ? 0.045f : 0.035f;
    for (const float side : {-1.0f, 1.0f}) {
        const float x = side * (hw - rail * 0.5f);
        addBox(part, {x - rail * 0.5f, 0.0f, -ht}, {x + rail * 0.5f, height, ht});
    }
    // Rungs between the rails: round-ish steel bars or thinner timber, the first one a step up.
    const float r = wood ? 0.032f : 0.024f;
    const float rd = wood ? std::min(thickness, 0.045f) : r;
    for (float y = rung; y < height - 0.05f; y += rung)
        addBox(part, {-hw + rail, y - r * 0.5f, -rd * 0.5f}, {hw - rail, y + r * 0.5f, rd * 0.5f});
    auto model = std::make_shared<Model>();
    model->parts.push_back(std::move(part));
    model->localBounds = {{-hw, 0.0f, -ht}, {hw, height, ht}};
    return model;
}

bool ensureLadderModel(Entity& e, const std::filesystem::path& assetDir)
{
    if (e.type != "ladder")
        return false;
    const std::string style = text(e, "style", "metal");
    if (style == "none") {
        if (!e.model)
            return false;
        e.model.reset();
        return true;
    }
    const Ladder l = ladderOf(e);
    const std::string material = text(e, "material", defaultMaterial(style));
    char key[256];
    std::snprintf(key, sizeof key, "ladder %s %.3f %.3f %.3f %.3f ", style.c_str(), l.width, l.height, l.thickness, l.rung);
    const std::string name = key + material;
    if (e.model && e.model->name == name)
        return false;
    std::shared_ptr<Model> m = buildLadderModel(l.width, l.height, l.thickness, l.rung, style);
    m->name = name;
    std::filesystem::path file(std::u8string(material.begin(), material.end()));
    if (file.is_relative())
        file = assetDir / file;
    if (std::shared_ptr<Material> mat = sharedMaterial(file.lexically_normal())) {
        m->materialOverrides["ladder"] = mat->sourcePath;
        m->overrideMaterials["ladder"] = std::move(mat);
    }
    e.modelScale = 1.0f; // the mesh is built at the entity's size
    e.modelYaw = 0.0f;
    e.model = std::move(m);
    return true;
}

} // namespace engine
