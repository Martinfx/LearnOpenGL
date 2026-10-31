#pragma once
#include "engine/Scene.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

// ---------------------------------------------------------------------------
// Scene hierarchy. Objects (boxes, models, entities) and groups form a tree by
// name: every node stores the name of its parent (empty = root). Positions stay
// in world space; moving a parent in the editor moves its subtree with it.
// ---------------------------------------------------------------------------

const std::string& objectName(const Scene& scene, int object);
const std::string& objectParent(const Scene& scene, int object);
glm::vec3 objectPosition(const Scene& scene, int object);
void translateObject(Scene& scene, int object, glm::vec3 delta);

int findObject(const Scene& scene, std::string_view name); // object index or -1
int findGroup(const Scene& scene, std::string_view name);  // index into Scene::groups or -1
bool nodeExists(const Scene& scene, std::string_view name);
// `base` if free, otherwise "base 2", "base 3", ...
std::string uniqueNodeName(const Scene& scene, const std::string& base);

// Parent of a node (object or group) by name; empty for roots and unknown names.
std::string nodeParent(const Scene& scene, std::string_view name);
// True when `ancestor` is `node` itself or above it in the tree.
bool isAncestor(const Scene& scene, std::string_view ancestor, std::string_view node);
// Re-parents a node. Refuses (returns false) when it would create a cycle or the parent does not exist.
bool setNodeParent(Scene& scene, std::string_view node, const std::string& parent);
// Renames an object or group and updates the children that refer to it.
bool renameNode(Scene& scene, std::string_view oldName, const std::string& newName);
// Objects anywhere below `name` (object or group), parents before children.
std::vector<int> descendantObjects(const Scene& scene, std::string_view name);
// Removes a group; its children move up to the group's parent.
void removeGroup(Scene& scene, std::string_view name);
// Call after deleting an object: its children move up to its parent.
void reparentChildren(Scene& scene, std::string_view removedName, const std::string& newParent);

// ---------------------------------------------------------------------------
// Dependencies: which files an object uses, and which objects use a file.
// ---------------------------------------------------------------------------

enum class AssetKind { Model, Character, Material, Texture, Script, Weapon };
const char* assetKindLabel(AssetKind kind);

struct AssetUse {
    AssetKind kind = AssetKind::Model;
    std::filesystem::path path;   // absolute
    std::string detail;           // e.g. material slot or texture map ("Normal map")
    int depth = 0;                // 0 = used directly, 1 = through a material
};

// Files the object uses: model, character, script, materials and their textures.
std::vector<AssetUse> objectDependencies(const Scene& scene, int object);
// Objects that use `asset` directly or through one of their materials.
std::vector<int> objectsUsing(const Scene& scene, const std::filesystem::path& asset);

} // namespace engine
