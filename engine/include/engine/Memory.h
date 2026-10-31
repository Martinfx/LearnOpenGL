#pragma once
#include <cstddef>

namespace engine {

struct Scene;

// CPU memory held by a level's assets. Shared data (mesh parts of model copies,
// textures used by several materials) is counted once.
struct SceneMemory {
    std::size_t meshBytes = 0;      // vertices and indices
    std::size_t textureBytes = 0;   // pixels still in RAM (released after GPU upload)
    std::size_t sharedMeshBytes = 0; // what copies of models would cost without sharing
    int meshParts = 0;
    int textures = 0;
};
SceneMemory sceneMemory(const Scene& scene);

// Resident memory of this process in bytes (0 if the platform does not tell).
std::size_t processMemoryBytes();

} // namespace engine
