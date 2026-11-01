#pragma once
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine {

// Albedo palette check: physically plausible base colors stay between about 30 and
// 240 sRGB (charcoal .. fresh snow) and the zone's look is muted, so strongly
// saturated albedo stands out. Used by the editor's Assets > Check albedo palette.
struct PaletteLimits {
    int darkest = 30;            // sRGB: pixels whose brightest channel is below are "too dark"
    int brightest = 240;         // sRGB: pixels whose darkest channel is above are "too bright"
    float maxShare = 0.05f;      // more than this share of pixels out of range is a problem
    float maxSaturation = 0.35f; // mean chroma (max - min channel, 0..1) above this is a problem
};

struct AlbedoStats {
    int pixels = 0;              // samples counted (alpha >= 50 %)
    float tooDark = 0.0f;        // share of samples
    float tooBright = 0.0f;
    float meanLuminance = 0.0f;  // sRGB 0..255 (Rec. 709 weights)
    float meanSaturation = 0.0f; // chroma 0..1: (max - min channel) / 255; HSV saturation would call every dark brown "saturated"
};

// Samples at most ~128 x 128 pixels of an RGBA8 image multiplied by `tint` (sRGB color,
// applied in linear space like the renderer). `tint` alone when `rgba` is null.
AlbedoStats albedoStats(const std::uint8_t* rgba, int width, int height, glm::vec3 tint = glm::vec3(1.0f),
                        const PaletteLimits& limits = {});
std::vector<std::string> paletteProblems(const AlbedoStats& stats, const PaletteLimits& limits = {});

struct PaletteEntry {
    std::filesystem::path asset; // .material, .gltf or .glb
    std::string part;            // glTF material name; empty for .material files
    AlbedoStats stats;
    std::vector<std::string> problems;
};
// A .material (albedo map x color) or a glTF model (every material's base color
// texture x factor). Unreadable files give one entry with the error as problem.
std::vector<PaletteEntry> checkAssetPalette(const std::filesystem::path& file, const PaletteLimits& limits = {});

} // namespace engine
