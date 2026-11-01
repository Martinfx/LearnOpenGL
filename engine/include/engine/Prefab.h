#pragma once
#include "engine/Scene.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace engine {

// ---------------------------------------------------------------------------
// Prefabs: a group of boxes, models and entities saved as an asset (a .prefab
// file in the level format) and placed in levels as instances.
//
// An instance (level line `prefab "name" "file" x y z yaw`) is expanded on load:
// its objects become ordinary scene objects named "<instance>/<object>" under a
// group named after the instance, so the game, physics and the editor see plain
// objects. They remember where they came from (prefabInstance / prefabObject).
// When the level is saved, the instance objects are compared with a fresh
// expansion of the prefab and only the differences are written as `override`
// sub-lines: a moved, rotated or resized object (transform), a changed script
// property (prop) and a deleted object (removed). An object changed in any other
// way (another model, material...) is detached: saved as a normal object. So a
// change to the .prefab file shows in every instance, except where it was
// overridden.
// ---------------------------------------------------------------------------

// The prefab file, loaded once and shared (reloaded when it or a prefab nested in
// it changes on disk). Null on error.
std::shared_ptr<const Scene> loadPrefab(const std::filesystem::path& file, std::string* error = nullptr);
// Forgets the cached prefabs (all of them for an empty path).
void clearPrefabCache(const std::filesystem::path& file = {});

int findPrefabInstance(const Scene& scene, const std::string& name); // index into Scene::prefabs or -1
// The instance an object belongs to, or -1.
int prefabInstanceOf(const Scene& scene, int object);

// Adds the instance's objects to the scene and applies its overrides. The
// instance itself must already be in scene.prefabs (or be added after).
bool expandPrefab(Scene& scene, const PrefabInstance& instance, std::string* error = nullptr);
// Removes the objects and groups of an instance (not the instance).
void removePrefabObjects(Scene& scene, const std::string& instance);

// Compares the instance's objects with the prefab and writes the differences to
// instance.overrides. A whole instance moved in the editor (every object by the
// same offset) moves instance.position instead. `detached` gets the objects that
// changed beyond overrides (saved as normal objects). False when the prefab file
// cannot be read (the overrides stay as they were).
bool computePrefabOverrides(const Scene& scene, PrefabInstance& instance, const std::filesystem::path& levelDir,
                            std::vector<int>* detached = nullptr);

// Editor operations (all keep the scene consistent; undo is the editor's).
// Places a new instance (unique name from `name` or the file name).
int addPrefabInstance(Scene& scene, const std::filesystem::path& file, glm::vec3 position, float yawDegrees,
                      const std::string& parent = {}, std::string name = {}, std::string* error = nullptr);
// Moves or turns the whole instance (its objects keep their overrides).
void setPrefabTransform(Scene& scene, int instance, glm::vec3 position, float yawDegrees);
// Rebuilds instances from their (changed) prefab, keeping the overrides. With a
// path, only instances of that prefab. Returns how many were rebuilt.
int refreshPrefabInstances(Scene& scene, const std::filesystem::path& levelDir, const std::filesystem::path& file = {});
// Drops the overrides of an instance (back to the prefab as it is).
bool revertPrefabInstance(Scene& scene, int instance, std::string* error = nullptr);
// Turns the instance into normal objects (no link to the prefab any more).
void unpackPrefab(Scene& scene, int instance);
// Saves the instance as it is now into its prefab file (so every instance gets the
// change) and clears its overrides.
bool applyPrefabInstance(Scene& scene, int instance, const std::filesystem::path& levelDir, std::string* error = nullptr);
// Saves a group (with everything below it) as a new prefab, its pivot at the bottom
// centre of the group's bounds, and replaces the group by an instance of it.
bool createPrefabFromGroup(Scene& scene, const std::string& group, const std::filesystem::path& file, std::string* error = nullptr);

} // namespace engine
