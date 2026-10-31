#include "engine/Terrain.h"
#include "engine/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <glm/geometric.hpp>

namespace engine {

namespace {

float brushFalloff(float distance, float radius)
{
    if (distance >= radius)
        return 0.0f;
    const float x = 1.0f - distance / radius; // 1 in the center, 0 at the edge
    return x * x * (3.0f - 2.0f * x);          // smoothstep
}

FILE* openFile(const std::filesystem::path& file, const char* mode)
{
#ifdef _WIN32
    const std::wstring wmode(mode, mode + std::strlen(mode));
    return _wfopen(file.wstring().c_str(), wmode.c_str());
#else
    return std::fopen(file.c_str(), mode);
#endif
}

constexpr std::uint32_t kMagic = 0x52524554; // "TERR"
constexpr std::uint32_t kVersion = 1;

struct FileHeader {
    std::uint32_t magic, version;
    std::int32_t resolution, splatResolution;
    float originX, originZ, size;
    std::uint32_t foliageCount;
};

} // namespace

void Terrain::create(glm::vec2 o, float s, int res, int splatRes)
{
    origin = o;
    size = std::max(1.0f, s);
    resolution = std::clamp(res, 2, 4097);
    splatResolution = std::clamp(splatRes, 16, 4096);
    heights.assign(static_cast<std::size_t>(resolution) * resolution, 0.0f);
    splat.assign(static_cast<std::size_t>(splatResolution) * splatResolution * 4, 0);
    for (std::size_t i = 0; i < splat.size(); i += 4)
        splat[i] = 255; // layer 0 everywhere
    foliage.clear();
    meshVersion = newResourceId();
    splatVersion = newResourceId();
    foliageVersion = newResourceId();
}

bool Terrain::contains(float x, float z) const
{
    return x >= origin.x && z >= origin.y && x <= origin.x + size && z <= origin.y + size;
}

float Terrain::heightAt(float x, float z) const
{
    if (!valid())
        return 0.0f;
    const float gx = std::clamp((x - origin.x) / spacing(), 0.0f, static_cast<float>(resolution - 1));
    const float gz = std::clamp((z - origin.y) / spacing(), 0.0f, static_cast<float>(resolution - 1));
    const int x0 = std::min(static_cast<int>(gx), resolution - 2), z0 = std::min(static_cast<int>(gz), resolution - 2);
    const float fx = gx - static_cast<float>(x0), fz = gz - static_cast<float>(z0);
    auto h = [&](int ix, int iz) { return heights[static_cast<std::size_t>(iz) * resolution + ix]; };
    const float a = h(x0, z0) + (h(x0 + 1, z0) - h(x0, z0)) * fx;
    const float b = h(x0, z0 + 1) + (h(x0 + 1, z0 + 1) - h(x0, z0 + 1)) * fx;
    return a + (b - a) * fz;
}

glm::vec3 Terrain::normalAt(float x, float z) const
{
    const float d = spacing();
    const float hx = heightAt(x + d, z) - heightAt(x - d, z);
    const float hz = heightAt(x, z + d) - heightAt(x, z - d);
    return glm::normalize(glm::vec3(-hx, 2.0f * d, -hz));
}

Aabb Terrain::bounds() const
{
    float lo = 0.0f, hi = 0.0f;
    if (!heights.empty()) {
        const auto [mn, mx] = std::minmax_element(heights.begin(), heights.end());
        lo = *mn;
        hi = *mx;
    }
    return {{origin.x, lo, origin.y}, {origin.x + size, hi, origin.y + size}};
}

bool Terrain::raycast(glm::vec3 o, glm::vec3 dir, float maxDistance, float& t) const
{
    if (!valid())
        return false;
    Aabb box = bounds();
    box.min.y -= 0.01f;
    box.max.y += 0.01f;
    // Clip the ray to the terrain box, then march in half-sample steps.
    float t0 = 0.0f, t1 = maxDistance;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(dir[a]) < 1e-8f) {
            if (o[a] < box.min[a] || o[a] > box.max[a])
                return false;
            continue;
        }
        float n = (box.min[a] - o[a]) / dir[a], f = (box.max[a] - o[a]) / dir[a];
        if (n > f)
            std::swap(n, f);
        t0 = std::max(t0, n);
        t1 = std::min(t1, f);
        if (t0 > t1)
            return false;
    }
    const float step = spacing() * 0.5f;
    auto above = [&](float s) {
        const glm::vec3 p = o + dir * s;
        return p.y - heightAt(p.x, p.z);
    };
    float prev = t0, prevH = above(t0);
    if (prevH < 0.0f) {
        t = t0;
        return true;
    }
    for (float s = t0 + step; s <= t1 + step; s += step) {
        const float cur = std::min(s, t1);
        const float h = above(cur);
        if (h <= 0.0f) {
            float a = prev, b = cur; // bisect between the last point above and this one below
            for (int i = 0; i < 16; ++i) {
                const float m = 0.5f * (a + b);
                (above(m) > 0.0f ? a : b) = m;
            }
            t = 0.5f * (a + b);
            return true;
        }
        prev = cur;
        prevH = h;
        if (cur >= t1)
            break;
    }
    return false;
}

void Terrain::sculpt(glm::vec3 c, float radius, float strength, Sculpt mode, float flattenHeight)
{
    if (!valid() || radius <= 0.0f)
        return;
    const float d = spacing();
    const int x0 = std::max(0, static_cast<int>(std::floor((c.x - radius - origin.x) / d)));
    const int x1 = std::min(resolution - 1, static_cast<int>(std::ceil((c.x + radius - origin.x) / d)));
    const int z0 = std::max(0, static_cast<int>(std::floor((c.z - radius - origin.y) / d)));
    const int z1 = std::min(resolution - 1, static_cast<int>(std::ceil((c.z + radius - origin.y) / d)));
    if (x0 > x1 || z0 > z1)
        return;
    const std::vector<float> before = mode == Sculpt::Smooth ? heights : std::vector<float>();
    auto at = [&](const std::vector<float>& h, int ix, int iz) {
        ix = std::clamp(ix, 0, resolution - 1);
        iz = std::clamp(iz, 0, resolution - 1);
        return h[static_cast<std::size_t>(iz) * resolution + ix];
    };
    for (int iz = z0; iz <= z1; ++iz)
        for (int ix = x0; ix <= x1; ++ix) {
            const float px = origin.x + ix * d, pz = origin.y + iz * d;
            const float f = brushFalloff(std::hypot(px - c.x, pz - c.z), radius) * std::clamp(strength, 0.0f, 1.0f);
            if (f <= 0.0f)
                continue;
            float& h = heights[static_cast<std::size_t>(iz) * resolution + ix];
            switch (mode) {
            case Sculpt::Raise: h += f * 0.5f; break; // up to half a metre per step in the center
            case Sculpt::Lower: h -= f * 0.5f; break;
            case Sculpt::Flatten: h += (flattenHeight - h) * f; break;
            case Sculpt::Smooth: {
                float sum = 0.0f;
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx)
                        sum += at(before, ix + dx, iz + dz);
                h += (sum / 9.0f - h) * f;
                break;
            }
            }
        }
    meshVersion = newResourceId();
}

void Terrain::paintLayer(glm::vec3 c, float radius, float strength, int layer)
{
    if (splat.empty() || layer < 0 || layer >= kLayers || radius <= 0.0f)
        return;
    const float px = size / static_cast<float>(splatResolution);
    const int x0 = std::max(0, static_cast<int>(std::floor((c.x - radius - origin.x) / px)));
    const int x1 = std::min(splatResolution - 1, static_cast<int>(std::ceil((c.x + radius - origin.x) / px)));
    const int z0 = std::max(0, static_cast<int>(std::floor((c.z - radius - origin.y) / px)));
    const int z1 = std::min(splatResolution - 1, static_cast<int>(std::ceil((c.z + radius - origin.y) / px)));
    for (int iz = z0; iz <= z1; ++iz)
        for (int ix = x0; ix <= x1; ++ix) {
            const float wx = origin.x + (ix + 0.5f) * px, wz = origin.y + (iz + 0.5f) * px;
            const float a = brushFalloff(std::hypot(wx - c.x, wz - c.z), radius) * std::clamp(strength, 0.0f, 1.0f);
            if (a <= 0.0f)
                continue;
            std::uint8_t* p = &splat[(static_cast<std::size_t>(iz) * splatResolution + ix) * 4];
            float w[kLayers], sum = 0.0f;
            for (int k = 0; k < kLayers; ++k)
                sum += w[k] = p[k] / 255.0f;
            for (int k = 0; k < kLayers; ++k)
                w[k] = sum > 0.0f ? w[k] / sum : (k == 0 ? 1.0f : 0.0f);
            // Move toward the painted layer; the weights keep summing to one.
            for (int k = 0; k < kLayers; ++k)
                w[k] = k == layer ? w[k] + a * (1.0f - w[k]) : w[k] * (1.0f - a);
            for (int k = 0; k < kLayers; ++k)
                p[k] = static_cast<std::uint8_t>(std::clamp(w[k], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    splatVersion = newResourceId();
}

float Terrain::layerWeight(int layer, float x, float z) const
{
    if (splat.empty() || layer < 0 || layer >= kLayers)
        return 0.0f;
    const float px = size / static_cast<float>(splatResolution);
    const int ix = std::clamp(static_cast<int>((x - origin.x) / px), 0, splatResolution - 1);
    const int iz = std::clamp(static_cast<int>((z - origin.y) / px), 0, splatResolution - 1);
    const std::uint8_t* p = &splat[(static_cast<std::size_t>(iz) * splatResolution + ix) * 4];
    const float sum = static_cast<float>(p[0] + p[1] + p[2] + p[3]);
    return sum > 0.0f ? p[layer] / sum : 0.0f;
}

int Terrain::paintFoliage(glm::vec3 c, float radius, int type, std::mt19937& rng)
{
    if (type < 0 || type >= static_cast<int>(foliageTypes.size()) || radius <= 0.0f)
        return 0;
    const FoliageType& ft = foliageTypes[static_cast<std::size_t>(type)];
    const float density = std::max(ft.density, 1e-4f);
    const float minSpacing = 0.6f / std::sqrt(density);
    std::vector<glm::vec2> near; // same type already in (or close to) the circle
    for (const FoliageInstance& f : foliage)
        if (f.type == type && std::hypot(f.position.x - c.x, f.position.z - c.z) < radius + minSpacing)
            near.emplace_back(f.position.x, f.position.z);
    int inside = 0;
    for (const glm::vec2& p : near)
        inside += std::hypot(p.x - c.x, p.y - c.z) < radius;
    const int target = static_cast<int>(density * 3.14159265f * radius * radius + 0.5f);
    int added = 0;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int attempt = 0; attempt < (target - inside) * 4 && inside + added < target; ++attempt) {
        const float r = radius * std::sqrt(unit(rng)), a = unit(rng) * 6.2831853f;
        const glm::vec2 p(c.x + r * std::cos(a), c.z + r * std::sin(a));
        if (valid() && !contains(p.x, p.y))
            continue;
        const bool crowded = std::any_of(near.begin(), near.end(),
                                         [&](const glm::vec2& q) { return std::hypot(q.x - p.x, q.y - p.y) < minSpacing; });
        if (crowded)
            continue;
        FoliageInstance f;
        f.type = static_cast<std::uint16_t>(type);
        f.position = {p.x, valid() ? heightAt(p.x, p.y) : c.y, p.y};
        f.yawDegrees = unit(rng) * 360.0f;
        f.scale = ft.minScale + (ft.maxScale - ft.minScale) * unit(rng);
        if (ft.alignToSlope && valid()) {
            const glm::vec3 n = normalAt(p.x, p.y);
            f.tiltX = std::atan2(n.z, n.y);
            f.tiltZ = -std::atan2(n.x, n.y);
        }
        foliage.push_back(f);
        near.push_back(p);
        ++added;
    }
    if (added)
        foliageVersion = newResourceId();
    return added;
}

int Terrain::eraseFoliage(glm::vec3 c, float radius, int type)
{
    const std::size_t before = foliage.size();
    foliage.erase(std::remove_if(foliage.begin(), foliage.end(),
                                 [&](const FoliageInstance& f) {
                                     return (type < 0 || f.type == type) &&
                                            std::hypot(f.position.x - c.x, f.position.z - c.z) < radius;
                                 }),
                  foliage.end());
    const int removed = static_cast<int>(before - foliage.size());
    if (removed)
        foliageVersion = newResourceId();
    return removed;
}

void Terrain::removeFoliageType(int type)
{
    if (type < 0 || type >= static_cast<int>(foliageTypes.size()))
        return;
    foliage.erase(std::remove_if(foliage.begin(), foliage.end(), [&](const FoliageInstance& f) { return f.type == type; }),
                  foliage.end());
    for (FoliageInstance& f : foliage)
        if (f.type > type)
            --f.type;
    foliageTypes.erase(foliageTypes.begin() + type);
    foliageVersion = newResourceId();
}

void Terrain::snapFoliageToGround()
{
    if (!valid())
        return;
    for (FoliageInstance& f : foliage)
        f.position.y = heightAt(f.position.x, f.position.z);
    foliageVersion = newResourceId();
}

bool saveTerrainData(const Terrain& t, const std::filesystem::path& file, std::string* error)
{
    FILE* f = openFile(file, "wb");
    if (!f) {
        if (error)
            *error = "Cannot write " + file.string();
        return false;
    }
    const FileHeader h{kMagic, kVersion, t.resolution, t.splatResolution, t.origin.x, t.origin.y, t.size,
                       static_cast<std::uint32_t>(t.foliage.size())};
    bool ok = std::fwrite(&h, sizeof h, 1, f) == 1;
    ok = ok && (t.heights.empty() || std::fwrite(t.heights.data(), sizeof(float), t.heights.size(), f) == t.heights.size());
    ok = ok && (t.splat.empty() || std::fwrite(t.splat.data(), 1, t.splat.size(), f) == t.splat.size());
    for (const FoliageInstance& fi : t.foliage) {
        const float v[7] = {fi.position.x, fi.position.y, fi.position.z, fi.yawDegrees, fi.scale, fi.tiltX, fi.tiltZ};
        const std::uint32_t type = fi.type;
        ok = ok && std::fwrite(v, sizeof v, 1, f) == 1 && std::fwrite(&type, sizeof type, 1, f) == 1;
    }
    ok = std::fclose(f) == 0 && ok;
    if (!ok && error)
        *error = "Write failed: " + file.string();
    return ok;
}

bool loadTerrainData(Terrain& t, const std::filesystem::path& file, std::string* error)
{
    FILE* f = openFile(file, "rb");
    if (!f) {
        if (error)
            *error = "Cannot open " + file.string();
        return false;
    }
    FileHeader h{};
    bool ok = std::fread(&h, sizeof h, 1, f) == 1 && h.magic == kMagic && h.version == kVersion && h.resolution >= 2 &&
              h.resolution <= 4097 && h.splatResolution >= 16 && h.splatResolution <= 4096 && h.size > 0.0f;
    if (ok) {
        t.resolution = h.resolution;
        t.splatResolution = h.splatResolution;
        t.origin = {h.originX, h.originZ};
        t.size = h.size;
        t.heights.resize(static_cast<std::size_t>(h.resolution) * h.resolution);
        t.splat.resize(static_cast<std::size_t>(h.splatResolution) * h.splatResolution * 4);
        ok = std::fread(t.heights.data(), sizeof(float), t.heights.size(), f) == t.heights.size() &&
             std::fread(t.splat.data(), 1, t.splat.size(), f) == t.splat.size();
        t.foliage.clear();
        for (std::uint32_t i = 0; ok && i < h.foliageCount; ++i) {
            float v[7];
            std::uint32_t type = 0;
            ok = std::fread(v, sizeof v, 1, f) == 1 && std::fread(&type, sizeof type, 1, f) == 1;
            if (ok)
                t.foliage.push_back({{v[0], v[1], v[2]}, v[3], v[4], v[5], v[6], static_cast<std::uint16_t>(type)});
        }
    }
    std::fclose(f);
    if (!ok) {
        if (error)
            *error = file.string() + ": not a terrain file or damaged";
        return false;
    }
    t.meshVersion = newResourceId();
    t.splatVersion = newResourceId();
    t.foliageVersion = newResourceId();
    return true;
}

} // namespace engine
