#pragma once
#include "engine/Geometry.h"
#include "engine/Material.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/ext/vector_int2.hpp>
#include <glm/vec3.hpp>

namespace engine {

struct Model;

// Ground texture of the terrain: an engine material repeated `tiling` times per metre.
struct TerrainLayer {
    std::filesystem::path materialPath; // absolute .material, empty = unused
    std::shared_ptr<Material> material; // runtime
    float tiling = 0.25f;               // repeats per metre (0.25 = one image every 4 m)
};

// A plant or prop painted with the foliage brush; drawn with instancing.
struct FoliageType {
    static constexpr int kLayerSlots = 8; // = Terrain::kLayers
    std::filesystem::path modelPath;    // absolute .glb/.gltf
    std::shared_ptr<const Model> model; // runtime
    float density = 0.5f;               // instances per square metre the brush aims for
    float minScale = 0.8f, maxScale = 1.2f;
    float cullDistance = 60.0f;         // not drawn farther (0 = always)
    bool collision = false;             // trees: on; grass and weeds: off
    bool alignToSlope = false;          // tilt with the ground (grass) or stay upright (trees)
    bool castShadows = true;            // grass: off (much cheaper shadow maps)
    // Simpler models by distance (level lines `foliage_lod "file" distance` after the type).
    struct Lod {
        std::filesystem::path modelPath;
        std::shared_ptr<const Model> model;
        float distance = 30.0f;
    };
    std::vector<Lod> lods;
    // Fraction left at the cull distance (thinning from 30 % of it on; 1 = no thinning).
    // Low ground cover without collision also thins by the quality preset (detailDensityFar).
    float farDensity = 1.0f;
    // Bending in the level's wind: 1 grass, ~0.3 trees, 0 rigid (rocks, wrecks).
    float sway = 1.0f;
    // Dithered fade over this share of the cull distance before it (0 = pops out).
    float fade = 0.15f;
    // How much of `density` grows on each terrain layer (grass on layer 0, none on asphalt).
    // Brush and "Fill" multiply density by the weighted sum at the spot, clamped to 0..1.
    std::array<float, kLayerSlots> layerDensity{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
};

struct FoliageInstance {
    glm::vec3 position{0.0f};
    float yawDegrees = 0.0f;
    float scale = 1.0f;
    float tiltX = 0.0f, tiltZ = 0.0f;   // slope alignment (radians)
    std::uint16_t type = 0;
};

// Heightfield ground with up to eight blended texture layers (splat map: one
// weight byte per layer and pixel) and painted foliage. Sculpted and painted with
// brushes in the editor (Landscape and Foliage modes). Stored next to the level
// as "<level>.terrain" (binary: heights, splat map, foliage instances); layers
// and foliage types are level lines so they stay readable and diffable.
struct Terrain {
    static constexpr int kLayers = 8;

    glm::vec2 origin{-128.0f, -128.0f}; // min X/Z corner, metres
    float size = 256.0f;                // square side, metres
    int resolution = 257;               // height samples per side
    std::vector<float> heights;         // resolution * resolution, row = Z
    int splatResolution = 512;          // splat map pixels per side
    std::vector<std::uint8_t> splat;    // kLayers weight bytes per pixel (layers 0..7)
    std::array<TerrainLayer, kLayers> layers;
    std::vector<FoliageType> foliageTypes;
    std::vector<FoliageInstance> foliage;

    // Change counters for the renderer and physics (new values on every edit).
    std::uint64_t meshVersion = 0, splatVersion = 0, foliageVersion = 0;
    // Recent height edits (sample rectangles, inclusive): the renderer re-uploads just these
    // tiles when it saw the version before them; anything else rebuilds everything.
    struct MeshEdit {
        std::uint64_t before = 0, after = 0;
        int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    };
    std::vector<MeshEdit> meshEdits; // oldest first, at most 32

    // Flat ground at height 0, layer 0 everywhere.
    void create(glm::vec2 origin, float size, int resolution, int splatResolution = 512);
    bool valid() const { return resolution >= 2 && heights.size() == static_cast<std::size_t>(resolution) * resolution; }
    float spacing() const { return size / static_cast<float>(resolution - 1); }
    bool contains(float x, float z) const;
    float heightAt(float x, float z) const;  // bilinear; 0 outside
    glm::vec3 normalAt(float x, float z) const;
    Aabb bounds() const;
    // First hit of a ray with the height field (marching + refinement), metres along `dir`.
    bool raycast(glm::vec3 origin, glm::vec3 dir, float maxDistance, float& t) const;

    // Brushes. `strength` 0..1 per stroke step; smooth falloff to the radius.
    enum class Sculpt { Raise, Lower, Smooth, Flatten };
    void sculpt(glm::vec3 center, float radius, float strength, Sculpt mode, float flattenHeight = 0.0f);
    void paintLayer(glm::vec3 center, float radius, float strength, int layer);
    // Grows or crops the terrain to `newSize` metres keeping the sample spacing (and the
    // splat texel size); `anchor` (-1, 0, 1 per axis) is the side that stays put, like a
    // canvas resize: (-1, -1) adds the new ground toward +X and +Z. New ground continues
    // the edge heights with layer 1; foliage outside is dropped. False if it would exceed
    // 4097 samples per side.
    bool resize(float newSize, glm::ivec2 anchor);
    // Adds instances of `type` up to its density inside the circle (keeps spacing); returns how many.
    int paintFoliage(glm::vec3 center, float radius, int type, std::mt19937& rng);
    int eraseFoliage(glm::vec3 center, float radius, int type = -1); // -1 = every type
    // 0..1: share of the type's density at the spot (its layerDensity by the splat weights).
    float foliageChance(const FoliageType& type, float x, float z) const;
    // Instances of `type` over the whole terrain by density and layers (jittered grid,
    // deterministic for a seed). Const: run it on a worker with a terrain snapshot.
    std::vector<FoliageInstance> scatterFoliage(int type, std::uint32_t seed) const;
    void removeFoliageType(int type);
    void snapFoliageToGround();                                       // after sculpting

    float layerWeight(int layer, float x, float z) const;             // 0..1, for tests and tools
};

// Binary file next to the level (heights, splat map, foliage instances).
bool saveTerrainData(const Terrain& terrain, const std::filesystem::path& file, std::string* error = nullptr);
bool loadTerrainData(Terrain& terrain, const std::filesystem::path& file, std::string* error = nullptr);

} // namespace engine
