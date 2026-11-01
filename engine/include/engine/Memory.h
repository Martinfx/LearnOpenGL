#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <vector>

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

// Memory budget of a level by category (level line `memory_budget`, editor Memory
// dock): the game warns once a category goes over its limit, the editor shows it red.
enum class MemoryCategory { GpuTextures, GpuMeshes, CpuMeshes, Audio, Physics, Lua, Particles, Count };
constexpr int kMemoryCategories = static_cast<int>(MemoryCategory::Count);
const char* memoryCategoryName(MemoryCategory c);   // "GPU textures"...
const char* memoryCategoryKey(MemoryCategory c);    // "gpu_textures"... (console)

struct MemoryBudget {
    // MB per category, 0 = no limit. Defaults fit a 4 GB GPU / 8 GB PC with room to spare.
    std::array<float, kMemoryCategories> mb{1536.0f, 512.0f, 1024.0f, 256.0f, 256.0f, 64.0f, 32.0f};
    bool operator==(const MemoryBudget&) const = default;
    float limit(MemoryCategory c) const { return mb[static_cast<std::size_t>(c)]; }
};

// What is in use now, in bytes per category (filled by the game or the editor; -1 = not measured).
struct MemoryUsage {
    std::array<long long, kMemoryCategories> bytes{-1, -1, -1, -1, -1, -1, -1};
    void set(MemoryCategory c, std::size_t b) { bytes[static_cast<std::size_t>(c)] = static_cast<long long>(b); }
};

struct MemoryOver {
    MemoryCategory category;
    double usedMB = 0.0, limitMB = 0.0;
};
std::vector<MemoryOver> overBudget(const MemoryUsage& usage, const MemoryBudget& budget);

} // namespace engine

