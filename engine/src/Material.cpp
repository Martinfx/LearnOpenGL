#include "engine/Material.h"
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

TextureSettings readTextureSettings(const std::filesystem::path& image)
{
    TextureSettings s;
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
        }
    }
    return s;
}

std::shared_ptr<TextureAsset> loadTextureAsset(const std::filesystem::path& image, std::string* error)
{
    static std::map<std::filesystem::path, std::weak_ptr<TextureAsset>> cache;
    const std::filesystem::path key = normalized(image);
    if (auto it = cache.find(key); it != cache.end())
        if (auto shared = it->second.lock())
            return shared;

    std::vector<unsigned char> bytes;
    if (!readFile(key, bytes)) {
        setError(error, "Cannot open " + key.string());
        return nullptr;
    }
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp, 4);
    if (!pixels) {
        setError(error, key.string() + ": unsupported image (PNG or JPEG expected)");
        return nullptr;
    }
    auto asset = std::make_shared<TextureAsset>();
    asset->imagePath = key;
    asset->width = w;
    asset->height = h;
    asset->source.assign(pixels, pixels + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(pixels);

    asset->settings = readTextureSettings(key);
    asset->texture = std::make_shared<Texture>();
    applyTextureSettings(*asset);
    cache[key] = asset;
    return asset;
}

void applyTextureSettings(TextureAsset& asset)
{
    const TextureSettings& s = asset.settings;
    if (!asset.texture)
        asset.texture = std::make_shared<Texture>();
    Texture& t = *asset.texture;
    t.id = newResourceId(); // the renderer uploads the new pixels and frees the old ones
    t.width = asset.width;
    t.height = asset.height;
    t.srgb = s.srgb;
    t.nearest = s.nearest;
    t.mipmaps = s.mipmaps;
    t.wrap = static_cast<int>(s.wrap);
    t.rgba.resize(asset.source.size());
    const bool identity = s.brightness == 0.0f && s.contrast == 1.0f && s.saturation == 1.0f && s.tint == glm::vec3(1.0f);
    const std::size_t rowBytes = static_cast<std::size_t>(asset.width) * 4;
    for (int y = 0; y < asset.height; ++y) {
        const int srcY = s.flipY ? asset.height - 1 - y : y;
        const std::uint8_t* src = asset.source.data() + static_cast<std::size_t>(srcY) * rowBytes;
        std::uint8_t* dst = t.rgba.data() + static_cast<std::size_t>(y) * rowBytes;
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
        << "tint " << s.tint.r << ' ' << s.tint.g << ' ' << s.tint.b << '\n';
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
    std::string line, texture;
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
            ok = static_cast<bool>(ls >> v) && v == 1;
        } else if (k == "name") {
            ok = static_cast<bool>(ls >> std::quoted(result.name));
        } else if (k == "color") {
            ok = static_cast<bool>(ls >> result.color.r >> result.color.g >> result.color.b);
        } else if (k == "texture") {
            ok = static_cast<bool>(ls >> std::quoted(texture));
        } else if (k == "tiling") {
            ok = static_cast<bool>(ls >> result.tiling.x >> result.tiling.y);
        } else if (k == "specular") {
            ok = static_cast<bool>(ls >> result.specular);
        } else if (k == "shininess") {
            ok = static_cast<bool>(ls >> result.shininess);
        } else if (k == "emissive") {
            ok = static_cast<bool>(ls >> result.emissive.r >> result.emissive.g >> result.emissive.b);
        } else if (k == "world_uv") {
            ok = static_cast<bool>(ls >> result.worldUv);
        } // unknown keys are ignored so newer files still load
        if (!ok) {
            setError(error, file.string() + ":" + std::to_string(lineNo) + ": invalid line: " + line);
            return false;
        }
    }
    if (!texture.empty()) {
        std::filesystem::path p = fromUtf8(texture);
        if (p.is_relative())
            p = result.sourcePath.parent_path() / p;
        if (!setMaterialTexture(result, p, error))
            return false;
    }
    m = std::move(result);
    return true;
}

} // namespace

std::shared_ptr<Material> loadMaterial(const std::filesystem::path& file, std::string* error)
{
    static std::map<std::filesystem::path, std::weak_ptr<Material>> cache;
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
    out << "# FPS engine material\nversion 1\n";
    out << "name " << std::quoted(m.name) << '\n';
    out << "color " << m.color.r << ' ' << m.color.g << ' ' << m.color.b << '\n';
    if (!m.texturePath.empty())
        out << "texture " << std::quoted(toUtf8(relativeTo(m.texturePath, normalized(file).parent_path()))) << '\n';
    out << "tiling " << m.tiling.x << ' ' << m.tiling.y << '\n';
    out << "specular " << m.specular << '\n';
    out << "shininess " << m.shininess << '\n';
    out << "emissive " << m.emissive.r << ' ' << m.emissive.g << ' ' << m.emissive.b << '\n';
    out << "world_uv " << m.worldUv << '\n';
    if (!out) {
        setError(error, "Write failed: " + file.string());
        return false;
    }
    return true;
}

bool setMaterialTexture(Material& material, const std::filesystem::path& image, std::string* error)
{
    if (image.empty()) {
        material.texturePath.clear();
        material.texture.reset();
        return true;
    }
    auto asset = loadTextureAsset(image, error);
    if (!asset)
        return false;
    material.texturePath = asset->imagePath;
    material.texture = std::move(asset);
    return true;
}

} // namespace engine
