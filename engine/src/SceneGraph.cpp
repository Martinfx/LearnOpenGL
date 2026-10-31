#include "engine/SceneGraph.h"
#include "engine/Material.h"

#include <algorithm>
#include <functional>
#include <system_error>

namespace engine {

namespace {
const std::string kEmpty;

std::string& parentRef(Scene& scene, int object)
{
    if (scene.isBox(object))
        return scene.boxes[static_cast<std::size_t>(object)].parent;
    if (scene.isModel(object))
        return scene.model(object).parent;
    return scene.entity(object).parent;
}

// Calls fn(parentField) for every node (groups and objects).
void forEachParentField(Scene& scene, const std::function<void(std::string&)>& fn)
{
    for (Group& g : scene.groups)
        fn(g.parent);
    for (int i = 0; i < scene.objectCount(); ++i)
        fn(parentRef(scene, i));
}

bool samePath(const std::filesystem::path& a, const std::filesystem::path& b)
{
    if (a.empty() || b.empty())
        return false;
    std::error_code ec;
    if (std::filesystem::exists(a, ec) && std::filesystem::exists(b, ec))
        return std::filesystem::equivalent(a, b, ec);
    return std::filesystem::absolute(a).lexically_normal() == std::filesystem::absolute(b).lexically_normal();
}

void addMaterial(std::vector<AssetUse>& out, const std::filesystem::path& path, const std::string& detail)
{
    if (path.empty())
        return;
    out.push_back({AssetKind::Material, path, detail, 0});
    std::string error;
    if (auto material = loadMaterial(path, &error))
        for (int k = 0; k < kMapCount; ++k) {
            const auto& map = material->mapPaths[static_cast<std::size_t>(k)];
            if (!map.empty())
                out.push_back({AssetKind::Texture, map, mapLabel(static_cast<MapKind>(k)), 1});
        }
}
} // namespace

const std::string& objectName(const Scene& scene, int object)
{
    if (scene.isBox(object))
        return scene.boxes[static_cast<std::size_t>(object)].name;
    if (scene.isModel(object))
        return scene.model(object).name;
    if (scene.isEntity(object))
        return scene.entity(object).name;
    return kEmpty;
}

const std::string& objectParent(const Scene& scene, int object)
{
    if (scene.isBox(object))
        return scene.boxes[static_cast<std::size_t>(object)].parent;
    if (scene.isModel(object))
        return scene.model(object).parent;
    if (scene.isEntity(object))
        return scene.entity(object).parent;
    return kEmpty;
}

glm::vec3 objectPosition(const Scene& scene, int object)
{
    if (scene.isBox(object))
        return scene.boxes[static_cast<std::size_t>(object)].position;
    if (scene.isModel(object))
        return scene.model(object).position;
    if (scene.isEntity(object))
        return scene.entity(object).position;
    return glm::vec3(0.0f);
}

void translateObject(Scene& scene, int object, glm::vec3 delta)
{
    if (scene.isBox(object))
        scene.boxes[static_cast<std::size_t>(object)].position += delta;
    else if (scene.isModel(object))
        scene.model(object).position += delta;
    else if (scene.isEntity(object))
        scene.entity(object).position += delta;
}

int findObject(const Scene& scene, std::string_view name)
{
    for (int i = 0; i < scene.objectCount(); ++i)
        if (objectName(scene, i) == name)
            return i;
    return -1;
}

int findGroup(const Scene& scene, std::string_view name)
{
    for (std::size_t i = 0; i < scene.groups.size(); ++i)
        if (scene.groups[i].name == name)
            return static_cast<int>(i);
    return -1;
}

bool nodeExists(const Scene& scene, std::string_view name)
{
    return findGroup(scene, name) >= 0 || findObject(scene, name) >= 0;
}

std::string uniqueNodeName(const Scene& scene, const std::string& base)
{
    if (!nodeExists(scene, base))
        return base;
    // "Crate 2" -> continue counting from 2 instead of making "Crate 2 2".
    std::string stem = base;
    int n = 2;
    if (const auto space = base.find_last_of(' '); space != std::string::npos && space + 1 < base.size() &&
                                                   std::all_of(base.begin() + static_cast<long>(space) + 1, base.end(),
                                                               [](char c) { return c >= '0' && c <= '9'; })) {
        stem = base.substr(0, space);
        n = std::stoi(base.substr(space + 1)) + 1;
    }
    for (;; ++n) {
        std::string candidate = stem + " " + std::to_string(n);
        if (!nodeExists(scene, candidate))
            return candidate;
    }
}

std::string nodeParent(const Scene& scene, std::string_view name)
{
    if (const int g = findGroup(scene, name); g >= 0)
        return scene.groups[static_cast<std::size_t>(g)].parent;
    if (const int o = findObject(scene, name); o >= 0)
        return objectParent(scene, o);
    return {};
}

bool isAncestor(const Scene& scene, std::string_view ancestor, std::string_view node)
{
    std::string current(node);
    // Bounded walk so broken files with cycles cannot hang the editor.
    for (int steps = 0; !current.empty() && steps <= scene.objectCount() + static_cast<int>(scene.groups.size()); ++steps) {
        if (current == ancestor)
            return true;
        current = nodeParent(scene, current);
    }
    return false;
}

bool setNodeParent(Scene& scene, std::string_view node, const std::string& parent)
{
    if (!parent.empty() && (!nodeExists(scene, parent) || isAncestor(scene, node, parent)))
        return false;
    if (const int g = findGroup(scene, node); g >= 0) {
        scene.groups[static_cast<std::size_t>(g)].parent = parent;
        return true;
    }
    if (const int o = findObject(scene, node); o >= 0) {
        parentRef(scene, o) = parent;
        return true;
    }
    return false;
}

bool renameNode(Scene& scene, std::string_view oldName, const std::string& newName)
{
    if (newName.empty() || oldName == newName)
        return oldName == newName;
    if (nodeExists(scene, newName))
        return false;
    const std::string old(oldName);
    if (const int g = findGroup(scene, old); g >= 0)
        scene.groups[static_cast<std::size_t>(g)].name = newName;
    else if (const int o = findObject(scene, old); o >= 0) {
        if (scene.isBox(o))
            scene.boxes[static_cast<std::size_t>(o)].name = newName;
        else if (scene.isModel(o))
            scene.model(o).name = newName;
        else
            scene.entity(o).name = newName;
    } else
        return false;
    forEachParentField(scene, [&](std::string& p) {
        if (p == old)
            p = newName;
    });
    return true;
}

std::vector<int> descendantObjects(const Scene& scene, std::string_view name)
{
    std::vector<int> result;
    std::vector<std::string> pending{std::string(name)};
    std::vector<std::string> visited;
    while (!pending.empty()) {
        const std::string current = pending.front();
        pending.erase(pending.begin());
        if (std::find(visited.begin(), visited.end(), current) != visited.end())
            continue;
        visited.push_back(current);
        for (const Group& g : scene.groups)
            if (g.parent == current)
                pending.push_back(g.name);
        for (int i = 0; i < scene.objectCount(); ++i)
            if (objectParent(scene, i) == current) {
                result.push_back(i);
                pending.push_back(objectName(scene, i));
            }
    }
    return result;
}

void reparentChildren(Scene& scene, std::string_view removedName, const std::string& newParent)
{
    const std::string removed(removedName);
    forEachParentField(scene, [&](std::string& p) {
        if (p == removed)
            p = newParent;
    });
}

void removeGroup(Scene& scene, std::string_view name)
{
    const int g = findGroup(scene, name);
    if (g < 0)
        return;
    const std::string removed = scene.groups[static_cast<std::size_t>(g)].name;
    const std::string up = scene.groups[static_cast<std::size_t>(g)].parent;
    scene.groups.erase(scene.groups.begin() + g);
    reparentChildren(scene, removed, up);
}

const char* assetKindLabel(AssetKind kind)
{
    switch (kind) {
    case AssetKind::Model: return "Model";
    case AssetKind::Character: return "Character";
    case AssetKind::Material: return "Material";
    case AssetKind::Texture: return "Texture";
    case AssetKind::Script: return "Script";
    case AssetKind::Weapon: return "Weapon";
    }
    return "File";
}

std::vector<AssetUse> objectDependencies(const Scene& scene, int object)
{
    std::vector<AssetUse> out;
    if (scene.isBox(object)) {
        addMaterial(out, scene.boxes[static_cast<std::size_t>(object)].materialPath, "Surface");
    } else if (scene.isModel(object)) {
        const Model& m = scene.model(object);
        out.push_back({AssetKind::Model, m.sourcePath, {}, 0});
        for (const auto& [slot, path] : m.materialOverrides)
            addMaterial(out, path, "Slot " + slot);
    } else if (scene.isEntity(object)) {
        const Entity& e = scene.entity(object);
        if (!e.scriptPath.empty())
            out.push_back({AssetKind::Script, e.scriptPath, {}, 0});
        if (!e.modelPath.empty())
            out.push_back({AssetKind::Character, e.modelPath, e.animation.empty() ? std::string() : "Clip " + e.animation, 0});
    }
    return out;
}

std::vector<int> objectsUsing(const Scene& scene, const std::filesystem::path& asset)
{
    std::vector<int> result;
    for (int i = 0; i < scene.objectCount(); ++i)
        for (const AssetUse& use : objectDependencies(scene, i))
            if (samePath(use.path, asset)) {
                result.push_back(i);
                break;
            }
    return result;
}

} // namespace engine
