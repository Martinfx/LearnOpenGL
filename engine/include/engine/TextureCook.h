#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine {

// GPU block compression of textures (S3TC / RGTC, supported by every desktop
// OpenGL 4.x driver). 4x4 pixel blocks: BC1 = 8 bytes (RGB, 1/8 of RGBA8),
// BC3 = 16 bytes (RGBA, 1/4), BC5 = 16 bytes (two channels, normal maps).
enum class TextureFormat : std::uint8_t { RGBA8, BC1, BC3, BC5 };

// What the pixels mean: decides the format and how mipmaps are averaged.
enum class TextureUsage : std::uint8_t {
    Color,  // albedo, emissive: BC1, or BC3 with alpha; sRGB-correct mipmaps
    Normal, // tangent-space normal map: BC5 (X, Y); the shader rebuilds Z
    Data,   // roughness, metallic, occlusion masks: BC1/BC3, linear
};

struct TextureMip {
    std::size_t offset = 0; // into the data block
    std::size_t size = 0;   // bytes
    int width = 0;
    int height = 0;
};

// A mip chain ready for glCompressedTextureSubImage2D (or RGBA8 when not compressed).
struct CookedTexture {
    TextureFormat format = TextureFormat::RGBA8;
    int width = 0;  // of mip 0
    int height = 0;
    std::vector<TextureMip> mips;
    std::vector<std::uint8_t> data;
};

struct CookOptions {
    TextureUsage usage = TextureUsage::Color;
    bool srgb = true;      // Color textures: average mipmaps in linear light
    bool compress = true;  // false = RGBA8 mip chain
    bool mipmaps = true;
    int maxSize = 0;       // longest side limit, halved until it fits (0 = none)
    // Alpha-tested (MASK) textures: alpha of every mip level is rescaled so the share of
    // pixels above this cutoff stays as in the full-size image; thin grass and leaves
    // then do not fade away in the distance (0 = plain averaging).
    float alphaCoverage = 0.0f;
};

// Process-wide switches. Compression is on by default; the renderer turns it
// off when the GPU lacks S3TC/RGTC. The cache folder defaults to the user's
// cache directory (~/.cache/fps_engine/textures, %LOCALAPPDATA%\fps_engine\...).
struct TextureCookConfig {
    bool compress = true;
    bool diskCache = true;
    std::filesystem::path cacheDir;
    // Size limit of the cooked files (textures and meshes); 0 = none. Every edit of a source
    // cooks a new file and nothing removed the old ones: the cache grew to 12 GB.
    std::uint64_t cacheLimitMb = 4096;
};
TextureCookConfig& textureCookConfig();
// Marks a cooked file as just used (its time decides what pruneCookCache keeps).
void touchCookedFile(const std::filesystem::path& file);
// Deletes the longest unused cooked textures and meshes until they take 3/4 of
// cacheLimitMb. Runs on a background thread (a scan of thousands of files); returns at once.
void pruneCookCache();
// The pruning itself (synchronous): oldest first down to 3/4 of `limitBytes`, never a file
// touched within the last hour. Returns how many files it deleted.
int pruneCookedFiles(const std::vector<std::filesystem::path>& dirs, std::uint64_t limitBytes);
// Deletes the cooked textures and meshes (MeshCache.h) at the next start of the editor or
// game: running processes still stream from the files, so they are not deleted now.
void requestCookCacheClear();

// Downscales to options.maxSize, builds the mip chain and block-compresses it.
// Big images are split over the job system.
CookedTexture cookTexture(const std::uint8_t* rgba, int width, int height, const CookOptions& options);

std::size_t textureFormatBlockBytes(TextureFormat format); // 0 for RGBA8
std::size_t textureLevelBytes(TextureFormat format, int width, int height);
const char* textureFormatName(TextureFormat format);

// Disk cache. `key` describes everything the cooked result depends on (source
// file, its size and modification time, options); the file name is its hash.
std::string textureCacheKey(const std::filesystem::path& source, const std::string& extra, const CookOptions& options);
std::filesystem::path textureCachePath(const std::string& key);
bool writeCookedTexture(const std::filesystem::path& file, const CookedTexture& texture);
// Reads mips [firstMip, end) only (smaller mips for streaming); mip offsets are
// rebased to the returned data. False if missing or damaged.
bool readCookedTexture(const std::filesystem::path& file, CookedTexture& out, int firstMip = 0);
// Header only: format, size and mip table, no pixels.
bool readCookedTextureInfo(const std::filesystem::path& file, CookedTexture& out);

} // namespace engine
