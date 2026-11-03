#pragma once
#include "engine/Scene.h"
#include "engine/TextureCook.h"

#include <glm/vec2.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine {

class Renderer;

// The level's map for the PDA and the minimap: the level rendered from straight above
// (orthographic, full renderer: shadows, vegetation, ambient occlusion), styled like a
// worn aerial photo (height shading, dark outlines along walls). Baked in tiles of ~48 m
// so distance culling and LOD treat every tile as seen from close by, one tile per call
// of step() so the editor and the loading screen stay responsive; the finished image is
// block-compressed and kept in the cook cache (<cache>/maps), keyed by the level's
// content and the bake settings (MapSettings), so it is rendered once per change.

// The area the map shows (world X, Z of the top-left (north-west) corner and the size in
// metres along X and Z). MapSettings::size > 0 sets it; otherwise it is fitted around
// the solid objects (collision), characters and the player start, so distant scenery
// (skylines, far trees) does not shrink the playable part to a dot.
struct MapArea {
    glm::vec2 min{0.0f};
    glm::vec2 size{100.0f};
};
MapArea mapAreaOf(const Scene& scene);

// Hash of everything the picture depends on (objects, terrain, area, settings).
std::uint64_t mapBakeHash(const Scene& scene);
// <cook cache>/maps/<level stem>_<hash>.ctex
std::filesystem::path mapCachePath(const std::filesystem::path& levelFile, const Scene& scene);

// A finished map: RGBA8 rows from the top (north), sRGB display colours.
struct MapImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    MapArea area;
};

// The cached file: block-compressed mip chain plus the area it covers.
bool writeMapCache(const std::filesystem::path& file, const CookedTexture& texture, const MapArea& area);
bool readMapCache(const std::filesystem::path& file, CookedTexture& texture, MapArea& area);
// GL texture from a cooked map (needs the GL context); 0 on failure. Row 0 = north.
unsigned uploadMapTexture(const CookedTexture& texture);
CookedTexture cookMapImage(const MapImage& image); // BC1 + mips (job system)

// Bakes the map with the renderer of the current GL context. Call begin(), then step()
// until it returns true (each call renders at least one tile and stops after budgetMs),
// then finish() (CPU: grading and outlines; run it on a worker if you can).
// The renderer's quality and streaming settings are restored after every step.
class MapBaker {
public:
    void begin(const Scene& scene);
    bool step(Renderer& renderer, const Scene& scene, double budgetMs = 30.0);
    float progress() const { return tiles_ > 0 ? static_cast<float>(done_) / static_cast<float>(tiles_) : 0.0f; }
    bool done() const { return tiles_ > 0 && done_ >= tiles_; }
    MapImage finish() const; // valid after step() returned true
    const MapArea& area() const { return area_; }

private:
    void renderTile(Renderer& renderer, const Scene& scene, int tx, int tz);

    MapSettings settings_;
    MapArea area_;
    float metresPerPixel_ = 0.25f;
    int tilePixels_ = 256, pad_ = 24;
    int tilesX_ = 0, tilesZ_ = 0, tiles_ = 0, done_ = 0;
    int width_ = 0, height_ = 0;
    float top_ = 50.0f, bottom_ = -10.0f; // world heights of the camera and the far plane
    Environment environment_;
    glm::vec3 sun_{0.0f, -1.0f, 0.0f};
    std::vector<std::uint8_t> color_; // RGB rows from the top
    std::vector<float> height_m_;     // world height per pixel (from the depth buffer)
    unsigned fbo_ = 0, colorTex_ = 0, depthRb_ = 0;
    int fboSize_ = 0;
};

} // namespace engine
