#pragma once
#include "engine/Geometry.h"
#include <filesystem>
#include <string>
#include <vector>

namespace engine {

// Milestone 1 level: axis-aligned boxes. Will be replaced by an ECS + glTF
// meshes in later milestones; the file format is versioned for that reason.
struct Box {
    std::string name = "Box";
    glm::vec3 position{0.0f};       // center
    glm::vec3 size{1.0f};
    glm::vec3 color{0.7f, 0.7f, 0.7f};

    Aabb bounds() const { return Aabb::fromCenterSize(position, size); }
};

struct Scene {
    std::vector<Box> boxes;
    glm::vec3 playerStart{0.0f, 0.0f, 0.0f}; // feet position
    glm::vec3 sunDirection{-0.4f, -1.0f, -0.3f};
};

Scene makeDefaultScene();

bool saveScene(const Scene& scene, const std::filesystem::path& file, std::string* error = nullptr);
bool loadScene(Scene& scene, const std::filesystem::path& file, std::string* error = nullptr);

// Index of the first box hit by the ray, or -1.
int pickBox(const Scene& scene, glm::vec3 origin, glm::vec3 dir);

} // namespace engine
