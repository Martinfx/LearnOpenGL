#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/TextureCook.h"

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
    // Memory: GPU block compression (BC1/BC3, BC5 for normal maps) and a size
    // limit (longest side, 0 = original). Compressed textures take 1/4..1/8 of the memory.
    bool compress = true;
    int maxSize = 0;
};

// An image file plus its settings. `texture` is what the renderer draws; it is
// rebuilt by applyTextureSettings() so edits show up immediately.
struct TextureAsset {
    std::filesystem::path imagePath;           // absolute
    TextureSettings settings;
    int width = 0;
    int height = 0;
    TextureUsage usage = TextureUsage::Color;  // from the material slot (normal maps compress differently)
    std::vector<std::uint8_t> source;          // original RGBA8 pixels; empty when loaded from the cook cache
    std::shared_ptr<Texture> texture;
};

// Cached per path: every material using the image shares one asset. Null on error.
// Images without a settings file default to sRGB when `color` is true (albedo) and linear otherwise (normal maps...).
// A cooked copy in the disk cache is used without decoding the image (`source` stays empty).
std::shared_ptr<TextureAsset> loadTextureAsset(const std::filesystem::path& image, std::string* error = nullptr,
                                               bool color = true, TextureUsage usage = TextureUsage::Color);
// Decodes the image into `source` if it was loaded from the cache (editors need the pixels).
bool ensureTextureSource(TextureAsset& asset, std::string* error = nullptr);
// Rebuilds the texture from `source` and the settings. `cook` = also mip-map and
// compress (slower; the texture editor previews uncompressed while dragging sliders).
void applyTextureSettings(TextureAsset& asset, bool cook = true);
bool saveTextureSettings(const TextureAsset& asset, std::string* error = nullptr);
std::filesystem::path textureSettingsPath(const std::filesystem::path& image);
// Reads "<image>.texture"; `defaults` when the file does not exist.
TextureSettings readTextureSettings(const std::filesystem::path& image, const TextureSettings& defaults = {});

// How a surface uses the alpha of its albedo (texture alpha x opacity), as in glTF:
// Opaque ignores it, Mask cuts holes below the cutoff (leaves, fences, grates),
// Blend draws see-through after the opaque scene (glass), sorted back to front.
enum class AlphaMode : std::uint8_t { Opaque, Mask, Blend };
const char* alphaModeName(AlphaMode mode); // "opaque", "mask", "blend"

// Texture slots of a material. Roughness, metallic and occlusion read the red channel.
// Detail maps repeat much finer than the base maps and fade out with distance: grain of concrete,
// scratches on metal close up. DetailAlbedo is grey (0.5 = no change), DetailNormal a normal map.
enum class MapKind { Albedo, Normal, Roughness, Metallic, Occlusion, Emissive, DetailAlbedo, DetailNormal, Count };
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
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;                  // Mask: alpha below this is cut away
    float opacity = 1.0f;                      // multiplies the albedo map's alpha
    bool doubleSided = false;                  // draw back faces too (leaves, thin fences, cloth)
    float detailTiling = 8.0f;                 // detail maps repeat this many times per base map repeat
    float detailStrength = 1.0f;               // 0 = off .. 1 = full detail
    float detailDistance = 12.0f;              // m: gone beyond this distance from the camera
    // Blend layers: other materials painted onto models in the editor (vertex paint: red =
    // layer 1, green = layer 2). Their color, roughness, albedo, normal and roughness maps are used;
    // the edge follows the layer's albedo brightness (moss in the cracks first), sharper with blendSharpness.
    static constexpr int kBlendLayers = 2;
    std::array<std::filesystem::path, kBlendLayers> layerPaths; // absolute, empty = none
    std::array<std::shared_ptr<Material>, kBlendLayers> layers; // runtime
    float blendSharpness = 4.0f;               // 1 = soft gradient .. 16 = hard edge
    std::string surface;                       // footsteps / impacts / bullet holes (engine/Surfaces.h), empty = guessed
    std::array<std::filesystem::path, kMapCount> mapPaths;       // absolute, empty = none
    std::array<std::shared_ptr<TextureAsset>, kMapCount> maps;   // runtime, loaded from mapPaths
    // Material instance (`parent "base.material"`): every value not in `overrides` (keys of the
    // .material file, e.g. "color", "normal_map") comes from the parent and follows its edits.
    std::filesystem::path parentPath;         // absolute, empty = a standalone material
    std::shared_ptr<Material> parent;         // runtime, loaded from parentPath
    std::set<std::string> overrides;

    const std::shared_ptr<TextureAsset>& map(MapKind kind) const { return maps[static_cast<std::size_t>(kind)]; }
    const std::filesystem::path& mapPath(MapKind kind) const { return mapPaths[static_cast<std::size_t>(kind)]; }
};

// Cached per path; the editor changes the returned material in place and the
// renderer picks the change up on the next frame. Null on error.
std::shared_ptr<Material> loadMaterial(const std::filesystem::path& file, std::string* error = nullptr);
bool saveMaterial(const Material& material, const std::filesystem::path& file, std::string* error = nullptr);
// Sets one texture slot and loads the image (empty path clears it).
bool setMaterialMap(Material& material, MapKind kind, const std::filesystem::path& image, std::string* error = nullptr);
// Sets blend layer `layer` (0 or 1) to another material (empty path clears it).
bool setMaterialLayer(Material& material, int layer, const std::filesystem::path& file, std::string* error = nullptr);
const char* layerKey(int layer); // "layer1_material", "layer2_material"
// Re-reads a material file into the cached object (e.g. "Revert" in the editor).
bool reloadMaterial(Material& material, std::string* error = nullptr);

// Material instances. Keys of the values a material file can set (color, roughness, ...,
// the map keys), the keys whose values differ between two materials, copying one value.
const std::vector<std::string>& materialValueKeys();
std::vector<std::string> materialDifferences(const Material& a, const Material& b);
void copyMaterialValue(Material& to, const Material& from, const std::string& key);
// Values not overridden come from the parent again (after the parent was edited in place).
void applyMaterialParent(Material& material);
// Every loaded instance takes its parent's current values (the editor calls it after an edit).
void refreshMaterialInstances();
// Makes `material` an instance of `parentFile` with no overrides (its values become the parent's),
// or with an empty path a standalone material that keeps its current values.
bool setMaterialParent(Material& material, const std::filesystem::path& parentFile, std::string* error = nullptr);

// Hot reload (engine/HotReload.h): the cached objects in use, or null when nothing uses the file.
std::shared_ptr<TextureAsset> findTextureAsset(const std::filesystem::path& image);
std::shared_ptr<Material> findMaterial(const std::filesystem::path& file);
// Decodes the changed image again into the cached asset (every material using it updates). False if not in use.
bool reloadTexture(const std::filesystem::path& image, std::string* error = nullptr);

} // namespace engine
