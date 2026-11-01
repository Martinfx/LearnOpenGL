#include "engine/Probes.h"
#include "engine/Vfs.h"

#include "engine/EntityIO.h"
#include "engine/Scene.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace engine {

namespace {

float numberProp(const Entity& e, const char* key, float fallback)
{
    const auto it = e.properties.find(key);
    return it == e.properties.end() || it->second.empty() ? fallback : std::strtof(it->second.c_str(), nullptr);
}

ProbeShape shapeOf(const Entity& e)
{
    ProbeShape s;
    s.name = e.name;
    s.half = glm::max(e.size * 0.5f, glm::vec3(0.05f));
    s.center = e.position + glm::vec3(0.0f, s.half.y, 0.0f);
    s.yawDegrees = e.yawDegrees;
    s.fade = std::max(0.0f, numberProp(e, "fade", 0.5f));
    return s;
}

constexpr char kMagic[8] = {'F', 'P', 'S', 'P', 'R', 'O', 'B', '2'};

template <class T>
void put(std::ostream& out, const T& v)
{
    out.write(reinterpret_cast<const char*>(&v), sizeof v);
}
template <class T>
bool get(std::istream& in, T& v)
{
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&v), sizeof v));
}
void putShape(std::ostream& out, const ProbeShape& s)
{
    put(out, static_cast<std::uint32_t>(s.name.size()));
    out.write(s.name.data(), static_cast<std::streamsize>(s.name.size()));
    put(out, s.center);
    put(out, s.half);
    put(out, s.yawDegrees);
    put(out, s.fade);
}
bool getShape(std::istream& in, ProbeShape& s)
{
    std::uint32_t n = 0;
    if (!get(in, n) || n > 4096)
        return false;
    s.name.resize(n);
    in.read(s.name.data(), n);
    return get(in, s.center) && get(in, s.half) && get(in, s.yawDegrees) && get(in, s.fade);
}

} // namespace

std::vector<ReflectionProbe> reflectionProbesToBake(const Scene& scene)
{
    std::vector<ReflectionProbe> out;
    for (const Entity& e : scene.entities) {
        if (e.type != "reflection_probe" || !e.alive || !entityEnabled(e))
            continue;
        ReflectionProbe p;
        p.shape = shapeOf(e);
        p.parallax = numberProp(e, "parallax", 1.0f) != 0.0f;
        p.intensity = std::max(0.0f, numberProp(e, "intensity", 1.0f));
        const int size = static_cast<int>(numberProp(e, "resolution", 128.0f));
        p.size = size <= 64 ? 64 : size <= 128 ? 128 : 256;
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<IrradianceVolume> irradianceVolumesToBake(const Scene& scene)
{
    std::vector<IrradianceVolume> out;
    for (const Entity& e : scene.entities) {
        if (e.type != "irradiance_volume" || !e.alive || !entityEnabled(e))
            continue;
        IrradianceVolume v;
        v.shape = shapeOf(e);
        v.intensity = std::max(0.0f, numberProp(e, "intensity", 1.0f));
        const float spacing = std::clamp(numberProp(e, "spacing", 2.0f), 0.25f, 50.0f);
        v.dims = glm::clamp(glm::ivec3(glm::round(v.shape.half * 2.0f / spacing)) + 1, glm::ivec3(2), glm::ivec3(64));
        out.push_back(std::move(v));
    }
    return out;
}

glm::vec3 irradiancePoint(const IrradianceVolume& volume, glm::ivec3 cell)
{
    // Points span the box from wall to wall (inset a little so they are not inside the walls).
    const glm::vec3 t = glm::vec3(cell) / glm::vec3(glm::max(volume.dims - 1, glm::ivec3(1)));
    const glm::vec3 inset = glm::min(volume.shape.half * 0.5f, glm::vec3(0.25f));
    const glm::vec3 local = -volume.shape.half + inset + t * (volume.shape.half - inset) * 2.0f;
    const float c = std::cos(glm::radians(volume.shape.yawDegrees)), s = std::sin(glm::radians(volume.shape.yawDegrees));
    // Box frame -> world (the entity box is turned by -yaw about +Y).
    return volume.shape.center + glm::vec3(c * local.x - s * local.z, local.y, s * local.x + c * local.z);
}

glm::vec3 cubeTexelDirection(int face, float s, float t)
{
    const float sc = s * 2.0f - 1.0f, tc = t * 2.0f - 1.0f;
    switch (face) {
    case 0: return {1.0f, -tc, -sc};
    case 1: return {-1.0f, -tc, sc};
    case 2: return {sc, 1.0f, tc};
    case 3: return {sc, -1.0f, -tc};
    case 4: return {sc, -tc, 1.0f};
    default: return {-sc, -tc, -1.0f};
    }
}

void projectIrradiance(const std::vector<float>& faces, int size, glm::vec4 out[3])
{
    // Radiance -> SH L1 (Y00 = 0.282095, Y1m = 0.488603 * {y, z, x}), weighted by texel solid angle;
    // then the cosine lobe (A0 = pi, A1 = 2pi/3) divided by pi: radiance a Lambert surface reflects
    // per unit albedo, like the environment map's blurriest level the shader uses outdoors.
    glm::vec3 l00(0.0f), l1y(0.0f), l1z(0.0f), l1x(0.0f);
    float total = 0.0f;
    const std::size_t faceSize = static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 3;
    for (int f = 0; f < 6; ++f)
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const glm::vec3 d = cubeTexelDirection(f, (static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                                       (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
                const float len2 = glm::dot(d, d);
                const float weight = 1.0f / (len2 * std::sqrt(len2)); // solid angle up to a constant
                const glm::vec3 n = d / std::sqrt(len2);
                const float* p = faces.data() + static_cast<std::size_t>(f) * faceSize + (static_cast<std::size_t>(y) * size + x) * 3;
                const glm::vec3 c(std::max(p[0], 0.0f), std::max(p[1], 0.0f), std::max(p[2], 0.0f));
                l00 += c * weight;
                l1y += c * (n.y * weight);
                l1z += c * (n.z * weight);
                l1x += c * (n.x * weight);
                total += weight;
            }
    const float norm = 4.0f * 3.14159265f / std::max(total, 1e-6f); // weights sum to 4 pi
    const float k0 = 0.282095f * 0.282095f * norm, k1 = (2.0f / 3.0f) * 0.488603f * 0.488603f * norm;
    for (int ch = 0; ch < 3; ++ch)
        out[ch] = glm::vec4(l00[ch] * k0, l1y[ch] * k1, l1z[ch] * k1, l1x[ch] * k1);
}

glm::vec3 evalIrradiance(const glm::vec4 sh[3], glm::vec3 n)
{
    glm::vec3 r;
    for (int ch = 0; ch < 3; ++ch)
        r[ch] = std::max(0.0f, sh[ch].x + sh[ch].y * n.y + sh[ch].z * n.z + sh[ch].w * n.x);
    return r;
}

std::filesystem::path probeFile(const std::filesystem::path& level)
{
    std::filesystem::path p = level;
    p.replace_extension(".probes");
    return p;
}

bool saveProbes(const ProbeSet& probes, const std::filesystem::path& file, std::string* error)
{
    const std::filesystem::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary);
        if (!out) {
            if (error)
                *error = "Cannot write " + file.string();
            return false;
        }
        out.write(kMagic, sizeof kMagic);
        put(out, static_cast<std::uint32_t>(probes.reflections.size()));
        for (const ReflectionProbe& p : probes.reflections) {
            putShape(out, p.shape);
            put(out, static_cast<std::uint8_t>(p.parallax ? 1 : 0));
            put(out, p.intensity);
            put(out, static_cast<std::uint32_t>(p.size));
            put(out, static_cast<std::uint32_t>(p.faces.size()));
            out.write(reinterpret_cast<const char*>(p.faces.data()), static_cast<std::streamsize>(p.faces.size() * 4));
        }
        put(out, static_cast<std::uint32_t>(probes.volumes.size()));
        for (const IrradianceVolume& v : probes.volumes) {
            putShape(out, v.shape);
            put(out, v.intensity);
            put(out, v.dims);
            put(out, static_cast<std::uint32_t>(v.sh.size()));
            out.write(reinterpret_cast<const char*>(v.sh.data()), static_cast<std::streamsize>(v.sh.size() * sizeof(glm::vec4)));
        }
        if (!out) {
            if (error)
                *error = "Cannot write " + file.string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, file, ec);
    if (ec && error)
        *error = "Cannot replace " + file.string();
    return !ec;
}

bool loadProbes(ProbeSet& probes, const std::filesystem::path& file, std::string* error)
{
    engine::VfsStream in(file);
    auto fail = [&](const char* why) {
        if (error)
            *error = file.string() + ": " + why;
        return false;
    };
    char magic[8] = {};
    if (!in.read(magic, sizeof magic) || std::memcmp(magic, kMagic, sizeof magic) != 0)
        return fail("not a probe file");
    ProbeSet result;
    std::uint32_t count = 0;
    if (!get(in, count) || count > 4096)
        return fail("bad reflection probe count");
    for (std::uint32_t i = 0; i < count; ++i) {
        ReflectionProbe p;
        std::uint8_t parallax = 0;
        std::uint32_t size = 0, n = 0;
        if (!getShape(in, p.shape) || !get(in, parallax) || !get(in, p.intensity) || !get(in, size) || !get(in, n) || size > 1024 ||
            n != 6u * size * size)
            return fail("bad reflection probe");
        p.parallax = parallax != 0;
        p.size = static_cast<int>(size);
        p.faces.resize(n);
        if (!in.read(reinterpret_cast<char*>(p.faces.data()), static_cast<std::streamsize>(n) * 4))
            return fail("truncated reflection probe");
        result.reflections.push_back(std::move(p));
    }
    if (!get(in, count) || count > 4096)
        return fail("bad irradiance volume count");
    for (std::uint32_t i = 0; i < count; ++i) {
        IrradianceVolume v;
        std::uint32_t n = 0;
        if (!getShape(in, v.shape) || !get(in, v.intensity) || !get(in, v.dims) || !get(in, n) || glm::any(glm::lessThan(v.dims, glm::ivec3(1))) ||
            glm::any(glm::greaterThan(v.dims, glm::ivec3(256))) || n != 3u * static_cast<std::uint32_t>(v.dims.x * v.dims.y * v.dims.z))
            return fail("bad irradiance volume");
        v.sh.resize(n);
        if (!in.read(reinterpret_cast<char*>(v.sh.data()), static_cast<std::streamsize>(n * sizeof(glm::vec4))))
            return fail("truncated irradiance volume");
        result.volumes.push_back(std::move(v));
    }
    probes = std::move(result);
    return true;
}

} // namespace engine
