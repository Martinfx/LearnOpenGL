#pragma once
#include "engine/Geometry.h"
#include <cstdint>
#include <filesystem>
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

// One glTF primitive with node transforms baked in (model space).
struct MeshPart {
    std::uint64_t id = 0;            // unique per loaded part, used as GPU cache key
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
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

struct Scene {
    std::vector<Box> boxes;
    std::vector<Model> models;
    glm::vec3 playerStart{0.0f, 0.0f, 0.0f}; // feet position
    glm::vec3 sunDirection{-0.4f, -1.0f, -0.3f};

    // Objects are addressed by one index: boxes first, then models.
    int objectCount() const { return static_cast<int>(boxes.size() + models.size()); }
    bool isBox(int object) const { return object >= 0 && object < static_cast<int>(boxes.size()); }
    bool isModel(int object) const { return object >= static_cast<int>(boxes.size()) && object < objectCount(); }
    Model& model(int object) { return models[static_cast<std::size_t>(object) - boxes.size()]; }
    const Model& model(int object) const { return models[static_cast<std::size_t>(object) - boxes.size()]; }
};

Scene makeDefaultScene();

// Level files are text; model paths are stored relative to the level file.
bool saveScene(const Scene& scene, const std::filesystem::path& file, std::string* error = nullptr);
bool loadScene(Scene& scene, const std::filesystem::path& file, std::string* error = nullptr);

// Loads every mesh of a glTF 2.0 file (.gltf or .glb) into model.parts.
bool loadModelFile(Model& model, const std::filesystem::path& file, std::string* error = nullptr);

// Object index (see Scene::objectCount) of the first box or model hit by the ray, or -1.
int pickObject(const Scene& scene, glm::vec3 origin, glm::vec3 dir);

} // namespace engine
