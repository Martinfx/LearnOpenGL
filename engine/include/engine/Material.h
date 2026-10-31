#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <vector>

namespace engine {

struct Texture;

// How an image is sampled and adjusted. Stored next to the image as
// "<image>.texture" (e.g. bricks.png.texture); the image file itself is never changed.
struct TextureSettings {
    enum class Wrap { Repeat, Clamp, Mirror };
    Wrap wrap = Wrap::Repeat;
    bool nearest = false;     // pixel-art look instead of smooth filtering
    bool mipmaps = true;
    bool srgb = true;         // color texture (false for data such as masks)
    bool flipY = false;
    float brightness = 0.0f;  // -1..1, added
    float contrast = 1.0f;    // 0..3, around mid grey
    float saturation = 1.0f;  // 0 = grey, 1 = unchanged
    glm::vec3 tint{1.0f};     // multiplied (sRGB 0..1)
};

// An image file plus its settings. `texture` is what the renderer draws; it is
// rebuilt by applyTextureSettings() so edits show up immediately.
struct TextureAsset {
    std::filesystem::path imagePath;           // absolute
    TextureSettings settings;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> source;          // original RGBA8 pixels
    std::shared_ptr<Texture> texture;
};

// Cached per path: every material using the image shares one asset. Null on error.
// Images without a settings file default to sRGB when `color` is true (albedo) and linear otherwise (normal maps...).
std::shared_ptr<TextureAsset> loadTextureAsset(const std::filesystem::path& image, std::string* error = nullptr,
                                               bool color = true);
void applyTextureSettings(TextureAsset& asset);
bool saveTextureSettings(const TextureAsset& asset, std::string* error = nullptr);
std::filesystem::path textureSettingsPath(const std::filesystem::path& image);
// Reads "<image>.texture"; `defaults` when the file does not exist.
TextureSettings readTextureSettings(const std::filesystem::path& image, const TextureSettings& defaults = {});

// Texture slots of a material. Roughness, metallic and occlusion read the red channel.
enum class MapKind { Albedo, Normal, Roughness, Metallic, Occlusion, Emissive, Count };
constexpr int kMapCount = static_cast<int>(MapKind::Count);
const char* mapKey(MapKind kind);   // key in .material files, e.g. "normal_map"
const char* mapLabel(MapKind kind); // for the editor, e.g. "Normal map"
bool mapIsColor(MapKind kind);      // albedo/emissive are sRGB, the rest are data

// Surface look (physically based, metal/roughness), stored as a text .material
// file and edited in the material editor.
struct Material {
    std::string name = "Material";
    std::filesystem::path sourcePath;          // absolute .material path
    glm::vec3 color{1.0f};                     // sRGB, multiplies the albedo map
    float roughness = 0.7f;                    // 0 = mirror, 1 = matte; multiplies the roughness map
    float metallic = 0.0f;                     // 0 = dielectric, 1 = metal; multiplies the metallic map
    float normalStrength = 1.0f;               // bumpiness of the normal map
    glm::vec3 emissive{0.0f};                  // glow added regardless of light (sRGB), multiplies the emissive map
    glm::vec2 tiling{1.0f};                    // texture repeats per UV unit (per metre with world UVs)
    bool worldUv = false;                      // project textures in world space (boxes always do)
    std::array<std::filesystem::path, kMapCount> mapPaths;       // absolute, empty = none
    std::array<std::shared_ptr<TextureAsset>, kMapCount> maps;   // runtime, loaded from mapPaths

    const std::shared_ptr<TextureAsset>& map(MapKind kind) const { return maps[static_cast<std::size_t>(kind)]; }
    const std::filesystem::path& mapPath(MapKind kind) const { return mapPaths[static_cast<std::size_t>(kind)]; }
};

// Cached per path; the editor changes the returned material in place and the
// renderer picks the change up on the next frame. Null on error.
std::shared_ptr<Material> loadMaterial(const std::filesystem::path& file, std::string* error = nullptr);
bool saveMaterial(const Material& material, const std::filesystem::path& file, std::string* error = nullptr);
// Sets one texture slot and loads the image (empty path clears it).
bool setMaterialMap(Material& material, MapKind kind, const std::filesystem::path& image, std::string* error = nullptr);
// Re-reads a material file into the cached object (e.g. "Revert" in the editor).
bool reloadMaterial(Material& material, std::string* error = nullptr);

} // namespace engine
