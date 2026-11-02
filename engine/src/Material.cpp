#include "engine/Material.h"
#include "engine/Vfs.h"

#include <mutex>
#include "engine/Scene.h"

#include <stb_image.h> // implementation lives in GltfLoader.cpp

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
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
    std::string bytes;
    if (!engine::readFile(file, bytes)) // through the VFS: archives and mods
        return false;
    data.insert(data.end(), bytes.begin(), bytes.end());
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
    engine::VfsStream in(textureSettingsPath(image));
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

    if (!fileExists(key)) {
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
    engine::VfsStream in(file);
    if (!in) {
        setError(error, "Cannot open " + file.string());
        return false;
    }
    Material result;
    result.sourcePath = normalized(file);
    result.name = file.stem().string();
    std::array<std::string, kMapCount> maps;
    std::string parentFile;
    std::array<std::string, Material::kBlendLayers> layerFiles;
    std::set<std::string> keys; // values set in this file (an instance's overrides)
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
        } else if (k == "parent") {
            ok = static_cast<bool>(ls >> std::quoted(parentFile));
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
        } else if (k == "height_depth") {
            ok = static_cast<bool>(ls >> result.heightDepth);
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
        } else if (k == "detail_tiling") {
            ok = static_cast<bool>(ls >> result.detailTiling);
        } else if (k == "detail_strength") {
            ok = static_cast<bool>(ls >> result.detailStrength);
        } else if (k == "detail_distance") {
            ok = static_cast<bool>(ls >> result.detailDistance);
        } else if (k == "blend_sharpness") {
            ok = static_cast<bool>(ls >> result.blendSharpness);
        } else if (k == layerKey(0) || k == layerKey(1)) {
            ok = static_cast<bool>(ls >> std::quoted(layerFiles[k == layerKey(0) ? 0 : 1]));
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
        if (k != "version" && k != "name" && k != "parent")
            keys.insert(k == "texture" ? std::string(mapKey(MapKind::Albedo)) : k);
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
    static thread_local int depth = 0; // parents and layers referring back in a loop
    for (int i = 0; i < Material::kBlendLayers; ++i) {
        if (layerFiles[static_cast<std::size_t>(i)].empty())
            continue;
        std::filesystem::path p = fromUtf8(layerFiles[static_cast<std::size_t>(i)]);
        if (p.is_relative())
            p = result.sourcePath.parent_path() / p;
        p = normalized(p);
        result.layerPaths[static_cast<std::size_t>(i)] = p;
        if (depth > 8 || p == result.sourcePath)
            continue; // drawn without the layer rather than failing the material
        ++depth;
        result.layers[static_cast<std::size_t>(i)] = loadMaterial(p); // missing file: no layer, the path is kept
        --depth;
    }
    if (!parentFile.empty()) {
        // Instance: the parent's values, then the ones set here.
        if (depth > 8) {
            setError(error, file.string() + ": material parents form a loop");
            return false;
        }
        std::filesystem::path p = fromUtf8(parentFile);
        if (p.is_relative())
            p = result.sourcePath.parent_path() / p;
        ++depth;
        std::string parentError;
        result.parent = loadMaterial(p, &parentError);
        --depth;
        if (!result.parent) {
            setError(error, file.string() + ": parent: " + parentError);
            return false;
        }
        result.parentPath = result.parent->sourcePath;
        result.overrides = keys;
        applyMaterialParent(result);
    }
    m = std::move(result);
    return true;
}

void writeMaterialValue(std::ostream& out, const Material& m, const std::string& key, const std::filesystem::path& dir)
{
    if (key == "color")
        out << "color " << m.color.r << ' ' << m.color.g << ' ' << m.color.b << '\n';
    else if (key == "roughness")
        out << "roughness " << m.roughness << '\n';
    else if (key == "metallic")
        out << "metallic " << m.metallic << '\n';
    else if (key == "normal_strength")
        out << "normal_strength " << m.normalStrength << '\n';
    else if (key == "height_depth")
        out << "height_depth " << m.heightDepth << '\n';
    else if (key == "emissive")
        out << "emissive " << m.emissive.r << ' ' << m.emissive.g << ' ' << m.emissive.b << '\n';
    else if (key == "tiling")
        out << "tiling " << m.tiling.x << ' ' << m.tiling.y << '\n';
    else if (key == "world_uv")
        out << "world_uv " << m.worldUv << '\n';
    else if (key == "alpha_mode")
        out << "alpha_mode " << alphaModeName(m.alphaMode) << '\n';
    else if (key == "alpha_cutoff")
        out << "alpha_cutoff " << m.alphaCutoff << '\n';
    else if (key == "opacity")
        out << "opacity " << m.opacity << '\n';
    else if (key == "double_sided")
        out << "double_sided " << m.doubleSided << '\n';
    else if (key == "detail_tiling")
        out << "detail_tiling " << m.detailTiling << '\n';
    else if (key == "detail_strength")
        out << "detail_strength " << m.detailStrength << '\n';
    else if (key == "detail_distance")
        out << "detail_distance " << m.detailDistance << '\n';
    else if (key == "blend_sharpness")
        out << "blend_sharpness " << m.blendSharpness << '\n';
    else if (key == layerKey(0) || key == layerKey(1)) {
        const std::filesystem::path& p = m.layerPaths[key == layerKey(0) ? 0 : 1];
        out << key << ' ' << std::quoted(p.empty() ? std::string() : toUtf8(relativeTo(p, dir))) << '\n';
    } else if (key == "surface") {
        if (!m.surface.empty())
            out << "surface " << m.surface << '\n';
    } else {
        for (int i = 0; i < kMapCount; ++i)
            if (key == mapKey(static_cast<MapKind>(i))) {
                const std::filesystem::path& p = m.mapPaths[static_cast<std::size_t>(i)];
                // An instance may override a map with none: an empty path.
                out << key << ' ' << std::quoted(p.empty() ? std::string() : toUtf8(relativeTo(p, dir))) << '\n';
            }
    }
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
    const std::filesystem::path dir = normalized(file).parent_path();
    if (!m.parentPath.empty()) {
        // Instance: the parent and only the values set on it.
        out << "parent " << std::quoted(toUtf8(relativeTo(m.parentPath, dir))) << '\n';
        for (const std::string& key : materialValueKeys())
            if (m.overrides.count(key))
                writeMaterialValue(out, m, key, dir);
    } else {
        for (const std::string& key : materialValueKeys()) {
            const bool map = key.size() > 4 && key.compare(key.size() - 4, 4, "_map") == 0;
            bool empty = false;
            for (int i = 0; i < kMapCount && map; ++i)
                if (key == mapKey(static_cast<MapKind>(i)))
                    empty = m.mapPaths[static_cast<std::size_t>(i)].empty();
            const bool noDetail = m.mapPaths[static_cast<std::size_t>(MapKind::DetailAlbedo)].empty() &&
                                  m.mapPaths[static_cast<std::size_t>(MapKind::DetailNormal)].empty();
            if (noDetail && key.rfind("detail_", 0) == 0)
                empty = true; // no detail maps: their settings stay out of the file
            if (key == "height_depth" && m.mapPaths[static_cast<std::size_t>(MapKind::Height)].empty())
                empty = true;
            const bool noLayers = m.layerPaths[0].empty() && m.layerPaths[1].empty();
            if ((key == layerKey(0) && m.layerPaths[0].empty()) || (key == layerKey(1) && m.layerPaths[1].empty()) ||
                (key == "blend_sharpness" && noLayers))
                empty = true;
            if (!empty)
                writeMaterialValue(out, m, key, dir);
        }
    }
    if (!out) {
        setError(error, "Write failed: " + file.string());
        return false;
    }
    return true;
}

const char* layerKey(int layer)
{
    return layer == 0 ? "layer1_material" : "layer2_material";
}

bool setMaterialLayer(Material& material, int layer, const std::filesystem::path& file, std::string* error)
{
    if (layer < 0 || layer >= Material::kBlendLayers)
        return false;
    const auto i = static_cast<std::size_t>(layer);
    if (file.empty()) {
        material.layerPaths[i].clear();
        material.layers[i].reset();
        return true;
    }
    auto loaded = loadMaterial(file, error);
    if (!loaded)
        return false;
    if (loaded.get() == &material || loaded->sourcePath == material.sourcePath) {
        setError(error, "A material cannot be its own layer");
        return false;
    }
    material.layerPaths[i] = loaded->sourcePath;
    material.layers[i] = std::move(loaded);
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
    case MapKind::DetailAlbedo: return "detail_albedo_map";
    case MapKind::DetailNormal: return "detail_normal_map";
    case MapKind::Height: return "height_map";
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
    case MapKind::DetailAlbedo: return "Detail albedo (grey)";
    case MapKind::DetailNormal: return "Detail normal map";
    case MapKind::Height: return "Height map (parallax)";
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
                                  kind == MapKind::Normal || kind == MapKind::DetailNormal ? TextureUsage::Normal
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


namespace engine {

const std::vector<std::string>& materialValueKeys()
{
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> k = {"color",      "roughness",    "metallic", "normal_strength", "emissive",     "tiling",
                                      "world_uv",   "alpha_mode",   "alpha_cutoff", "opacity",     "double_sided", "surface",
                                      "detail_tiling", "detail_strength", "detail_distance", "layer1_material",
                                      "layer2_material", "blend_sharpness", "height_depth"};
        for (int i = 0; i < kMapCount; ++i)
            k.emplace_back(mapKey(static_cast<MapKind>(i)));
        return k;
    }();
    return keys;
}

void copyMaterialValue(Material& to, const Material& from, const std::string& key)
{
    if (key == "color")
        to.color = from.color;
    else if (key == "roughness")
        to.roughness = from.roughness;
    else if (key == "metallic")
        to.metallic = from.metallic;
    else if (key == "normal_strength")
        to.normalStrength = from.normalStrength;
    else if (key == "height_depth")
        to.heightDepth = from.heightDepth;
    else if (key == "emissive")
        to.emissive = from.emissive;
    else if (key == "tiling")
        to.tiling = from.tiling;
    else if (key == "world_uv")
        to.worldUv = from.worldUv;
    else if (key == "alpha_mode")
        to.alphaMode = from.alphaMode;
    else if (key == "alpha_cutoff")
        to.alphaCutoff = from.alphaCutoff;
    else if (key == "opacity")
        to.opacity = from.opacity;
    else if (key == "double_sided")
        to.doubleSided = from.doubleSided;
    else if (key == "surface")
        to.surface = from.surface;
    else if (key == "detail_tiling")
        to.detailTiling = from.detailTiling;
    else if (key == "detail_strength")
        to.detailStrength = from.detailStrength;
    else if (key == "detail_distance")
        to.detailDistance = from.detailDistance;
    else if (key == "blend_sharpness")
        to.blendSharpness = from.blendSharpness;
    else if (key == layerKey(0) || key == layerKey(1)) {
        const std::size_t i = key == layerKey(0) ? 0 : 1;
        to.layerPaths[i] = from.layerPaths[i];
        to.layers[i] = from.layers[i];
    } else
        for (int i = 0; i < kMapCount; ++i)
            if (key == mapKey(static_cast<MapKind>(i))) {
                to.mapPaths[static_cast<std::size_t>(i)] = from.mapPaths[static_cast<std::size_t>(i)];
                to.maps[static_cast<std::size_t>(i)] = from.maps[static_cast<std::size_t>(i)];
            }
}

std::vector<std::string> materialDifferences(const Material& a, const Material& b)
{
    std::vector<std::string> out;
    for (const std::string& key : materialValueKeys()) {
        bool same = true;
        if (key == "color")
            same = a.color == b.color;
        else if (key == "roughness")
            same = a.roughness == b.roughness;
        else if (key == "metallic")
            same = a.metallic == b.metallic;
        else if (key == "normal_strength")
            same = a.normalStrength == b.normalStrength;
        else if (key == "height_depth")
            same = a.heightDepth == b.heightDepth;
        else if (key == "emissive")
            same = a.emissive == b.emissive;
        else if (key == "tiling")
            same = a.tiling == b.tiling;
        else if (key == "world_uv")
            same = a.worldUv == b.worldUv;
        else if (key == "alpha_mode")
            same = a.alphaMode == b.alphaMode;
        else if (key == "alpha_cutoff")
            same = a.alphaCutoff == b.alphaCutoff;
        else if (key == "opacity")
            same = a.opacity == b.opacity;
        else if (key == "double_sided")
            same = a.doubleSided == b.doubleSided;
        else if (key == "surface")
            same = a.surface == b.surface;
        else if (key == "detail_tiling")
            same = a.detailTiling == b.detailTiling;
        else if (key == "detail_strength")
            same = a.detailStrength == b.detailStrength;
        else if (key == "detail_distance")
            same = a.detailDistance == b.detailDistance;
        else if (key == "blend_sharpness")
            same = a.blendSharpness == b.blendSharpness;
        else if (key == layerKey(0) || key == layerKey(1))
            same = a.layerPaths[key == layerKey(0) ? 0 : 1] == b.layerPaths[key == layerKey(0) ? 0 : 1];
        else
            for (int i = 0; i < kMapCount; ++i)
                if (key == mapKey(static_cast<MapKind>(i)))
                    same = a.mapPaths[static_cast<std::size_t>(i)] == b.mapPaths[static_cast<std::size_t>(i)];
        if (!same)
            out.push_back(key);
    }
    return out;
}

void applyMaterialParent(Material& material)
{
    if (!material.parent)
        return;
    applyMaterialParent(*material.parent); // a parent may be an instance too
    for (const std::string& key : materialValueKeys())
        if (!material.overrides.count(key))
            copyMaterialValue(material, *material.parent, key);
}

void refreshMaterialInstances()
{
    const std::lock_guard lock(cacheMutex());
    for (auto& [path, weak] : materialCache())
        if (auto m = weak.lock())
            applyMaterialParent(*m);
}

bool setMaterialParent(Material& material, const std::filesystem::path& parentFile, std::string* error)
{
    if (parentFile.empty()) {
        material.parent.reset();
        material.parentPath.clear();
        material.overrides.clear();
        return true;
    }
    auto parent = loadMaterial(parentFile, error);
    if (!parent)
        return false;
    for (const Material* p = parent.get(); p; p = p->parent.get())
        if (p == &material || p->sourcePath == material.sourcePath) {
            setError(error, "A material cannot be its own parent");
            return false;
        }
    material.parent = std::move(parent);
    material.parentPath = material.parent->sourcePath;
    material.overrides.clear();
    applyMaterialParent(material);
    return true;
}

} // namespace engine
