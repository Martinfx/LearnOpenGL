#include "engine/MapBake.h"
#include "engine/JobSystem.h"
#include "engine/Log.h"
#include "engine/Renderer.h"
#include "engine/Terrain.h"
#include "engine/Vfs.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <locale>
#include <sstream>

namespace engine {

namespace {

constexpr int kVersion = 6; // bump when the look of the bake changes: old cache files are not used
constexpr int kMaxPixels = 4096, kMinPixels = 2048;
constexpr float kTileMetres = 48.0f;

struct Hasher {
    std::uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, std::size_t n)
    {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    template <class T> void add(const T& v) { bytes(&v, sizeof v); }
    void str(const std::string& s)
    {
        bytes(s.data(), s.size());
        add(s.size());
    }
};

bool includeInArea(const Scene& scene, int i)
{
    if (scene.isEntity(i)) {
        const Entity& e = scene.entity(i);
        return !e.helper() && e.type != "empty";
    }
    if (scene.isModel(i))
        return scene.model(i).collision; // solid things; grass, far scenery and skylines are left out
    return true;                         // boxes
}

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

} // namespace

MapArea mapAreaOf(const Scene& scene)
{
    MapArea a;
    if (scene.map.size > 0.0f) {
        a.min = scene.map.min;
        a.size = glm::vec2(scene.map.size, scene.map.sizeZ > 0.0f ? scene.map.sizeZ : scene.map.size);
        return a;
    }
    glm::vec2 lo(scene.playerStart.x, scene.playerStart.z), hi = lo;
    for (int i = 0; i < scene.objectCount(); ++i) {
        if (!includeInArea(scene, i))
            continue;
        const Aabb b = scene.objectBounds(i);
        if (!(b.min.x <= b.max.x) || std::abs(b.min.x) > 1e5f || std::abs(b.max.z) > 1e5f)
            continue;
        lo = glm::min(lo, glm::vec2(b.min.x, b.min.z));
        hi = glm::max(hi, glm::vec2(b.max.x, b.max.z));
    }
    const glm::vec2 margin = glm::max((hi - lo) * 0.04f, glm::vec2(12.0f));
    a.min = lo - margin;
    a.size = glm::max(hi - lo + margin * 2.0f, glm::vec2(60.0f));
    return a;
}

std::uint64_t mapBakeHash(const Scene& scene)
{
    Hasher h;
    h.add(kVersion);
    const MapArea area = mapAreaOf(scene);
    h.add(area.min);
    h.add(area.size);
    const MapSettings& m = scene.map;
    for (float v : {m.pixelsPerMetre, m.sunAzimuth, m.sunElevation, m.exposure, m.saturation, m.contrast, m.outlines, m.heightShade})
        h.add(v);
    h.add(m.daylight);
    for (const Model& model : scene.models) {
        h.str(model.sourcePath.filename().generic_string());
        h.add(model.position);
        h.add(model.rotation);
        h.add(model.scale);
    }
    for (const Box& b : scene.boxes) {
        h.add(b.position);
        h.add(b.size);
        h.add(b.rotation);
        h.add(b.color);
        h.str(b.materialPath.filename().generic_string());
    }
    // Entities are left out: the game adds its own at the start (animals, pickups), the editor does not.
    if (const Terrain* t = scene.terrain.get()) {
        h.add(t->origin);
        h.add(t->size);
        h.bytes(t->heights.data(), t->heights.size() * sizeof(float));
        h.bytes(t->splat.data(), t->splat.size());
        h.add(t->foliage.size());
        for (const FoliageType& f : t->foliageTypes)
            h.str(f.modelPath.filename().generic_string());
        for (const TerrainLayer& l : t->layers)
            h.str(l.materialPath.filename().generic_string());
    }
    if (!m.daylight) {
        const Environment& e = scene.environment;
        h.add(scene.sunDirection);
        h.add(e.sunColor);
        h.add(e.sunIntensity);
        h.add(e.skyZenith);
        h.add(e.ambientIntensity);
        h.add(e.exposure);
        h.add(e.rain);
    }
    return h.h;
}

std::filesystem::path mapCachePath(const std::filesystem::path& levelFile, const Scene& scene)
{
    char name[64];
    std::snprintf(name, sizeof name, "_%016llx.ctex", static_cast<unsigned long long>(mapBakeHash(scene)));
    return textureCookConfig().cacheDir.parent_path() / "maps" / (levelFile.stem().string() + name);
}

bool writeMapCache(const std::filesystem::path& file, const CookedTexture& texture, const MapArea& area)
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // Older bakes of the same level (<stem>_<hash>) are not needed any more.
    const std::string stem = file.stem().string();
    if (const std::size_t cut = stem.rfind('_'); cut != std::string::npos)
        for (const auto& entry : std::filesystem::directory_iterator(file.parent_path(), ec)) {
            const std::string other = entry.path().stem().string();
            if (other != stem && other.size() == stem.size() && other.compare(0, cut + 1, stem, 0, cut + 1) == 0)
                std::filesystem::remove(entry.path(), ec);
        }
    std::filesystem::path areaFile = file;
    areaFile.replace_extension(".area");
    {
        std::ofstream out(areaFile, std::ios::trunc);
        out.imbue(std::locale::classic());
        out << area.min.x << ' ' << area.min.y << ' ' << area.size.x << ' ' << area.size.y << '\n';
        if (!out)
            return false;
    }
    return writeCookedTexture(file, texture);
}

bool readMapCache(const std::filesystem::path& file, CookedTexture& texture, MapArea& area)
{
    std::filesystem::path areaFile = file;
    areaFile.replace_extension(".area");
    std::ifstream in(areaFile);
    in.imbue(std::locale::classic());
    if (!(in >> area.min.x >> area.min.y >> area.size.x >> area.size.y))
        return false;
    return readCookedTexture(file, texture);
}

unsigned uploadMapTexture(const CookedTexture& c)
{
    if (c.mips.empty() || c.width <= 0 || c.height <= 0)
        return 0;
    GLenum format = GL_RGBA8;
    if (c.format == TextureFormat::BC1)
        format = GL_COMPRESSED_RGB_S3TC_DXT1_EXT; // display colours: the UI shader shows them as they are
    else if (c.format == TextureFormat::BC3)
        format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    else if (c.format != TextureFormat::RGBA8)
        return 0;
    unsigned tex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    glTextureStorage2D(tex, static_cast<GLsizei>(c.mips.size()), format, c.width, c.height);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (std::size_t i = 0; i < c.mips.size(); ++i) {
        const TextureMip& m = c.mips[i];
        if (c.format == TextureFormat::RGBA8)
            glTextureSubImage2D(tex, static_cast<GLint>(i), 0, 0, m.width, m.height, GL_RGBA, GL_UNSIGNED_BYTE, c.data.data() + m.offset);
        else
            glCompressedTextureSubImage2D(tex, static_cast<GLint>(i), 0, 0, m.width, m.height, format, static_cast<GLsizei>(m.size),
                                          c.data.data() + m.offset);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {0.06f, 0.065f, 0.055f, 1.0f};
    glTextureParameterfv(tex, GL_TEXTURE_BORDER_COLOR, border);
    glTextureParameterf(tex, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    return tex;
}

CookedTexture cookMapImage(const MapImage& image)
{
    CookOptions o;
    o.usage = TextureUsage::Color;
    o.srgb = true;
    o.compress = textureCookConfig().compress;
    o.mipmaps = true;
    return cookTexture(image.rgba.data(), image.width, image.height, o);
}

void MapBaker::begin(const Scene& scene)
{
    settings_ = scene.map;
    area_ = mapAreaOf(scene);
    const float longest = std::max(area_.size.x, area_.size.y);
    float ppm = std::clamp(settings_.pixelsPerMetre, 0.5f, 24.0f);
    ppm = std::clamp(ppm, std::min(24.0f, static_cast<float>(kMinPixels) / longest), static_cast<float>(kMaxPixels) / longest);
    tilePixels_ = std::clamp(static_cast<int>(std::lround(kTileMetres * ppm)), 96, 512);
    // Whole tiles that stay within the size limit.
    const int maxTiles = std::max(1, kMaxPixels / tilePixels_);
    const float tileMetres = static_cast<float>(tilePixels_) / ppm;
    tilesX_ = std::clamp(static_cast<int>(std::ceil(area_.size.x / tileMetres)), 1, maxTiles);
    tilesZ_ = std::clamp(static_cast<int>(std::ceil(area_.size.y / tileMetres)), 1, maxTiles);
    width_ = tilesX_ * tilePixels_;
    height_ = tilesZ_ * tilePixels_;
    metresPerPixel_ = 1.0f / ppm;
    // Grow the area to the whole tiles around its middle.
    const glm::vec2 center = area_.min + area_.size * 0.5f;
    area_.size = glm::vec2(static_cast<float>(width_), static_cast<float>(height_)) * metresPerPixel_;
    area_.min = center - area_.size * 0.5f;
    pad_ = std::max(16, tilePixels_ / 8);
    tiles_ = tilesX_ * tilesZ_;
    done_ = 0;
    // Heights: the camera above the highest object, the far plane under the lowest.
    float hiY = -1e30f, loY = 1e30f;
    for (int i = 0; i < scene.objectCount(); ++i) {
        if (scene.isEntity(i) && scene.entity(i).helper())
            continue;
        const Aabb b = scene.objectBounds(i);
        if (!(b.min.y <= b.max.y) || std::abs(b.max.y) > 1e4f || std::abs(b.min.y) > 1e4f)
            continue;
        const bool inside = b.max.x >= area_.min.x && b.min.x <= area_.min.x + area_.size.x && b.max.z >= area_.min.y &&
                            b.min.z <= area_.min.y + area_.size.y;
        if (!inside)
            continue;
        hiY = std::max(hiY, b.max.y);
        loY = std::min(loY, b.min.y);
    }
    if (const Terrain* t = scene.terrain.get(); t && t->valid()) {
        const Aabb b = t->bounds();
        hiY = std::max(hiY, b.max.y);
        loY = std::min(loY, b.min.y);
    }
    if (hiY < loY) {
        hiY = 20.0f;
        loY = -5.0f;
    }
    top_ = std::min(hiY, loY + 600.0f) + 5.0f;
    bottom_ = loY - 5.0f;
    // Light.
    environment_ = scene.environment; // the level's look: grading, tone mapping, LUT, materials' wetness
    if (settings_.daylight) {
        // ...under a fair sky: a sun that throws readable shadows, sky light that keeps them open.
        // The level's sky and colours stay; the sun comes out from behind the clouds.
        Environment& e = environment_;
        e.sunIntensity = std::max(e.sunIntensity, 1.8f);
        e.sunColor = glm::mix(e.sunColor, Environment().sunColor, 0.5f);
        e.cloudCover = std::min(e.cloudCover, 0.5f);
        e.ambientIntensity = std::max(e.ambientIntensity, 1.0f);
        e.rain = 0.0f;
        e.contactShadowLength = std::max(e.contactShadowLength, 0.4f);
        const float az = glm::radians(settings_.sunAzimuth), el = glm::radians(std::clamp(settings_.sunElevation, 5.0f, 89.0f));
        const glm::vec3 toSun(std::sin(az) * std::cos(el), std::sin(el), -std::cos(az) * std::cos(el));
        sun_ = -toSun;
    } else {
        sun_ = scene.sunDirection;
    }
    Environment& e = environment_;
    e.fogDensity = 0.0f;
    e.groundHaze = 0.0f;
    e.fogVolumetricRange = 0.0f;
    e.volumetricClouds = 0.0f;
    e.grain = 0.0f;
    e.vignette = 0.0f;
    e.bloomIntensity = 0.0f;
    e.autoExposure = 0.0f;
    e.skyOcclusion = 0.0f;
    e.storm = 0.0f;
    e.weather.enabled = false;
    e.weatherNow = WeatherNow{};
    e.anomalyStormNow = AnomalyStormNow{};
    e.time.enabled = false;
    e.shadowDistance = (top_ - bottom_) + 20.0f; // the cascades reach the ground from the camera
    e.exposure *= std::max(0.05f, settings_.exposure);
    e.ssrIntensity = 0.0f;
    if (settings_.daylight)
        e.rain = 0.0f;
    color_.assign(static_cast<std::size_t>(width_) * height_ * 3, 0);
    height_m_.assign(static_cast<std::size_t>(width_) * height_, bottom_);
    LOG_INFO("map", "baking the map: %.0f x %.0f m, %d x %d px (%.1f px/m), %d tiles", area_.size.x, area_.size.y, width_, height_, ppm, tiles_);
}

bool MapBaker::step(Renderer& renderer, const Scene& scene, double budgetMs)
{
    if (done())
        return true;
    const auto start = std::chrono::steady_clock::now();
    const RenderQuality saved = renderer.quality();
    const bool savedDeterministic = renderer.deterministicStreaming();
    RenderQuality q = saved;
    q.taa = false;
    q.ssr = false;
    q.fxaa = true;
    q.bloom = false;
    q.motionBlur = 0.0f;
    q.volumetricClouds = 0;
    q.volumetricFog = 0;
    q.textureUploadsPerFrame = 0;
    q.shadowCache = false;
    q.detailDensityFar = 1.0f;
    q.lodScale = std::max(saved.lodScale, 3.0f);
    q.occlusionCulling = false;
    q.asyncCull = false;
    q.renderScale = 1.0f;
    q.dynamicTargetFps = 0.0f;
    q.shadowResolution = std::max(saved.shadowResolution, 1024);
    q.shadowCascades = 3;
    renderer.setQuality(q);
    renderer.setDeterministicStreaming(true);
    GLint previous = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int size = tilePixels_ + pad_ * 2;
    if (fboSize_ != size) {
        if (fbo_) {
            glDeleteFramebuffers(1, &fbo_);
            glDeleteTextures(1, &colorTex_);
            glDeleteRenderbuffers(1, &depthRb_);
        }
        glCreateTextures(GL_TEXTURE_2D, 1, &colorTex_);
        glTextureStorage2D(colorTex_, 1, GL_RGBA8, size, size);
        glCreateRenderbuffers(1, &depthRb_);
        glNamedRenderbufferStorage(depthRb_, GL_DEPTH24_STENCIL8, size, size);
        glCreateFramebuffers(1, &fbo_);
        glNamedFramebufferTexture(fbo_, GL_COLOR_ATTACHMENT0, colorTex_, 0);
        glNamedFramebufferRenderbuffer(fbo_, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRb_);
        fboSize_ = size;
    }
    while (!done()) {
        renderTile(renderer, scene, done_ % tilesX_, done_ / tilesX_);
        ++done_;
        if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() > budgetMs)
            break;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(previous));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    renderer.setDeterministicStreaming(savedDeterministic);
    renderer.setQuality(saved);
    renderer.resetExposure();
    if (done()) {
        glDeleteFramebuffers(1, &fbo_);
        glDeleteTextures(1, &colorTex_);
        glDeleteRenderbuffers(1, &depthRb_);
        fbo_ = colorTex_ = depthRb_ = 0;
        fboSize_ = 0;
    }
    return done();
}

void MapBaker::renderTile(Renderer& renderer, const Scene& scene, int tx, int tz)
{
    const int size = fboSize_;
    const float half = static_cast<float>(size) * 0.5f * metresPerPixel_;
    const float tileMetres = static_cast<float>(tilePixels_) * metresPerPixel_;
    const glm::vec2 c = area_.min + glm::vec2((static_cast<float>(tx) + 0.5f) * tileMetres, (static_cast<float>(tz) + 0.5f) * tileMetres);
    RenderView view;
    view.viewportWidth = view.viewportHeight = size;
    view.cameraPosition = glm::vec3(c.x, top_, c.y);
    view.view = glm::lookAt(view.cameraPosition, view.cameraPosition - glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    view.projection = glm::ortho(-half, half, -half, half, 0.0f, top_ - bottom_);
    view.fog = false;
    view.temporal = false;
    view.environment = &environment_;
    view.sunDirection = sun_;
    float ground = 0.0f;
    if (const Terrain* t = scene.terrain.get(); t && t->valid() && t->contains(c.x, c.y))
        ground = t->heightAt(c.x, c.y);
    view.lodEye = glm::vec3(c.x, ground + 1.7f, c.y); // a person standing in the middle of the tile
    view.wetness = settings_.daylight ? 0.0f : -1.0f;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    // Twice: the first pass asks for the full textures and LODs near the tile, the second draws them.
    renderer.render(scene, view);
    renderer.render(scene, view);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    int dw = 0, dh = 0;
    const std::vector<float> depth = renderer.readSceneDepth(dw, dh);
    const bool haveDepth = dw == size && dh == size && depth.size() == static_cast<std::size_t>(size) * size;
    const float range = top_ - bottom_;
    for (int r = 0; r < tilePixels_; ++r) {
        const int srcRow = size - 1 - (r + pad_); // GL rows from the bottom; image rows from the top (north)
        const int dstRow = tz * tilePixels_ + r;
        for (int col = 0; col < tilePixels_; ++col) {
            const std::size_t s = static_cast<std::size_t>(srcRow) * size + static_cast<std::size_t>(col + pad_);
            const std::size_t d = static_cast<std::size_t>(dstRow) * width_ + static_cast<std::size_t>(tx * tilePixels_ + col);
            color_[d * 3 + 0] = rgba[s * 4 + 0];
            color_[d * 3 + 1] = rgba[s * 4 + 1];
            color_[d * 3 + 2] = rgba[s * 4 + 2];
            if (haveDepth) {
                const float z = depth[s];
                height_m_[d] = z >= 1.0f ? bottom_ : top_ - z * range;
            }
        }
    }
}

MapImage MapBaker::finish() const
{
    MapImage out;
    out.width = width_;
    out.height = height_;
    out.area = area_;
    out.rgba.assign(static_cast<std::size_t>(width_) * height_ * 4, 255);
    if (!done() || width_ <= 0)
        return out;
    // Ground level: a low percentile of the heights (streets and yards are the most of the map).
    std::vector<float> sample;
    for (std::size_t i = 0; i < height_m_.size(); i += 61)
        sample.push_back(height_m_[i]);
    std::nth_element(sample.begin(), sample.begin() + static_cast<std::ptrdiff_t>(sample.size() / 5), sample.end());
    const float ground = sample.empty() ? 0.0f : sample[sample.size() / 5];
    // Auto levels: the middle brightness of the map at a readable grey, whatever the weather.
    float gain = 1.0f;
    {
        std::vector<float> luma;
        for (std::size_t i = 0; i < height_m_.size(); i += 37)
            if (height_m_[i] > bottom_ + 0.01f) // not the empty background
                luma.push_back((0.2126f * color_[i * 3] + 0.7152f * color_[i * 3 + 1] + 0.0722f * color_[i * 3 + 2]) / 255.0f);
        if (!luma.empty()) {
            std::nth_element(luma.begin(), luma.begin() + static_cast<std::ptrdiff_t>(luma.size() / 2), luma.end());
            gain = std::clamp(0.3f / std::max(0.01f, luma[luma.size() / 2]), 0.6f, 3.0f);
        }
    }
    const float ppm = 1.0f / metresPerPixel_;
    const int reach = std::max(1, static_cast<int>(std::lround(ppm * 0.18f))); // outline width ~0.35 m
    const MapSettings& s = settings_;
    auto heightAt = [&](int x, int y) {
        x = std::clamp(x, 0, width_ - 1);
        y = std::clamp(y, 0, height_ - 1);
        return height_m_[static_cast<std::size_t>(y) * width_ + x];
    };
    JobSystem::global().parallelFor(static_cast<std::size_t>(height_), 16, [&](std::size_t y0, std::size_t y1) {
        for (int y = static_cast<int>(y0); y < static_cast<int>(y1); ++y)
            for (int x = 0; x < width_; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * width_ + x;
                glm::vec3 c(color_[i * 3], color_[i * 3 + 1], color_[i * 3 + 2]);
                c *= gain / 255.0f;
                const float h = height_m_[i];
                // Step down to a neighbour (wall seen from above): an outline on the high side.
                float drop = 0.0f;
                for (int d = 1; d <= reach; ++d)
                    drop = std::max({drop, h - heightAt(x + d, y), h - heightAt(x - d, y), h - heightAt(x, y + d), h - heightAt(x, y - d)});
                const float line = glm::smoothstep(0.6f, 2.5f, drop) * clamp01(s.outlines);
                // Height: roofs a touch lighter, sunken ground a touch darker.
                const float lift = std::clamp((h - ground) / 18.0f, -0.6f, 1.0f) * s.heightShade;
                c *= 1.0f + lift * 0.45f;
                const float luma = glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
                c = glm::mix(glm::vec3(luma), c, std::max(0.0f, s.saturation));
                c = glm::pow(glm::max(c, glm::vec3(0.0f)), glm::vec3(0.85f)); // open up the deep shade of street canyons
                c = (c - 0.5f) * std::max(0.1f, s.contrast) + 0.5f;
                c *= 1.0f - 0.8f * line;
                c = glm::clamp(c, 0.0f, 1.0f);
                for (int k = 0; k < 3; ++k)
                    out.rgba[i * 4 + k] = static_cast<std::uint8_t>(std::lround(c[k] * 255.0f));
            }
    });
    return out;
}

} // namespace engine
