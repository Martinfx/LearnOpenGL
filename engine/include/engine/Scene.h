#pragma once
#include "engine/Geometry.h"
#include <cstdint>
#include <filesystem>
#include <glm/vec2.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace engine {

// Axis-aligned box: quick blockout geometry placed in the editor.
struct Box {
    std::string name = "Box";
    glm::vec3 position{0.0f};       // center
    glm::vec3 size{1.0f};
    glm::vec3 color{0.7f, 0.7f, 0.7f}; // sRGB

    Aabb bounds() const { return Aabb::fromCenterSize(position, size); }
};

// Decoded RGBA8 image (sRGB), shared by all parts that use it.
struct Texture {
    std::uint64_t id = 0;            // GPU cache key
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

// One glTF primitive with node transforms baked in (model space).
struct MeshPart {
    std::uint64_t id = 0;            // unique per loaded part, used as GPU cache key
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;   // empty when the material has no texture
    std::shared_ptr<const Texture> texture; // base color texture, may be null
    std::vector<std::uint32_t> indices; // triangles
    glm::vec3 color{0.8f};           // sRGB
};

// Static level geometry imported from glTF (Blender: File > Export > glTF 2.0).
struct Model {
    std::string name = "Model";
    std::filesystem::path sourcePath; // absolute path of the .gltf/.glb
    glm::vec3 position{0.0f};
    std::vector<MeshPart> parts;
    Aabb localBounds;

    Aabb bounds() const { return {localBounds.min + position, localBounds.max + position}; }
};

// Gameplay object (enemy, pickup, trigger...). Behaviour comes from its Lua script.
struct Entity {
    std::string name = "Entity";
    std::string type = "empty";          // gameplay role, readable from scripts
    glm::vec3 position{0.0f};            // base (feet) position
    float yawDegrees = 0.0f;             // facing, 0 = +X, 90 = +Z
    float health = 100.0f;               // runtime: changed by weapons and scripts
    bool alive = true;                   // runtime: dead entities are not drawn or simulated
    glm::vec3 size{0.6f, 1.8f, 0.6f};    // marker box / character capsule size
    glm::vec3 color{0.8f, 0.25f, 0.2f};  // sRGB
    std::filesystem::path scriptPath;    // absolute path to a .lua file, empty = none
    // Values for the script's `properties` (set in the editor); missing keys use the script defaults.
    std::map<std::string, std::string> properties;

    Aabb bounds() const
    {
        const glm::vec3 half(size.x * 0.5f, 0.0f, size.z * 0.5f);
        return {position - half, position + half + glm::vec3(0.0f, size.y, 0.0f)};
    }
};

// Player movement and health, stored per level and edited in the editor.
struct PlayerSettings {
    float walkSpeed = 5.0f;     // m/s
    float sprintSpeed = 8.5f;
    float jumpSpeed = 5.5f;
    float gravity = 18.0f;      // m/s^2, a bit above earth for snappy jumps
    float airControl = 0.15f;   // 0 = none, 1 = full
    float radius = 0.3f;
    float height = 1.8f;
    float eyeHeight = 1.65f;
    float stepHeight = 0.4f;    // stairs up to this height are climbed automatically
    float maxSlopeDegrees = 45.0f;
    float killY = -50.0f;       // respawn when falling below this
    float maxHealth = 100.0f;
};

struct Scene {
    std::vector<Box> boxes;
    std::vector<Model> models;
    std::vector<Entity> entities;
    std::vector<std::filesystem::path> loadout;
    PlayerSettings player; // player's .weapon files (absolute), slot 1, 2, ...
    glm::vec3 playerStart{0.0f, 0.0f, 0.0f}; // feet position
    glm::vec3 sunDirection{-0.4f, -1.0f, -0.3f};

    // Objects are addressed by one index: boxes first, then models, then entities.
    int objectCount() const { return static_cast<int>(boxes.size() + models.size() + entities.size()); }
    int firstModel() const { return static_cast<int>(boxes.size()); }
    int firstEntity() const { return static_cast<int>(boxes.size() + models.size()); }
    bool isBox(int object) const { return object >= 0 && object < firstModel(); }
    bool isModel(int object) const { return object >= firstModel() && object < firstEntity(); }
    bool isEntity(int object) const { return object >= firstEntity() && object < objectCount(); }
    Model& model(int object) { return models[static_cast<std::size_t>(object - firstModel())]; }
    const Model& model(int object) const { return models[static_cast<std::size_t>(object - firstModel())]; }
    Entity& entity(int object) { return entities[static_cast<std::size_t>(object - firstEntity())]; }
    const Entity& entity(int object) const { return entities[static_cast<std::size_t>(object - firstEntity())]; }
    Aabb objectBounds(int object) const;
};

Scene makeDefaultScene();

// Level files are text; model paths are stored relative to the level file.
bool saveScene(const Scene& scene, const std::filesystem::path& file, std::string* error = nullptr);
bool loadScene(Scene& scene, const std::filesystem::path& file, std::string* error = nullptr);

// Loads every mesh of a glTF 2.0 file (.gltf or .glb) into model.parts.
bool loadModelFile(Model& model, const std::filesystem::path& file, std::string* error = nullptr);

// A tunable declared by a script in its `properties` table (shown in the editor).
struct ScriptProperty {
    std::string name;
    std::string defaultValue;   // as text: number, true/false or string
    std::string description;
};

// Reads the `properties = { {"name", default, "description"}, ... }` table of a
// Lua script without running its callbacks.
bool readScriptProperties(const std::filesystem::path& script, std::vector<ScriptProperty>& out, std::string* error = nullptr);

// Object index (see Scene::objectCount) of the first box or model hit by the ray, or -1.
int pickObject(const Scene& scene, glm::vec3 origin, glm::vec3 dir);

} // namespace engine
