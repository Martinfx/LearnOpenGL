#pragma once
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine {

struct Scene;

// Baked lighting probes of a level (the editor bakes them into <level>.probes):
//  - reflection probes (entity "reflection_probe"): a cubemap captured at the middle of the
//    box, used for reflections of surfaces inside it (box projected, blends over `fade`);
//  - irradiance volumes (entity "irradiance_volume"): a grid of points with the light arriving
//    from all directions (spherical harmonics L1), the diffuse light inside buildings (light
//    from windows and doors instead of the dimmed sky).
// The baked data keeps the shape it was baked with: moving an entity needs a new bake.

struct ProbeShape {
    std::string name;            // entity name (bake input)
    glm::vec3 center{0.0f};      // middle of the box
    glm::vec3 half{1.0f};        // half size along its own axes
    float yawDegrees = 0.0f;     // turned like the entity (0 = +X)
    float fade = 0.5f;           // m inside the box over which it blends in
};

struct ReflectionProbe {
    ProbeShape shape;
    float intensity = 1.0f;          // artistic multiplier at runtime (not baked in)
    bool parallax = true;            // box projection (rooms); off = infinitely far (open areas)
    int size = 128;                  // face size in pixels
    std::vector<std::uint32_t> faces; // 6 * size * size, RGB9E5 (GL_UNSIGNED_INT_5_9_9_9_REV), GL face order
};

struct IrradianceVolume {
    ProbeShape shape;
    float intensity = 1.0f;          // artistic multiplier at runtime (not baked in)
    glm::ivec3 dims{2};              // grid points along the box's own axes
    // Per point (x fastest, then y, then z): 3 vec4 = SH L1 for r, g, b, pre-scaled so that
    // radiance(n) = c.x + c.y * n.y + c.z * n.z + c.w * n.x (cosine-convolved, divided by pi).
    std::vector<glm::vec4> sh;
};

struct ProbeSet {
    std::vector<ReflectionProbe> reflections;
    std::vector<IrradianceVolume> volumes;
};

// Shapes from the scene's entities (data empty, to be baked).
std::vector<ReflectionProbe> reflectionProbesToBake(const Scene& scene);
std::vector<IrradianceVolume> irradianceVolumesToBake(const Scene& scene);
// World position of a grid point of a volume.
glm::vec3 irradiancePoint(const IrradianceVolume& volume, glm::ivec3 cell);

// Radiance cubemap (6 faces of size*size RGB floats, GL face order, rows from t = 0) -> the
// volume's per-point coefficients (3 vec4, see IrradianceVolume::sh).
void projectIrradiance(const std::vector<float>& faces, int size, glm::vec4 out[3]);
glm::vec3 evalIrradiance(const glm::vec4 sh[3], glm::vec3 n);
// GL cube face texel -> direction (unnormalized), s/t in 0..1.
glm::vec3 cubeTexelDirection(int face, float s, float t);

std::filesystem::path probeFile(const std::filesystem::path& level); // level.probes
bool saveProbes(const ProbeSet& probes, const std::filesystem::path& file, std::string* error = nullptr);
bool loadProbes(ProbeSet& probes, const std::filesystem::path& file, std::string* error = nullptr);

} // namespace engine
