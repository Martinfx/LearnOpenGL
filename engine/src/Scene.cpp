#include "engine/Scene.h"
#include <cmath>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <iomanip>
#include <sstream>

namespace engine {

namespace {
std::filesystem::path pathFromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string pathToUtf8(const std::filesystem::path& p)
{
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

void setError(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
}
} // namespace

Scene makeDefaultScene()
{
    Scene s;
    s.playerStart = {0.0f, 0.0f, 8.0f};
    const glm::vec3 stone{0.62f, 0.60f, 0.56f};
    const glm::vec3 wall{0.48f, 0.52f, 0.58f};
    const glm::vec3 accent{0.80f, 0.45f, 0.20f};

    s.boxes.push_back({"Floor", {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f}, stone});
    s.boxes.push_back({"Wall North", {0.0f, 2.0f, -20.0f}, {40.0f, 4.0f, 1.0f}, wall});
    s.boxes.push_back({"Wall South", {0.0f, 2.0f, 20.0f}, {40.0f, 4.0f, 1.0f}, wall});
    s.boxes.push_back({"Wall West", {-20.0f, 2.0f, 0.0f}, {1.0f, 4.0f, 40.0f}, wall});
    s.boxes.push_back({"Wall East", {20.0f, 2.0f, 0.0f}, {1.0f, 4.0f, 40.0f}, wall});
    s.boxes.push_back({"Crate A", {-4.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, accent});
    s.boxes.push_back({"Crate B", {-4.0f, 1.5f, -1.2f}, {1.0f, 1.0f, 1.0f}, accent});
    s.boxes.push_back({"Pillar", {5.0f, 2.0f, -5.0f}, {1.5f, 4.0f, 1.5f}, wall});
    // A staircase of 0.3 m steps leading onto a platform.
    for (int i = 0; i < 6; ++i) {
        const float h = 0.3f * static_cast<float>(i + 1);
        s.boxes.push_back({"Step " + std::to_string(i + 1),
                           {8.0f, h * 0.5f, 6.0f - static_cast<float>(i) * 0.6f},
                           {3.0f, h, 0.6f}, stone});
    }
    s.boxes.push_back({"Platform", {8.0f, 0.9f, -1.8f}, {6.0f, 1.8f, 4.0f}, stone});
    return s;
}

bool saveScene(const Scene& scene, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        setError(error, "Cannot write " + file.string());
        return false;
    }
    out << std::setprecision(6);
    out << "# FPS engine level\nversion 5\n";
    out << "player_start " << scene.playerStart.x << ' ' << scene.playerStart.y << ' ' << scene.playerStart.z << '\n';
    out << "sun " << scene.sunDirection.x << ' ' << scene.sunDirection.y << ' ' << scene.sunDirection.z << '\n';
    const std::filesystem::path levelDir = std::filesystem::absolute(file).parent_path();
    auto relative = [&](const std::filesystem::path& p) {
        const std::filesystem::path rel = std::filesystem::absolute(p).lexically_relative(levelDir);
        return pathToUtf8(rel.empty() ? std::filesystem::absolute(p) : rel);
    };
    for (const Group& g : scene.groups)
        out << "group " << std::quoted(g.name) << ' ' << std::quoted(g.parent) << '\n';
    auto writeParent = [&out](const std::string& parent) {
        if (!parent.empty())
            out << "  parent " << std::quoted(parent) << '\n';
    };
    for (const Box& b : scene.boxes) {
        out << "box " << std::quoted(b.name) << ' '
            << b.position.x << ' ' << b.position.y << ' ' << b.position.z << ' '
            << b.size.x << ' ' << b.size.y << ' ' << b.size.z << ' '
            << b.color.r << ' ' << b.color.g << ' ' << b.color.b << '\n';
        writeParent(b.parent);
        if (!b.materialPath.empty())
            out << "  material " << std::quoted(relative(b.materialPath)) << '\n';
    }
    for (const Model& m : scene.models) {
        std::filesystem::path rel = std::filesystem::absolute(m.sourcePath).lexically_relative(levelDir);
        if (rel.empty())
            rel = std::filesystem::absolute(m.sourcePath); // e.g. another drive on Windows
        out << "model " << std::quoted(m.name) << ' ' << std::quoted(pathToUtf8(rel)) << ' '
            << m.position.x << ' ' << m.position.y << ' ' << m.position.z << '\n';
        writeParent(m.parent);
        for (const auto& [slot, path] : m.materialOverrides)
            out << "  material_slot " << std::quoted(slot) << ' ' << std::quoted(relative(path)) << '\n';
    }
    const PlayerSettings& pl = scene.player;
    out << "player " << pl.walkSpeed << ' ' << pl.sprintSpeed << ' ' << pl.jumpSpeed << ' ' << pl.gravity << ' '
        << pl.airControl << ' ' << pl.height << ' ' << pl.eyeHeight << ' ' << pl.stepHeight << ' '
        << pl.maxSlopeDegrees << ' ' << pl.maxHealth << '\n';
    for (const std::filesystem::path& w : scene.loadout) {
        const std::filesystem::path rel = std::filesystem::absolute(w).lexically_relative(levelDir);
        out << "weapon " << std::quoted(pathToUtf8(rel.empty() ? std::filesystem::absolute(w) : rel)) << '\n';
    }
    for (const Entity& e : scene.entities) {
        std::string script;
        if (!e.scriptPath.empty()) {
            std::filesystem::path rel = std::filesystem::absolute(e.scriptPath).lexically_relative(levelDir);
            script = pathToUtf8(rel.empty() ? std::filesystem::absolute(e.scriptPath) : rel);
        }
        out << "entity " << std::quoted(e.name) << ' ' << std::quoted(e.type) << ' '
            << e.position.x << ' ' << e.position.y << ' ' << e.position.z << ' ' << e.yawDegrees << ' '
            << e.size.x << ' ' << e.size.y << ' ' << e.size.z << ' '
            << e.color.r << ' ' << e.color.g << ' ' << e.color.b << ' ' << std::quoted(script) << '\n';
        writeParent(e.parent);
        if (!e.modelPath.empty()) {
            const std::filesystem::path rel = std::filesystem::absolute(e.modelPath).lexically_relative(levelDir);
            out << "  character " << std::quoted(pathToUtf8(rel.empty() ? std::filesystem::absolute(e.modelPath) : rel))
                << ' ' << e.modelScale << ' ' << e.modelYaw << ' ' << std::quoted(e.animation) << '\n';
        }
        for (const auto& [key, value] : e.properties)
            out << "  prop " << std::quoted(key) << ' ' << std::quoted(value) << '\n';
    }
    if (!out) {
        setError(error, "Write failed: " + file.string());
        return false;
    }
    return true;
}

bool loadScene(Scene& scene, const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        setError(error, "Cannot open " + file.string());
        return false;
    }
    Scene result;
    std::string line;
    int lineNo = 0;
    enum class LastObject { None, Box, Model, Entity } lastObject = LastObject::None;
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#')
            continue;
        bool ok = true;
        if (key == "version") {
            int v = 0;
            ok = static_cast<bool>(ls >> v) && v >= 1 && v <= 5;
        } else if (key == "group") {
            Group g;
            ok = static_cast<bool>(ls >> std::quoted(g.name) >> std::quoted(g.parent));
            if (ok)
                result.groups.push_back(std::move(g));
        } else if (key == "parent") {
            // Hierarchy parent of the object on the line above.
            std::string parent;
            ok = static_cast<bool>(ls >> std::quoted(parent)) && lastObject != LastObject::None;
            if (ok) {
                if (lastObject == LastObject::Box)
                    result.boxes.back().parent = parent;
                else if (lastObject == LastObject::Model)
                    result.models.back().parent = parent;
                else
                    result.entities.back().parent = parent;
            }
        } else if (key == "player_start") {
            ok = static_cast<bool>(ls >> result.playerStart.x >> result.playerStart.y >> result.playerStart.z);
        } else if (key == "sun") {
            ok = static_cast<bool>(ls >> result.sunDirection.x >> result.sunDirection.y >> result.sunDirection.z);
        } else if (key == "box") {
            Box b;
            ok = static_cast<bool>(ls >> std::quoted(b.name)
                                      >> b.position.x >> b.position.y >> b.position.z
                                      >> b.size.x >> b.size.y >> b.size.z
                                      >> b.color.r >> b.color.g >> b.color.b);
            if (ok) {
                result.boxes.push_back(std::move(b));
                lastObject = LastObject::Box;
            }
        } else if (key == "model") {
            Model m;
            std::string path;
            ok = static_cast<bool>(ls >> std::quoted(m.name) >> std::quoted(path)
                                      >> m.position.x >> m.position.y >> m.position.z);
            if (ok) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                std::string modelError;
                if (!loadModelFile(m, p.lexically_normal(), &modelError)) {
                    setError(error, file.string() + ":" + std::to_string(lineNo) + ": " + modelError);
                    return false;
                }
                result.models.push_back(std::move(m));
                lastObject = LastObject::Model;
            }
        } else if (key == "player") {
            PlayerSettings& pl = result.player;
            ok = static_cast<bool>(ls >> pl.walkSpeed >> pl.sprintSpeed >> pl.jumpSpeed >> pl.gravity >> pl.airControl
                                      >> pl.height >> pl.eyeHeight >> pl.stepHeight >> pl.maxSlopeDegrees >> pl.maxHealth);
        } else if (key == "prop") {
            std::string k, v;
            ok = static_cast<bool>(ls >> std::quoted(k) >> std::quoted(v)) && !result.entities.empty();
            if (ok)
                result.entities.back().properties[k] = v;
        } else if (key == "material" || key == "material_slot") {
            // Box material, or glTF material override of the last model.
            std::string slot, path;
            if (key == "material_slot")
                ok = static_cast<bool>(ls >> std::quoted(slot)) && !result.models.empty();
            else
                ok = !result.boxes.empty();
            ok = ok && static_cast<bool>(ls >> std::quoted(path));
            if (ok) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                p = p.lexically_normal();
                std::string materialError;
                auto material = loadMaterial(p, &materialError);
                if (!material) {
                    setError(error, file.string() + ":" + std::to_string(lineNo) + ": " + materialError);
                    return false;
                }
                if (key == "material") {
                    result.boxes.back().materialPath = p;
                    result.boxes.back().material = std::move(material);
                } else {
                    result.models.back().materialOverrides[slot] = p;
                    result.models.back().overrideMaterials[slot] = std::move(material);
                }
            }
        } else if (key == "character") {
            std::string path;
            ok = !result.entities.empty();
            Entity* e = ok ? &result.entities.back() : nullptr;
            ok = ok && static_cast<bool>(ls >> std::quoted(path) >> e->modelScale >> e->modelYaw >> std::quoted(e->animation));
            if (ok) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                e->modelPath = p.lexically_normal();
                std::string modelError;
                e->model = loadCharacterModel(e->modelPath, &modelError);
                if (!e->model) {
                    setError(error, file.string() + ":" + std::to_string(lineNo) + ": " + modelError);
                    return false;
                }
            }
        } else if (key == "weapon") {
            std::string path;
            ok = static_cast<bool>(ls >> std::quoted(path));
            if (ok) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                result.loadout.push_back(p.lexically_normal());
            }
        } else if (key == "entity") {
            Entity e;
            std::string script;
            ok = static_cast<bool>(ls >> std::quoted(e.name) >> std::quoted(e.type)
                                      >> e.position.x >> e.position.y >> e.position.z >> e.yawDegrees
                                      >> e.size.x >> e.size.y >> e.size.z
                                      >> e.color.r >> e.color.g >> e.color.b >> std::quoted(script));
            if (ok) {
                if (!script.empty()) {
                    std::filesystem::path p = pathFromUtf8(script);
                    if (p.is_relative())
                        p = std::filesystem::absolute(file).parent_path() / p;
                    e.scriptPath = p.lexically_normal();
                }
                result.entities.push_back(std::move(e));
                lastObject = LastObject::Entity;
            }
        } else {
            ok = false;
        }
        if (!ok) {
            setError(error, file.string() + ":" + std::to_string(lineNo) + ": invalid line: " + line);
            return false;
        }
    }
    scene = std::move(result);
    return true;
}

glm::mat4 Entity::modelTransform() const
{
    if (!model)
        return glm::mat4(1.0f);
    const Aabb& b = model->localBounds;
    const float height = std::max(b.max.y - b.min.y, 1e-4f);
    const float fit = size.y / height * modelScale;
    const glm::vec3 center = (b.min + b.max) * 0.5f;
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    // Entity yaw 0 faces +X; glTF characters face +Z, hence the extra 90 degrees.
    m = glm::rotate(m, glm::radians(90.0f - yawDegrees + modelYaw), glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::scale(m, glm::vec3(fit));
    return glm::translate(m, glm::vec3(-center.x, -b.min.y, -center.z));
}

void updateAnimations(Scene& scene, float dt)
{
    for (Entity& e : scene.entities) {
        e.flash = std::max(0.0f, e.flash - dt);
        if (!e.model || !e.model->skeleton)
            continue;
        const Skeleton& skeleton = *e.model->skeleton;
        if (e.animator.current.clip < 0 && !e.animation.empty())
            e.animator.play(skeleton, e.animation, true, 1.0f, 0.0f);
        e.animator.update(skeleton, dt);
    }
}

Aabb Scene::objectBounds(int object) const
{
    if (isBox(object))
        return boxes[static_cast<std::size_t>(object)].bounds();
    if (isModel(object))
        return model(object).bounds();
    if (isEntity(object))
        return entity(object).bounds();
    return {};
}

int pickObject(const Scene& scene, glm::vec3 origin, glm::vec3 dir)
{
    int best = -1;
    float bestT = 0.0f;
    // Entities first: they are small and usually stand inside larger geometry.
    for (int i = scene.objectCount() - 1; i >= 0; --i) {
        const Aabb bounds = scene.objectBounds(i);
        if (auto t = rayAabb(origin, dir, bounds); t && (best < 0 || *t < bestT - 1e-4f)) {
            best = i;
            bestT = *t;
        }
    }
    return best;
}

} // namespace engine
