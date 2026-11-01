#include "engine/Material.h"

#include <mutex>
#include "engine/Scene.h"

#include <stb_image.h> // implementation lives in GltfLoader.cpp

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace engine {

namespace {

std::filesystem::path fromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string toUtf8(const std::filesystem::path& p)
{
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

void setError(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
}

std::filesystem::path normalized(const std::filesystem::path& p)
{
    return std::filesystem::absolute(p).lexically_normal();
}

bool readFile(const std::filesystem::path& file, std::vector<unsigned char>& data)
{
    const std::u8string u8 = file.u8string();
    FILE* f = std::fopen(std::string(u8.begin(), u8.end()).c_str(), "rb");
#ifdef _WIN32
    if (!f)
        f = _wfopen(file.wstring().c_str(), L"rb");
#endif
    if (!f)
        return false;
    unsigned char buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

const char* wrapName(TextureSettings::Wrap w)
{
    switch (w) {
    case TextureSettings::Wrap::Clamp: return "clamp";
    case TextureSettings::Wrap::Mirror: return "mirror";
    default: return "repeat";
    }
}

std::filesystem::path relativeTo(const std::filesystem::path& target, const std::filesystem::path& dir)
{
    const std::filesystem::path rel = normalized(target).lexically_relative(dir);
    return rel.empty() ? normalized(target) : rel;
}

} // namespace

std::filesystem::path textureSettingsPath(const std::filesystem::path& image)
{
    std::filesystem::path p = image;
    p += ".texture";
    return p;
}

TextureSettings readTextureSettings(const std::filesystem::path& image, const TextureSettings& defaults)
{
    TextureSettings s = defaults;
    std::ifstream in(textureSettingsPath(image));
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string k;
        if (!(ls >> k) || k[0] == '#')
            continue;
        if (k == "wrap") {
            std::string v;
            ls >> v;
            s.wrap = v == "clamp" ? TextureSettings::Wrap::Clamp
                   : v == "mirror" ? TextureSettings::Wrap::Mirror
                                   : TextureSettings::Wrap::Repeat;
        } else if (k == "filter") {
            std::string v;
            ls >> v;
            s.nearest = v == "nearest";
        } else if (k == "mipmaps") {
            ls >> s.mipmaps;
        } else if (k == "srgb") {
            ls >> s.srgb;
        } else if (k == "flip_y") {
            ls >> s.flipY;
        } else if (k == "brightness") {
            ls >> s.brightness;
        } else if (k == "contrast") {
            ls >> s.contrast;
        } else if (k == "saturation") {
            ls >> s.saturation;
        } else if (k == "tint") {
            ls >> s.tint.r >> s.tint.g >> s.tint.b;
        } else if (k == "compress") {
            ls >> s.compress;
        } else if (k == "max_size") {
            ls >> s.maxSize;
            s.maxSize = std::max(0, s.maxSize);
        }
    }
    return s;
}

namespace {
// Asset caches are shared by the editor's background loaders and the UI thread.
std::recursive_mutex& cacheMutex()
{
    static std::recursive_mutex m;
    return m;
}
} // namespace

namespace {

CookOptions cookOptions(const TextureAsset& asset)
{
    const TextureSettings& s = asset.settings;
    CookOptions o;
    o.usage = asset.usage == TextureUsage::Normal ? TextureUsage::Normal : (s.srgb ? TextureUsage::Color : TextureUsage::Data);
    o.srgb = s.srgb;
    // Pixel-art (nearest) textures stay sharp: block compression would smear them.
    o.compress = textureCookConfig().compress && s.compress && !s.nearest;
    o.mipmaps = s.mipmaps;
    o.maxSize = s.maxSize;
    return o;
}

// Everything that changes the cooked pixels, for the cache key.
std::string settingsKey(const TextureSettings& s)
{
    std::ostringstream k;
    k << s.flipY << ' ' << s.brightness << ' ' << s.contrast << ' ' << s.saturation << ' ' << s.tint.r << ' ' << s.tint.g << ' '
      << s.tint.b;
    return k.str();
}

void applySampling(const TextureSettings& s, Texture& t)
{
    t.srgb = s.srgb;
    t.nearest = s.nearest;
    t.mipmaps = s.mipmaps;
    t.wrap = static_cast<int>(s.wrap);
}

bool decodeImage(const std::filesystem::path& file, TextureAsset& asset, std::string* error)
{
    std::vector<unsigned char> bytes;
    if (!readFile(file, bytes)) {
        setError(error, "Cannot open " + file.string());
        return false;
    }
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp, 4);
    if (!pixels) {
        setError(error, file.string() + ": unsupported image (PNG or JPEG expected)");
        return false;
    }
    asset.width = w;
    asset.height = h;
    asset.source.assign(pixels, pixels + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(pixels);
    return true;
}

} // namespace

std::map<std::filesystem::path, std::weak_ptr<TextureAsset>>& textureCache()
{
    static std::map<std::filesystem::path, std::weak_ptr<TextureAsset>> cache;
    return cache;
}

std::map<std::filesystem::path, std::weak_ptr<Material>>& materialCache()
{
    static std::map<std::filesystem::path, std::weak_ptr<Material>> cache;
    return cache;
}

std::shared_ptr<TextureAsset> loadTextureAsset(const std::filesystem::path& image, std::string* error, bool color,
                                               TextureUsage usage)
{
    const std::lock_guard lock(cacheMutex());
    auto& cache = textureCache();
    const std::filesystem::path key = normalized(image);
    if (auto it = cache.find(key); it != cache.end())
        if (auto shared = it->second.lock())
            return shared;

    if (!std::filesystem::exists(key)) {
        setError(error, "Cannot open " + key.string());
        return nullptr;
    }
    auto asset = std::make_shared<TextureAsset>();
    asset->imagePath = key;
    asset->usage = usage;
    TextureSettings defaults;
    defaults.srgb = color;
    asset->settings = readTextureSettings(key, defaults);

    // Cooked copy in the disk cache: no decode, no compression.
    const TextureCookConfig& config = textureCookConfig();
    if (config.diskCache) {
        const CookOptions options = cookOptions(*asset);
        const std::filesystem::path file = textureCachePath(textureCacheKey(key, settingsKey(asset->settings), options));
        CookedTexture cooked;
        if (readCookedTextureInfo(file, cooked)) { // pixels are read by the renderer when needed
            asset->width = cooked.width;
            asset->height = cooked.height;
            asset->texture = std::make_shared<Texture>();
            asset->texture->id = newResourceId();
            applySampling(asset->settings, *asset->texture);
            asset->texture->setCooked(std::move(cooked), file);
            asset->texture->mipmaps = asset->settings.mipmaps;
            cache[key] = asset;
            return asset;
        }
    }
    if (!decodeImage(key, *asset, error))
        return nullptr;
    asset->texture = std::make_shared<Texture>();
    applyTextureSettings(*asset);
    cache[key] = asset;
    return asset;
}

bool ensureTextureSource(TextureAsset& asset, std::string* error)
{
    if (!asset.source.empty())
        return true;
    return decodeImage(asset.imagePath, asset, error);
}

void applyTextureSettings(TextureAsset& asset, bool cook)
{
    if (asset.source.empty() && !ensureTextureSource(asset))
        return;
    const TextureSettings& s = asset.settings;
    if (!asset.texture)
        asset.texture = std::make_shared<Texture>();
    Texture& t = *asset.texture;
    t.id = newResourceId(); // the renderer uploads the new pixels and frees the old ones
    applySampling(s, t);
    std::vector<std::uint8_t> rgba(asset.source.size());
    const bool identity = s.brightness == 0.0f && s.contrast == 1.0f && s.saturation == 1.0f && s.tint == glm::vec3(1.0f);
    const std::size_t rowBytes = static_cast<std::size_t>(asset.width) * 4;
    for (int y = 0; y < asset.height; ++y) {
        const int srcY = s.flipY ? asset.height - 1 - y : y;
        const std::uint8_t* src = asset.source.data() + static_cast<std::size_t>(srcY) * rowBytes;
        std::uint8_t* dst = rgba.data() + static_cast<std::size_t>(y) * rowBytes;
        if (identity) {
            std::copy(src, src + rowBytes, dst);
            continue;
        }
        for (int x = 0; x < asset.width; ++x) {
            float c[3];
            for (int k = 0; k < 3; ++k)
                c[k] = src[x * 4 + k] / 255.0f;
            const float grey = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
            for (int k = 0; k < 3; ++k) {
                float v = grey + (c[k] - grey) * s.saturation;
                v = (v - 0.5f) * s.contrast + 0.5f + s.brightness;
                v *= s.tint[k];
                dst[x * 4 + k] = static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    if (cook) {
        const CookOptions options = cookOptions(asset);
        CookedTexture cooked = cookTexture(rgba.data(), asset.width, asset.height, options);
        std::filesystem::path file;
        if (textureCookConfig().diskCache) {
            file = textureCachePath(textureCacheKey(asset.imagePath, settingsKey(s), options));
            if (!writeCookedTexture(file, cooked))
                file.clear();
        }
        t.setCooked(std::move(cooked), file);
        t.mipmaps = s.mipmaps;
    } else {
        t.setCooked({}, {});
        t.width = asset.width;
        t.height = asset.height;
        t.format = TextureFormat::RGBA8;
        t.mipCount = 1;
        t.mipmaps = s.mipmaps;
    }
    t.rgba = std::move(rgba); // full-size pixels for previews (the renderer may drop them after upload)
}

const char* alphaModeName(AlphaMode mode)
{
    switch (mode) {
    case AlphaMode::Mask: return "mask";
    case AlphaMode::Blend: return "blend";
    default: return "opaque";
    }
}

bool saveTextureSettings(const TextureAsset& asset, std::string* error)
{
    const std::filesystem::path file = textureSettingsPath(asset.imagePath);
    std::ofstream out(file);
    if (!out) {
        setError(error, "Cannot write " + file.string());
        return false;
    }
    const TextureSettings& s = asset.settings;
    out << std::setprecision(6);
    out << "# Texture settings for " << toUtf8(asset.imagePath.filename()) << " (the image itself is not changed)\n"
        << "version 1\n"
        << "wrap " << wrapName(s.wrap) << '\n'
        << "filter " << (s.nearest ? "nearest" : "linear") << '\n'
        << "mipmaps " << s.mipmaps << '\n'
        << "srgb " << s.srgb << '\n'
        << "flip_y " << s.flipY << '\n'
        << "brightness " << s.brightness << '\n'
        << "contrast " << s.contrast << '\n'
        << "saturation " << s.saturation << '\n'
        << "tint " << s.tint.r << ' ' << s.tint.g << ' ' << s.tint.b << '\n'
        << "compress " << s.compress << '\n'
        << "max_size " << s.maxSize << '\n';
    if (!out) {
        setError(error, "Write failed: " + file.string());
        return false;
    }
    return true;
}

namespace {

bool parseMaterial(Material& m, const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        setError(error, "Cannot open " + file.string());
        return false;
    }
    Material result;
    result.sourcePath = normalized(file);
    result.name = file.stem().string();
    std::array<std::string, kMapCount> maps;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string k;
        if (!(ls >> k) || k[0] == '#')
            continue;
        bool ok = true;
        if (k == "version") {
            int v = 0;
            ok = static_cast<bool>(ls >> v) && v >= 1 && v <= 2;
        } else if (k == "name") {
            ok = static_cast<bool>(ls >> std::quoted(result.name));
        } else if (k == "color") {
            ok = static_cast<bool>(ls >> result.color.r >> result.color.g >> result.color.b);
        } else if (k == "texture") { // version 1 name of the albedo map
            ok = static_cast<bool>(ls >> std::quoted(maps[0]));
        } else if (k == "tiling") {
            ok = static_cast<bool>(ls >> result.tiling.x >> result.tiling.y);
        } else if (k == "roughness") {
            ok = static_cast<bool>(ls >> result.roughness);
        } else if (k == "metallic") {
            ok = static_cast<bool>(ls >> result.metallic);
        } else if (k == "normal_strength") {
            ok = static_cast<bool>(ls >> result.normalStrength);
        } else if (k == "emissive") {
            ok = static_cast<bool>(ls >> result.emissive.r >> result.emissive.g >> result.emissive.b);
        } else if (k == "world_uv") {
            ok = static_cast<bool>(ls >> result.worldUv);
        } else if (k == "alpha_mode") {
            std::string v;
            ok = static_cast<bool>(ls >> v);
            result.alphaMode = v == "mask" ? AlphaMode::Mask : (v == "blend" ? AlphaMode::Blend : AlphaMode::Opaque);
        } else if (k == "alpha_cutoff") {
            ok = static_cast<bool>(ls >> result.alphaCutoff);
        } else if (k == "opacity") {
            ok = static_cast<bool>(ls >> result.opacity);
        } else if (k == "double_sided") {
            ok = static_cast<bool>(ls >> result.doubleSided);
        } else if (k == "surface") {
            ok = static_cast<bool>(ls >> result.surface);
        } else {
            for (int i = 0; i < kMapCount; ++i)
                if (k == mapKey(static_cast<MapKind>(i)))
                    ok = static_cast<bool>(ls >> std::quoted(maps[static_cast<std::size_t>(i)]));
            // other unknown keys (e.g. version 1 specular/shininess) are ignored
        }
        if (!ok) {
            setError(error, file.string() + ":" + std::to_string(lineNo) + ": invalid line: " + line);
            return false;
        }
    }
    for (int i = 0; i < kMapCount; ++i) {
        const std::string& texture = maps[static_cast<std::size_t>(i)];
        if (texture.empty())
            continue;
        std::filesystem::path p = fromUtf8(texture);
        if (p.is_relative())
            p = result.sourcePath.parent_path() / p;
        if (!setMaterialMap(result, static_cast<MapKind>(i), p, error))
            return false;
    }
    m = std::move(result);
    return true;
}

} // namespace

std::shared_ptr<Material> loadMaterial(const std::filesystem::path& file, std::string* error)
{
    const std::lock_guard lock(cacheMutex());
    auto& cache = materialCache();
    const std::filesystem::path key = normalized(file);
    if (auto it = cache.find(key); it != cache.end())
        if (auto shared = it->second.lock())
            return shared;
    auto m = std::make_shared<Material>();
    if (!parseMaterial(*m, key, error))
        return nullptr;
    cache[key] = m;
    return m;
}

bool reloadMaterial(Material& material, std::string* error)
{
    Material fresh;
    if (!parseMaterial(fresh, material.sourcePath, error))
        return false;
    material = std::move(fresh);
    return true;
}

bool saveMaterial(const Material& m, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        setError(error, "Cannot write " + file.string());
        return false;
    }
    out << std::setprecision(6);
    out << "# FPS engine material (physically based: metal/roughness)\nversion 2\n";
    out << "name " << std::quoted(m.name) << '\n';
    out << "color " << m.color.r << ' ' << m.color.g << ' ' << m.color.b << '\n';
    out << "roughness " << m.roughness << '\n';
    out << "metallic " << m.metallic << '\n';
    out << "normal_strength " << m.normalStrength << '\n';
    out << "emissive " << m.emissive.r << ' ' << m.emissive.g << ' ' << m.emissive.b << '\n';
    out << "tiling " << m.tiling.x << ' ' << m.tiling.y << '\n';
    out << "world_uv " << m.worldUv << '\n';
    out << "alpha_mode " << alphaModeName(m.alphaMode) << '\n';
    out << "alpha_cutoff " << m.alphaCutoff << '\n';
    out << "opacity " << m.opacity << '\n';
    out << "double_sided " << m.doubleSided << '\n';
    if (!m.surface.empty())
        out << "surface " << m.surface << '\n';
    const std::filesystem::path dir = normalized(file).parent_path();
    for (int i = 0; i < kMapCount; ++i) {
        const std::filesystem::path& p = m.mapPaths[static_cast<std::size_t>(i)];
        if (!p.empty())
            out << mapKey(static_cast<MapKind>(i)) << ' ' << std::quoted(toUtf8(relativeTo(p, dir))) << '\n';
    }
    if (!out) {
        setError(error, "Write failed: " + file.string());
        return false;
    }
    return true;
}

const char* mapKey(MapKind kind)
{
    switch (kind) {
    case MapKind::Albedo: return "albedo_map";
    case MapKind::Normal: return "normal_map";
    case MapKind::Roughness: return "roughness_map";
    case MapKind::Metallic: return "metallic_map";
    case MapKind::Occlusion: return "occlusion_map";
    case MapKind::Emissive: return "emissive_map";
    default: return "";
    }
}

const char* mapLabel(MapKind kind)
{
    switch (kind) {
    case MapKind::Albedo: return "Albedo (color)";
    case MapKind::Normal: return "Normal map";
    case MapKind::Roughness: return "Roughness map";
    case MapKind::Metallic: return "Metallic map";
    case MapKind::Occlusion: return "Ambient occlusion";
    case MapKind::Emissive: return "Emissive map";
    default: return "";
    }
}

bool mapIsColor(MapKind kind)
{
    return kind == MapKind::Albedo || kind == MapKind::Emissive;
}

bool setMaterialMap(Material& material, MapKind kind, const std::filesystem::path& image, std::string* error)
{
    const auto i = static_cast<std::size_t>(kind);
    if (image.empty()) {
        material.mapPaths[i].clear();
        material.maps[i].reset();
        return true;
    }
    auto asset = loadTextureAsset(image, error, mapIsColor(kind),
                                  kind == MapKind::Normal ? TextureUsage::Normal
                                                          : (mapIsColor(kind) ? TextureUsage::Color : TextureUsage::Data));
    if (!asset)
        return false;
    material.mapPaths[i] = asset->imagePath;
    material.maps[i] = std::move(asset);
    return true;
}

std::shared_ptr<TextureAsset> findTextureAsset(const std::filesystem::path& image)
{
    const std::lock_guard lock(cacheMutex());
    const auto it = textureCache().find(normalized(image));
    return it == textureCache().end() ? nullptr : it->second.lock();
}

std::shared_ptr<Material> findMaterial(const std::filesystem::path& file)
{
    const std::lock_guard lock(cacheMutex());
    const auto it = materialCache().find(normalized(file));
    return it == materialCache().end() ? nullptr : it->second.lock();
}

bool reloadTexture(const std::filesystem::path& image, std::string* error)
{
    const std::shared_ptr<TextureAsset> asset = findTextureAsset(image);
    if (!asset)
        return false;
    TextureAsset fresh = *asset; // decode outside the shared object, then swap the pixels in
    fresh.source.clear();
    if (!decodeImage(asset->imagePath, fresh, error))
        return false;
    asset->source = std::move(fresh.source);
    asset->width = fresh.width;
    asset->height = fresh.height;
    asset->settings = readTextureSettings(asset->imagePath, asset->settings);
    applyTextureSettings(*asset, true);
    return true;
}

} // namespace engine

