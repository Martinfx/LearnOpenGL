#include "engine/Prefab.h"
#include "engine/Vfs.h"
#include "engine/SceneGraph.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {

namespace {

struct CacheEntry {
    std::shared_ptr<const Scene> scene;
    // The file and the prefabs nested in it, with their times when it was read.
    std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> files;
};
std::mutex cacheMutex;
std::map<std::filesystem::path, CacheEntry> cache;
thread_local int loadDepth = 0; // prefabs nested in themselves

std::filesystem::file_time_type writeTime(const std::filesystem::path& file)
{
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(file, ec);
    return ec ? std::filesystem::file_time_type{} : t;
}

std::filesystem::path key(const std::filesystem::path& file)
{
    return std::filesystem::absolute(file).lexically_normal();
}

ObjectId mixId(ObjectId a, ObjectId b)
{
    auto mix = [](std::uint64_t x) {
        x += 0x9e3779b97f4a7c15ull;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
        return x ^ (x >> 31);
    };
    const ObjectId id = mix(a ^ mix(b));
    return id == 0 ? 1 : id;
}

// Prefab space <-> level for one instance (turn around Y as Model::rotation.y).
struct Placement {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    glm::vec3 toWorld(glm::vec3 p) const { return position + rotate(p, yaw); }
    glm::vec3 toLocal(glm::vec3 p) const { return rotate(p - position, -yaw); }
    static glm::vec3 rotate(glm::vec3 p, float degrees)
    {
        const glm::mat4 r = glm::rotate(glm::mat4(1.0f), glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
        return glm::vec3(r * glm::vec4(p, 1.0f));
    }
};

// Entity yaw turns the other way (0 = +X, 90 = +Z) than a model's rotation.y.
void place(Box& b, const Placement& p)
{
    b.position = p.toWorld(b.position);
    b.rotation.y += p.yaw;
}
void place(Model& m, const Placement& p)
{
    m.position = p.toWorld(m.position);
    m.rotation.y += p.yaw;
}
void place(Entity& e, const Placement& p)
{
    e.position = p.toWorld(e.position);
    e.yawDegrees -= p.yaw;
}

PrefabOverride localTransform(const Box& b, const Placement& p)
{
    PrefabOverride o;
    o.position = p.toLocal(b.position);
    o.rotation = b.rotation - glm::vec3(0.0f, p.yaw, 0.0f);
    o.scale = b.size;
    return o;
}
PrefabOverride localTransform(const Model& m, const Placement& p)
{
    PrefabOverride o;
    o.position = p.toLocal(m.position);
    o.rotation = m.rotation - glm::vec3(0.0f, p.yaw, 0.0f);
    o.scale = glm::vec3(m.scale);
    return o;
}
PrefabOverride localTransform(const Entity& e, const Placement& p)
{
    PrefabOverride o;
    o.position = p.toLocal(e.position);
    o.rotation = glm::vec3(0.0f, e.yawDegrees + p.yaw, 0.0f);
    o.scale = e.size;
    return o;
}

void applyTransform(Box& b, const PrefabOverride& o, const Placement& p)
{
    b.position = p.toWorld(o.position);
    b.rotation = o.rotation + glm::vec3(0.0f, p.yaw, 0.0f);
    b.size = o.scale;
}
void applyTransform(Model& m, const PrefabOverride& o, const Placement& p)
{
    m.position = p.toWorld(o.position);
    m.rotation = o.rotation + glm::vec3(0.0f, p.yaw, 0.0f);
    m.scale = o.scale.x;
}
void applyTransform(Entity& e, const PrefabOverride& o, const Placement& p)
{
    e.position = p.toWorld(o.position);
    e.yawDegrees = o.rotation.y - p.yaw;
    e.size = o.scale;
}

float wrapDegrees(float d)
{
    d = std::fmod(d, 360.0f);
    if (d > 180.0f)
        d -= 360.0f;
    if (d <= -180.0f)
        d += 360.0f;
    return d;
}

bool near(glm::vec3 a, glm::vec3 b, float eps = 1e-3f)
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(eps)));
}
bool sameTransform(const PrefabOverride& a, const PrefabOverride& b)
{
    glm::vec3 dr = a.rotation - b.rotation;
    dr = {wrapDegrees(dr.x), wrapDegrees(dr.y), wrapDegrees(dr.z)};
    return near(a.position, b.position) && near(dr, glm::vec3(0.0f), 0.01f) && near(a.scale, b.scale);
}

// Copies the transform, name, hierarchy and ID of `from` so that only the other
// fields are compared.
template <typename T>
void copyIdentity(T& to, const T& from)
{
    to.name = from.name;
    to.parent = from.parent;
    to.id = from.id;
    to.position = from.position;
    if constexpr (std::is_same_v<T, Box>) {
        to.rotation = from.rotation;
        to.size = from.size;
    } else if constexpr (std::is_same_v<T, Model>) {
        to.rotation = from.rotation;
        to.scale = from.scale;
    } else {
        to.yawDegrees = from.yawDegrees;
        to.size = from.size;
    }
}

std::string text(const Box& b, const std::filesystem::path& dir)
{
    std::ostringstream s;
    s.precision(5);
    writeBox(s, b, dir);
    return s.str();
}
std::string text(const Model& m, const std::filesystem::path& dir)
{
    std::ostringstream s;
    s.precision(5);
    writeModel(s, m, dir, false);
    return s.str();
}
std::string text(const Entity& e, const std::filesystem::path& dir)
{
    std::ostringstream s;
    s.precision(5);
    writeEntity(s, e, dir);
    return s.str();
}

} // namespace

std::shared_ptr<const Scene> loadPrefab(const std::filesystem::path& file, std::string* error)
{
    const std::filesystem::path k = key(file);
    {
        std::lock_guard lock(cacheMutex);
        if (const auto it = cache.find(k); it != cache.end()) {
            bool fresh = true;
            for (const auto& [f, t] : it->second.files)
                fresh = fresh && writeTime(f) == t;
            if (fresh)
                return it->second.scene;
        }
    }
    if (loadDepth > 8) {
        if (error)
            *error = "prefabs nested too deep (a prefab inside itself?): " + file.string();
        return nullptr;
    }
    if (!fileExists(k)) {
        if (error)
            *error = "missing prefab " + file.string();
        return nullptr;
    }
    CacheEntry entry;
    entry.files.emplace_back(k, writeTime(k));
    auto scene = std::make_shared<Scene>();
    ++loadDepth;
    const bool ok = loadScene(*scene, k, error);
    --loadDepth;
    if (!ok)
        return nullptr;
    scene->terrain.reset();
    for (const PrefabInstance& nested : scene->prefabs) {
        std::lock_guard lock(cacheMutex);
        if (const auto it = cache.find(key(nested.path)); it != cache.end())
            entry.files.insert(entry.files.end(), it->second.files.begin(), it->second.files.end());
    }
    entry.scene = scene;
    std::lock_guard lock(cacheMutex);
    cache[k] = entry;
    return scene;
}

void clearPrefabCache(const std::filesystem::path& file)
{
    std::lock_guard lock(cacheMutex);
    if (file.empty())
        cache.clear();
    else
        cache.erase(key(file));
}

int findPrefabInstance(const Scene& scene, const std::string& name)
{
    for (std::size_t i = 0; i < scene.prefabs.size(); ++i)
        if (scene.prefabs[i].name == name)
            return static_cast<int>(i);
    return -1;
}

int prefabInstanceOf(const Scene& scene, int object)
{
    if (object < 0 || object >= scene.objectCount())
        return -1;
    const std::string& inst = scene.isBox(object)     ? scene.boxes[static_cast<std::size_t>(object)].prefabInstance
                              : scene.isModel(object) ? scene.model(object).prefabInstance
                                                      : scene.entity(object).prefabInstance;
    return inst.empty() ? -1 : findPrefabInstance(scene, inst);
}

namespace {
void expandFrom(Scene& scene, const PrefabInstance& inst, const Scene* src)
{
    const std::string prefix = inst.name + "/";
    const Placement placement{inst.position, inst.yawDegrees};
    std::set<std::string> removed;
    std::map<std::string, const PrefabOverride*> transforms;
    std::multimap<std::string, const PrefabOverride*> props;
    for (const PrefabOverride& o : inst.overrides) {
        if (o.kind == PrefabOverride::Kind::Removed)
            removed.insert(o.object);
        else if (o.kind == PrefabOverride::Kind::Transform)
            transforms[o.object] = &o;
        else
            props.emplace(o.object, &o);
    }
    auto mapParent = [&](const std::string& p) { return p.empty() ? inst.name : prefix + p; };
    std::set<std::string> entityNames;
    for (const Entity& e : src->entities)
        entityNames.insert(e.name);

    Group root;
    root.name = inst.name;
    root.parent = inst.parent;
    root.prefabInstance = inst.name;
    scene.groups.push_back(root);
    for (const Group& g : src->groups) {
        Group copy;
        copy.name = prefix + g.name;
        copy.parent = mapParent(g.parent);
        copy.prefabInstance = inst.name;
        scene.groups.push_back(std::move(copy));
    }
    auto adopt = [&](auto& o, const auto& from) {
        o.name = prefix + from.name;
        o.parent = mapParent(from.parent);
        o.id = mixId(inst.id, from.id);
        o.prefabInstance = inst.name;
        o.prefabObject = from.name;
        place(o, placement);
        if (const auto it = transforms.find(from.name); it != transforms.end())
            applyTransform(o, *it->second, placement);
    };
    for (const Box& b : src->boxes)
        if (!removed.count(b.name)) {
            scene.boxes.push_back(b);
            adopt(scene.boxes.back(), b);
        }
    for (const Model& m : src->models)
        if (!removed.count(m.name)) {
            scene.models.push_back(m);
            adopt(scene.models.back(), m);
        }
    for (const Entity& e : src->entities)
        if (!removed.count(e.name)) {
            scene.entities.push_back(e);
            Entity& copy = scene.entities.back();
            adopt(copy, e);
            for (EntityOutput& out : copy.outputs) // links inside the prefab point to this instance's entities
                if (entityNames.count(out.target))
                    out.target = prefix + out.target;
            for (auto [it, end] = props.equal_range(e.name); it != end; ++it)
                copy.properties[it->second->key] = it->second->value;
        }
}
bool overridesAgainst(const Scene& scene, PrefabInstance& inst, const std::filesystem::path& levelDir, std::vector<int>* detached,
                      const Scene& src);
} // namespace

bool expandPrefab(Scene& scene, const PrefabInstance& inst, std::string* error)
{
    const std::shared_ptr<const Scene> src = loadPrefab(inst.path, error);
    if (!src)
        return false;
    expandFrom(scene, inst, src.get());
    return true;
}

void removePrefabObjects(Scene& scene, const std::string& instance)
{
    auto belongs = [&](const auto& o) { return o.prefabInstance == instance; };
    std::erase_if(scene.boxes, belongs);
    std::erase_if(scene.models, belongs);
    std::erase_if(scene.entities, belongs);
    std::erase_if(scene.groups, belongs);
}

bool computePrefabOverrides(const Scene& scene, PrefabInstance& inst, const std::filesystem::path& levelDir, std::vector<int>* detached)
{
    const std::shared_ptr<const Scene> src = loadPrefab(inst.path);
    return src && overridesAgainst(scene, inst, levelDir, detached, *src);
}

namespace {
bool overridesAgainst(const Scene& scene, PrefabInstance& inst, const std::filesystem::path& levelDir, std::vector<int>* detached,
                      const Scene& src)
{
    PrefabInstance pristineInst = inst;
    pristineInst.overrides.clear();
    Scene pristine;
    expandFrom(pristine, pristineInst, &src);

    // Current objects of the instance by their prefab name (first one wins; copies
    // pasted with the same origin are detached).
    std::map<std::string, int> current;
    std::vector<int> extra;
    for (int o = 0; o < scene.objectCount(); ++o) {
        const std::string* instName = scene.isBox(o)     ? &scene.boxes[static_cast<std::size_t>(o)].prefabInstance
                                      : scene.isModel(o) ? &scene.model(o).prefabInstance
                                                         : &scene.entity(o).prefabInstance;
        if (*instName != inst.name)
            continue;
        const std::string& obj = scene.isBox(o)     ? scene.boxes[static_cast<std::size_t>(o)].prefabObject
                                 : scene.isModel(o) ? scene.model(o).prefabObject
                                                    : scene.entity(o).prefabObject;
        if (!current.emplace(obj, o).second)
            extra.push_back(o);
    }

    // The whole instance moved (the editor moves a group's objects together): every
    // object off by the same offset from where the instance last put it becomes the
    // instance's new position.
    {
        Scene placed;
        expandFrom(placed, inst, &src);
        std::optional<glm::vec3> delta;
        bool same = !current.empty();
        for (int p = 0; p < placed.objectCount() && same; ++p) {
            const std::string& obj = placed.isBox(p)     ? placed.boxes[static_cast<std::size_t>(p)].prefabObject
                                     : placed.isModel(p) ? placed.model(p).prefabObject
                                                         : placed.entity(p).prefabObject;
            const auto it = current.find(obj);
            if (it == current.end())
                continue;
            const glm::vec3 d = objectPosition(scene, it->second) - objectPosition(placed, p);
            if (!delta)
                delta = d;
            same = near(*delta, d);
        }
        if (same && delta && !near(*delta, glm::vec3(0.0f), 1e-4f)) {
            inst.position += *delta;
            pristineInst.position = inst.position;
            pristine = Scene{};
            expandFrom(pristine, pristineInst, &src);
        }
    }

    const Placement placement{inst.position, inst.yawDegrees};
    inst.overrides.clear();
    auto compare = [&](const auto& now, const auto& before, int object) {
        const PrefabOverride a = localTransform(now, placement), b = localTransform(before, placement);
        auto copy = now;
        copyIdentity(copy, before);
        if constexpr (std::is_same_v<std::decay_t<decltype(now)>, Entity>)
            copy.properties = before.properties;
        if (text(copy, levelDir) != text(before, levelDir)) {
            // Changed beyond what an override holds: saved as its own object.
            if (detached)
                detached->push_back(object);
            PrefabOverride r;
            r.kind = PrefabOverride::Kind::Removed;
            r.object = before.prefabObject;
            inst.overrides.push_back(r);
            return;
        }
        if constexpr (std::is_same_v<std::decay_t<decltype(now)>, Entity>) {
            for (const auto& [k, v] : before.properties)
                if (!now.properties.count(k)) { // a removed property cannot be an override
                    if (detached)
                        detached->push_back(object);
                    PrefabOverride r;
                    r.kind = PrefabOverride::Kind::Removed;
                    r.object = before.prefabObject;
                    inst.overrides.push_back(r);
                    return;
                }
        }
        if (!sameTransform(a, b)) {
            PrefabOverride t = a;
            t.kind = PrefabOverride::Kind::Transform;
            t.object = before.prefabObject;
            inst.overrides.push_back(t);
        }
        if constexpr (std::is_same_v<std::decay_t<decltype(now)>, Entity>) {
            for (const auto& [k, v] : now.properties) {
                const auto it = before.properties.find(k);
                if (it == before.properties.end() || it->second != v) {
                    PrefabOverride p;
                    p.kind = PrefabOverride::Kind::Property;
                    p.object = before.prefabObject;
                    p.key = k;
                    p.value = v;
                    inst.overrides.push_back(p);
                }
            }
        }
    };
    for (int p = 0; p < pristine.objectCount(); ++p) {
        const std::string& obj = pristine.isBox(p)     ? pristine.boxes[static_cast<std::size_t>(p)].prefabObject
                                 : pristine.isModel(p) ? pristine.model(p).prefabObject
                                                       : pristine.entity(p).prefabObject;
        const auto it = current.find(obj);
        const int o = it == current.end() ? -1 : it->second;
        const bool sameKind = o >= 0 && pristine.isBox(p) == scene.isBox(o) && pristine.isModel(p) == scene.isModel(o);
        if (!sameKind) {
            PrefabOverride r;
            r.kind = PrefabOverride::Kind::Removed;
            r.object = obj;
            inst.overrides.push_back(r);
            if (o >= 0 && detached)
                detached->push_back(o);
            continue;
        }
        if (scene.isBox(o))
            compare(scene.boxes[static_cast<std::size_t>(o)], pristine.boxes[static_cast<std::size_t>(p)], o);
        else if (scene.isModel(o))
            compare(scene.model(o), pristine.model(p), o);
        else
            compare(scene.entity(o), pristine.entity(p), o);
    }
    // Objects that are not in the prefab (copies) are saved as normal objects.
    std::set<std::string> known;
    for (int p = 0; p < pristine.objectCount(); ++p)
        known.insert(pristine.isBox(p)     ? pristine.boxes[static_cast<std::size_t>(p)].prefabObject
                     : pristine.isModel(p) ? pristine.model(p).prefabObject
                                           : pristine.entity(p).prefabObject);
    for (const auto& [obj, o] : current)
        if (!known.count(obj))
            extra.push_back(o);
    if (detached)
        detached->insert(detached->end(), extra.begin(), extra.end());
    return true;
}
} // namespace

int addPrefabInstance(Scene& scene, const std::filesystem::path& file, glm::vec3 position, float yawDegrees, const std::string& parent,
                      std::string name, std::string* error)
{
    PrefabInstance inst;
    inst.path = key(file);
    if (name.empty())
        name = inst.path.stem().string();
    inst.name = uniqueNodeName(scene, name);
    inst.parent = parent;
    inst.position = position;
    inst.yawDegrees = yawDegrees;
    inst.id = newObjectId();
    if (!expandPrefab(scene, inst, error))
        return -1;
    scene.prefabs.push_back(std::move(inst));
    return static_cast<int>(scene.prefabs.size()) - 1;
}

void setPrefabTransform(Scene& scene, int index, glm::vec3 position, float yawDegrees)
{
    if (index < 0 || index >= static_cast<int>(scene.prefabs.size()))
        return;
    PrefabInstance& inst = scene.prefabs[static_cast<std::size_t>(index)];
    const Placement before{inst.position, inst.yawDegrees}, after{position, yawDegrees};
    auto move = [&](auto& o) {
        if (o.prefabInstance != inst.name)
            return;
        applyTransform(o, localTransform(o, before), after);
    };
    for (Box& b : scene.boxes)
        move(b);
    for (Model& m : scene.models)
        move(m);
    for (Entity& e : scene.entities)
        move(e);
    inst.position = position;
    inst.yawDegrees = yawDegrees;
}

int refreshPrefabInstances(Scene& scene, const std::filesystem::path& levelDir, const std::filesystem::path& file)
{
    int count = 0;
    for (PrefabInstance& inst : scene.prefabs) {
        if (!file.empty() && key(inst.path) != key(file))
            continue;
        // The overrides against the prefab as this instance was built from it (the
        // cached copy, even when the file has changed since).
        std::shared_ptr<const Scene> old;
        {
            std::lock_guard lock(cacheMutex);
            if (const auto it = cache.find(key(inst.path)); it != cache.end())
                old = it->second.scene;
        }
        PrefabInstance updated = inst;
        std::vector<int> detached;
        if (old)
            overridesAgainst(scene, updated, levelDir, &detached, *old);
        if (const int g = findGroup(scene, inst.name); g >= 0) // keep the group's place in the hierarchy
            updated.parent = scene.groups[static_cast<std::size_t>(g)].parent;
        // Detached objects stay as normal objects.
        std::set<std::string> groups;
        for (const Group& g : scene.groups)
            if (g.prefabInstance == inst.name)
                groups.insert(g.name);
        for (int o : detached) {
            auto release = [&](auto& x) {
                x.prefabInstance.clear();
                x.prefabObject.clear();
                if (groups.count(x.parent))
                    x.parent = updated.parent;
            };
            if (scene.isBox(o))
                release(scene.boxes[static_cast<std::size_t>(o)]);
            else if (scene.isModel(o))
                release(scene.model(o));
            else
                release(scene.entity(o));
        }
        const std::shared_ptr<const Scene> now = (clearPrefabCache(inst.path), loadPrefab(inst.path));
        if (!now)
            continue; // e.g. being written; the next change refreshes it
        removePrefabObjects(scene, inst.name);
        inst = updated;
        expandFrom(scene, inst, now.get());
        ++count;
    }
    return count;
}

bool revertPrefabInstance(Scene& scene, int index, std::string* error)
{
    if (index < 0 || index >= static_cast<int>(scene.prefabs.size()))
        return false;
    PrefabInstance& inst = scene.prefabs[static_cast<std::size_t>(index)];
    if (const int g = findGroup(scene, inst.name); g >= 0)
        inst.parent = scene.groups[static_cast<std::size_t>(g)].parent;
    removePrefabObjects(scene, inst.name);
    inst.overrides.clear();
    return expandPrefab(scene, inst, error);
}

void unpackPrefab(Scene& scene, int index)
{
    if (index < 0 || index >= static_cast<int>(scene.prefabs.size()))
        return;
    const std::string name = scene.prefabs[static_cast<std::size_t>(index)].name;
    auto clear = [&](auto& o) {
        if (o.prefabInstance == name) {
            o.prefabInstance.clear();
            if constexpr (!std::is_same_v<std::decay_t<decltype(o)>, Group>)
                o.prefabObject.clear();
        }
    };
    for (Box& b : scene.boxes)
        clear(b);
    for (Model& m : scene.models)
        clear(m);
    for (Entity& e : scene.entities)
        clear(e);
    for (Group& g : scene.groups)
        clear(g);
    scene.prefabs.erase(scene.prefabs.begin() + index);
}

namespace {
// Objects of `scene` that `pick` selects, written as a prefab with `origin` and
// `yaw` mapped to the prefab's origin; `rename` gives their name in the prefab.
bool savePrefabFile(const Scene& scene, const std::vector<int>& objects, const std::vector<Group>& groups, const Placement& placement,
                    const std::function<std::string(const std::string&)>& rename, const std::filesystem::path& file, std::string* error)
{
    Scene out;
    out.environment = Environment{};
    out.groups = groups;
    std::set<std::string> entityNames;
    for (int o : objects)
        if (scene.isEntity(o))
            entityNames.insert(scene.entity(o).name);
    auto local = [&](auto& o) {
        applyTransform(o, localTransform(o, placement), Placement{});
        o.name = rename(o.name);
        o.parent = rename(o.parent);
        o.prefabInstance.clear();
        o.prefabObject.clear();
    };
    for (int o : objects) {
        if (scene.isBox(o)) {
            out.boxes.push_back(scene.boxes[static_cast<std::size_t>(o)]);
            local(out.boxes.back());
        } else if (scene.isModel(o)) {
            out.models.push_back(scene.model(o));
            local(out.models.back());
        } else {
            out.entities.push_back(scene.entity(o));
            Entity& e = out.entities.back();
            local(e);
            for (EntityOutput& link : e.outputs)
                if (entityNames.count(link.target))
                    link.target = rename(link.target);
        }
    }
    std::filesystem::create_directories(file.parent_path());
    if (!saveScene(out, file, error))
        return false;
    clearPrefabCache(file);
    return true;
}
} // namespace

bool applyPrefabInstance(Scene& scene, int index, const std::filesystem::path& levelDir, std::string* error)
{
    if (index < 0 || index >= static_cast<int>(scene.prefabs.size()))
        return false;
    const PrefabInstance inst = scene.prefabs[static_cast<std::size_t>(index)];
    std::vector<int> objects = descendantObjects(scene, inst.name); // also objects added to the instance's group
    for (int o = 0; o < scene.objectCount(); ++o)
        if (prefabInstanceOf(scene, o) == index && std::find(objects.begin(), objects.end(), o) == objects.end())
            objects.push_back(o);
    const std::string prefix = inst.name + "/";
    auto rename = [&](const std::string& n) {
        if (n == inst.name)
            return std::string();
        return n.rfind(prefix, 0) == 0 ? n.substr(prefix.size()) : n;
    };
    std::vector<Group> groups;
    for (const Group& g : scene.groups)
        if (g.prefabInstance == inst.name && g.name != inst.name)
            groups.push_back({rename(g.name), rename(g.parent), {}});
    if (!savePrefabFile(scene, objects, groups, Placement{inst.position, inst.yawDegrees}, rename, inst.path, error))
        return false;
    // Every instance of the prefab (this one now has no overrides).
    for (int o : objects) // objects added to the group are part of the prefab now
        if (scene.isBox(o))
            scene.boxes[static_cast<std::size_t>(o)].prefabInstance = inst.name;
        else if (scene.isModel(o))
            scene.model(o).prefabInstance = inst.name;
        else
            scene.entity(o).prefabInstance = inst.name;
    scene.prefabs[static_cast<std::size_t>(index)].overrides.clear();
    removePrefabObjects(scene, inst.name);
    expandPrefab(scene, scene.prefabs[static_cast<std::size_t>(index)], nullptr);
    for (std::size_t i = 0; i < scene.prefabs.size(); ++i)
        if (static_cast<int>(i) != index && key(scene.prefabs[i].path) == key(inst.path)) {
            PrefabInstance& other = scene.prefabs[i];
            if (const int g = findGroup(scene, other.name); g >= 0)
                other.parent = scene.groups[static_cast<std::size_t>(g)].parent;
            removePrefabObjects(scene, other.name);
            expandPrefab(scene, other, nullptr); // with its stored overrides
        }
    (void)levelDir;
    return true;
}

bool createPrefabFromGroup(Scene& scene, const std::string& group, const std::filesystem::path& file, std::string* error)
{
    const int g = findGroup(scene, group);
    if (g < 0) {
        if (error)
            *error = "no group " + group;
        return false;
    }
    const std::vector<int> objects = descendantObjects(scene, group);
    if (objects.empty()) {
        if (error)
            *error = "the group is empty";
        return false;
    }
    for (int o : objects)
        if (prefabInstanceOf(scene, o) >= 0) {
            if (error)
                *error = "the group contains a prefab instance; unpack it first";
            return false;
        }
    // Pivot: bottom centre of everything in the group.
    Aabb box{glm::vec3(1e30f), glm::vec3(-1e30f)};
    for (int o : objects) {
        const Aabb b = scene.objectBounds(o);
        box.min = glm::min(box.min, b.min);
        box.max = glm::max(box.max, b.max);
    }
    const glm::vec3 pivot((box.min.x + box.max.x) * 0.5f, box.min.y, (box.min.z + box.max.z) * 0.5f);
    std::vector<Group> groups;
    std::set<std::string> inside{group};
    for (bool grew = true; grew;) {
        grew = false;
        for (const Group& sub : scene.groups)
            if (!inside.count(sub.name) && inside.count(sub.parent)) {
                inside.insert(sub.name);
                groups.push_back({sub.name, sub.parent == group ? std::string() : sub.parent, {}});
                grew = true;
            }
    }
    auto rename = [&](const std::string& n) { return n == group ? std::string() : n; };
    if (!savePrefabFile(scene, objects, groups, Placement{pivot, 0.0f}, rename, file, error))
        return false;

    // Replace the group by an instance of the new prefab.
    const std::string parent = scene.groups[static_cast<std::size_t>(g)].parent;
    std::vector<int> sorted = objects;
    std::sort(sorted.rbegin(), sorted.rend());
    for (int o : sorted) {
        if (scene.isBox(o))
            scene.boxes.erase(scene.boxes.begin() + o);
        else if (scene.isModel(o))
            scene.models.erase(scene.models.begin() + (o - scene.firstModel()));
        else
            scene.entities.erase(scene.entities.begin() + (o - scene.firstEntity()));
    }
    std::erase_if(scene.groups, [&](const Group& x) { return inside.count(x.name) > 0; });
    if (addPrefabInstance(scene, file, pivot, 0.0f, parent, group, error) < 0)
        return false;
    return true;
}

} // namespace engine
