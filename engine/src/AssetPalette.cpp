#include "engine/AssetPalette.h"
#include "engine/Material.h"

#include <cgltf.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace engine {

namespace {
float toLinear(float c) { return std::pow(c, 2.2f); }
float toSrgb(float c) { return std::pow(std::clamp(c, 0.0f, 1.0f), 1.0f / 2.2f); }

std::vector<std::uint8_t> readBytes(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Decoded RGBA8 pixels of an image file or memory block.
struct Pixels {
    std::vector<std::uint8_t> data;
    int width = 0, height = 0;
};
bool decode(const std::uint8_t* bytes, std::size_t size, Pixels& out)
{
    int w = 0, h = 0, comp = 0;
    stbi_uc* p = stbi_load_from_memory(bytes, static_cast<int>(size), &w, &h, &comp, 4);
    if (!p)
        return false;
    out.data.assign(p, p + static_cast<std::size_t>(w) * h * 4);
    out.width = w;
    out.height = h;
    stbi_image_free(p);
    return true;
}
} // namespace

AlbedoStats albedoStats(const std::uint8_t* rgba, int width, int height, glm::vec3 tint, const PaletteLimits& limits)
{
    const float darkest = static_cast<float>(limits.darkest), brightest = static_cast<float>(limits.brightest);
    AlbedoStats s;
    const glm::vec3 tintLinear(toLinear(tint.r), toLinear(tint.g), toLinear(tint.b));
    double luminance = 0.0, saturation = 0.0;
    int saturated = 0, dark = 0, bright = 0;
    auto sample = [&](float r, float g, float b) {
        r = toSrgb(toLinear(r) * tintLinear.r) * 255.0f;
        g = toSrgb(toLinear(g) * tintLinear.g) * 255.0f;
        b = toSrgb(toLinear(b) * tintLinear.b) * 255.0f;
        const float hi = std::max({r, g, b}), lo = std::min({r, g, b});
        dark += hi < darkest ? 1 : 0;
        bright += lo > brightest ? 1 : 0;
        luminance += 0.2126 * r + 0.7152 * g + 0.0722 * b;
        saturation += (hi - lo) / 255.0f;
        ++saturated;
        ++s.pixels;
    };
    if (!rgba || width <= 0 || height <= 0) {
        sample(1.0f, 1.0f, 1.0f);
    } else {
        const int step = std::max(1, std::max(width, height) / 128);
        for (int y = step / 2; y < height; y += step)
            for (int x = step / 2; x < width; x += step) {
                const std::uint8_t* p = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
                if (p[3] < 128)
                    continue; // cut-out (leaves, fences)
                sample(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f);
            }
    }
    if (s.pixels > 0) {
        s.tooDark = static_cast<float>(dark) / static_cast<float>(s.pixels);
        s.tooBright = static_cast<float>(bright) / static_cast<float>(s.pixels);
        s.meanLuminance = static_cast<float>(luminance / s.pixels);
    }
    if (saturated > 0)
        s.meanSaturation = static_cast<float>(saturation / saturated);
    return s;
}

std::vector<std::string> paletteProblems(const AlbedoStats& s, const PaletteLimits& limits)
{
    std::vector<std::string> out;
    char text[160];
    if (s.pixels == 0)
        return out;
    if (s.tooDark > limits.maxShare || s.meanLuminance < static_cast<float>(limits.darkest)) {
        std::snprintf(text, sizeof(text), "too dark: %.0f %% of pixels below %d sRGB (mean %.0f)", s.tooDark * 100.0f, limits.darkest,
                      s.meanLuminance);
        out.emplace_back(text);
    }
    if (s.tooBright > limits.maxShare || s.meanLuminance > static_cast<float>(limits.brightest)) {
        std::snprintf(text, sizeof(text), "too bright: %.0f %% of pixels above %d sRGB (mean %.0f)", s.tooBright * 100.0f,
                      limits.brightest, s.meanLuminance);
        out.emplace_back(text);
    }
    if (s.meanSaturation > limits.maxSaturation) {
        std::snprintf(text, sizeof(text), "too saturated: mean %.2f (limit %.2f)", s.meanSaturation, limits.maxSaturation);
        out.emplace_back(text);
    }
    return out;
}

std::vector<PaletteEntry> checkAssetPalette(const std::filesystem::path& file, const PaletteLimits& limits)
{
    std::vector<PaletteEntry> result;
    auto fail = [&](const std::string& why) {
        PaletteEntry e;
        e.asset = file;
        e.problems.push_back(why);
        result.push_back(std::move(e));
        return result;
    };
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".material") {
        std::string error;
        const auto material = loadMaterial(file, &error);
        if (!material)
            return fail("cannot read: " + error);
        PaletteEntry e;
        e.asset = file;
        const std::filesystem::path& map = material->mapPath(MapKind::Albedo);
        Pixels px;
        if (!map.empty()) {
            const std::vector<std::uint8_t> bytes = readBytes(map);
            if (bytes.empty() || !decode(bytes.data(), bytes.size(), px))
                return fail("cannot read albedo map " + map.filename().string());
        }
        e.stats = albedoStats(px.data.empty() ? nullptr : px.data.data(), px.width, px.height, material->color, limits);
        e.problems = paletteProblems(e.stats, limits);
        result.push_back(std::move(e));
        return result;
    }
    if (ext != ".gltf" && ext != ".glb")
        return result;
    const std::u8string u8 = file.u8string();
    const std::string path(u8.begin(), u8.end());
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
        return fail("cannot parse glTF");
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return fail("cannot load glTF buffers");
    }
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& m = data->materials[i];
        if (!m.has_pbr_metallic_roughness)
            continue;
        const cgltf_pbr_metallic_roughness& pbr = m.pbr_metallic_roughness;
        const glm::vec3 factorLinear(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
        const glm::vec3 tint(toSrgb(factorLinear.r), toSrgb(factorLinear.g), toSrgb(factorLinear.b));
        Pixels px;
        if (const cgltf_texture* tex = pbr.base_color_texture.texture; tex && tex->image) {
            const cgltf_image* image = tex->image;
            if (image->buffer_view && image->buffer_view->buffer->data) {
                const auto* bytes = static_cast<const std::uint8_t*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
                decode(bytes, image->buffer_view->size, px);
            } else if (image->uri && std::string_view(image->uri).rfind("data:", 0) != 0) {
                const std::vector<std::uint8_t> bytes = readBytes(file.parent_path() / image->uri);
                if (!bytes.empty())
                    decode(bytes.data(), bytes.size(), px);
            }
        }
        PaletteEntry e;
        e.asset = file;
        e.part = m.name ? m.name : "material " + std::to_string(i);
        e.stats = albedoStats(px.data.empty() ? nullptr : px.data.data(), px.width, px.height, tint, limits);
        e.problems = paletteProblems(e.stats, limits);
        result.push_back(std::move(e));
    }
    cgltf_free(data);
    return result;
}

} // namespace engine
