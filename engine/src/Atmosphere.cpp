#include "engine/Atmosphere.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <algorithm>
#include <cmath>

namespace engine {

namespace {
// Earth, in kilometres (Bruneton / Hillaire reference values).
constexpr float kGround = 6360.0f, kTop = 6460.0f;
constexpr float kEye = 0.002f; // eye height above the ground (km): the player
const glm::vec3 kRayleighScatter(5.802e-3f, 13.558e-3f, 33.1e-3f); // 1/km at sea level
constexpr float kRayleighHeight = 8.0f;
constexpr float kMieScatter = 3.996e-3f, kMieAbsorb = 4.40e-3f, kMieHeight = 1.2f;
const glm::vec3 kOzoneAbsorb(0.650e-3f, 1.881e-3f, 0.085e-3f);
constexpr float kPi = 3.14159265f;

struct Medium {
    glm::vec3 rayleigh, extinction;
    float mie;
};
Medium medium(float altitude, const AtmosphereParams& p)
{
    const float r = std::exp(-altitude / kRayleighHeight) * p.air;
    const float m = std::exp(-altitude / kMieHeight) * p.haze;
    const float o = std::max(0.0f, 1.0f - std::abs(altitude - 25.0f) / 15.0f) * p.ozone;
    Medium md;
    md.rayleigh = kRayleighScatter * r;
    md.mie = kMieScatter * m;
    md.extinction = md.rayleigh + glm::vec3((kMieScatter + kMieAbsorb) * m) + kOzoneAbsorb * o;
    return md;
}

// Distance from `origin` (km, from the planet centre) along `dir` to the sphere of radius r; -1 if missed.
float toSphere(glm::vec3 origin, glm::vec3 dir, float r)
{
    const float b = glm::dot(origin, dir), c = glm::dot(origin, origin) - r * r;
    const float d = b * b - c;
    if (d < 0.0f)
        return -1.0f;
    const float s = std::sqrt(d);
    if (-b - s > 0.0f)
        return -b - s;
    return -b + s > 0.0f ? -b + s : -1.0f;
}

glm::vec3 transmittanceFrom(glm::vec3 pos, glm::vec3 dir, const AtmosphereParams& p)
{
    if (toSphere(pos, dir, kGround) > 0.0f)
        return glm::vec3(0.0f); // below the horizon: the planet is in the way
    const float len = toSphere(pos, dir, kTop);
    if (len <= 0.0f)
        return glm::vec3(1.0f);
    constexpr int kSteps = 12;
    glm::vec3 depth(0.0f);
    float t0 = 0.0f;
    for (int i = 0; i < kSteps; ++i) {
        // Quadratic spacing: dense near the ground where the air is.
        const float t1 = len * std::pow(static_cast<float>(i + 1) / kSteps, 2.0f);
        const glm::vec3 x = pos + dir * (0.5f * (t0 + t1));
        depth += medium(glm::length(x) - kGround, p).extinction * (t1 - t0);
        t0 = t1;
    }
    return glm::exp(-depth);
}

float rayleighPhase(float c) { return 3.0f / (16.0f * kPi) * (1.0f + c * c); }
float miePhase(float c)
{
    constexpr float g = 0.8f; // Cornette-Shanks
    const float k = 3.0f / (8.0f * kPi) * (1.0f - g * g) / (2.0f + g * g);
    return k * (1.0f + c * c) / std::pow(std::max(1.0f + g * g - 2.0f * g * c, 1e-4f), 1.5f);
}

// Single scattering along the ray plus a constant share for the light scattered more than once
// (Hillaire's isotropic multiple-scattering term, folded into one factor that lifts shadowed sky).
void integrate(glm::vec3 view, glm::vec3 toSun, float maxKm, const AtmosphereParams& p, glm::vec3& inscatter, glm::vec3& transmittance)
{
    const glm::vec3 eye(0.0f, kGround + kEye, 0.0f);
    float len = toSphere(eye, view, kTop);
    const float ground = toSphere(eye, view, kGround);
    if (ground > 0.0f)
        len = ground;
    len = std::min(len, maxKm);
    inscatter = glm::vec3(0.0f);
    transmittance = glm::vec3(1.0f);
    if (len <= 0.0f)
        return;
    const float c = glm::dot(view, toSun);
    const float pr = rayleighPhase(c), pm = miePhase(c);
    constexpr int kSteps = 24;
    float t0 = 0.0f;
    for (int i = 0; i < kSteps; ++i) {
        const float t1 = len * std::pow(static_cast<float>(i + 1) / kSteps, 2.0f);
        const float dt = t1 - t0;
        const glm::vec3 x = eye + view * (0.5f * (t0 + t1));
        const float alt = glm::length(x) - kGround;
        const Medium md = medium(alt, p);
        const glm::vec3 sun = transmittanceFrom(x, toSun, p);
        const glm::vec3 scatter = md.rayleigh * pr + glm::vec3(md.mie * pm);
        const glm::vec3 multi = (md.rayleigh + glm::vec3(md.mie)) * (0.08f * glm::clamp(toSun.y * 4.0f + 0.4f, 0.0f, 1.0f));
        const glm::vec3 s = (sun * scatter + multi * glm::vec3(1.0f, 1.0f, 1.0f));
        // Analytic integration over the step (energy conserving for thick steps).
        const glm::vec3 stepT = glm::exp(-md.extinction * dt);
        inscatter += transmittance * s * (glm::vec3(1.0f) - stepT) / glm::max(md.extinction, glm::vec3(1e-7f));
        transmittance *= stepT;
        t0 = t1;
    }
}
} // namespace

glm::vec3 atmosphereTransmittance(glm::vec3 toSun, const AtmosphereParams& p)
{
    return transmittanceFrom(glm::vec3(0.0f, kGround + kEye, 0.0f), glm::normalize(toSun), p);
}

glm::vec3 atmosphereSkyRadiance(glm::vec3 view, glm::vec3 toSun, const AtmosphereParams& p)
{
    glm::vec3 in, t;
    integrate(glm::normalize(view), glm::normalize(toSun), 1e9f, p, in, t);
    return in;
}

void atmosphereAerial(glm::vec3 view, glm::vec3 toSun, float distanceM, const AtmosphereParams& p, glm::vec3& inscatter,
                      glm::vec3& transmittance)
{
    integrate(glm::normalize(view), glm::normalize(toSun), std::max(distanceM, 0.0f) * 0.001f, p, inscatter, transmittance);
}

glm::vec2 skyLutUv(glm::vec3 view, glm::vec3 toSun)
{
    view = glm::normalize(view);
    const float elevation = std::asin(std::clamp(view.y, -1.0f, 1.0f));
    const float v = 0.5f + 0.5f * std::copysign(std::sqrt(std::abs(elevation) / (0.5f * kPi)), elevation);
    const glm::vec2 a(view.x, view.z), s(toSun.x, toSun.z);
    float u = 0.0f;
    if (glm::length(a) > 1e-5f && glm::length(s) > 1e-5f)
        u = std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(s)), -1.0f, 1.0f)) / kPi;
    return {u, v};
}

glm::vec3 skyLutDirection(glm::vec2 uv, glm::vec3 toSun)
{
    const float sv = uv.y * 2.0f - 1.0f;
    const float elevation = std::copysign(sv * sv, sv) * 0.5f * kPi;
    const float azimuth = uv.x * kPi;
    glm::vec2 s(toSun.x, toSun.z);
    s = glm::length(s) > 1e-5f ? glm::normalize(s) : glm::vec2(1.0f, 0.0f);
    const glm::vec2 h(s.x * std::cos(azimuth) - s.y * std::sin(azimuth), s.x * std::sin(azimuth) + s.y * std::cos(azimuth));
    return glm::vec3(h.x * std::cos(elevation), std::sin(elevation), h.y * std::cos(elevation));
}

SkyLut buildSkyLut(glm::vec3 toSun, const AtmosphereParams& p)
{
    toSun = glm::normalize(toSun);
    SkyLut lut;
    lut.texels.resize(static_cast<std::size_t>(SkyLut::kWidth) * SkyLut::kHeight);
    for (int y = 0; y < SkyLut::kHeight; ++y)
        for (int x = 0; x < SkyLut::kWidth; ++x) {
            const glm::vec2 uv((x + 0.5f) / SkyLut::kWidth, (y + 0.5f) / SkyLut::kHeight);
            glm::vec3 dir = skyLutDirection(uv, toSun);
            if (dir.y < 0.0f) // below the horizon: the haze over the (unmodelled) ground, as seen at the horizon
                dir = glm::normalize(glm::vec3(dir.x, 0.0f, dir.z));
            lut.texels[static_cast<std::size_t>(y) * SkyLut::kWidth + x] = atmosphereSkyRadiance(dir, toSun, p);
        }
    return lut;
}

glm::vec3 sampleSkyLut(const SkyLut& lut, glm::vec3 view, glm::vec3 toSun)
{
    if (lut.texels.empty())
        return glm::vec3(0.0f);
    const glm::vec2 uv = skyLutUv(view, toSun);
    const float fx = std::clamp(uv.x * SkyLut::kWidth - 0.5f, 0.0f, SkyLut::kWidth - 1.001f);
    const float fy = std::clamp(uv.y * SkyLut::kHeight - 0.5f, 0.0f, SkyLut::kHeight - 1.001f);
    const int x = static_cast<int>(fx), y = static_cast<int>(fy);
    const float ax = fx - x, ay = fy - y;
    auto at = [&](int i, int j) { return lut.texels[static_cast<std::size_t>(j) * SkyLut::kWidth + i]; };
    return glm::mix(glm::mix(at(x, y), at(x + 1, y), ax), glm::mix(at(x, y + 1), at(x + 1, y + 1), ax), ay);
}

namespace {
std::uint32_t cloudHash(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t seed)
{
    std::uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ z * 0xcb1ab31fu ^ seed * 0x9e3779b9u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}
float cloudRand(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t seed)
{
    return static_cast<float>(cloudHash(x, y, z, seed) & 0xffffffu) / 16777215.0f;
}
int wrapCell(int i, int n)
{
    return ((i % n) + n) % n;
}

// Inverted distance to the nearest feature point (one per cell), cells per tile = n: 1 at a point.
float worley(glm::vec3 p, int n, std::uint32_t seed)
{
    const glm::vec3 q = p * static_cast<float>(n);
    const glm::ivec3 c(static_cast<int>(std::floor(q.x)), static_cast<int>(std::floor(q.y)), static_cast<int>(std::floor(q.z)));
    float best = 1e9f;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const glm::ivec3 k = c + glm::ivec3(dx, dy, dz);
                const auto ux = static_cast<std::uint32_t>(wrapCell(k.x, n)), uy = static_cast<std::uint32_t>(wrapCell(k.y, n)),
                           uz = static_cast<std::uint32_t>(wrapCell(k.z, n));
                const glm::vec3 f = glm::vec3(k) + glm::vec3(cloudRand(ux, uy, uz, seed), cloudRand(ux, uy, uz, seed + 1), cloudRand(ux, uy, uz, seed + 2));
                const glm::vec3 d = f - q;
                best = std::min(best, glm::dot(d, d));
            }
    return 1.0f - std::clamp(std::sqrt(best), 0.0f, 1.0f);
}
float worleyFbm(glm::vec3 p, int n, std::uint32_t seed)
{
    return worley(p, n, seed) * 0.625f + worley(p, n * 2, seed + 7) * 0.25f + worley(p, n * 4, seed + 13) * 0.125f;
}

// Tileable gradient (Perlin) noise, period n cells, about -1..1.
float perlin(glm::vec3 p, int n, std::uint32_t seed)
{
    const glm::vec3 q = p * static_cast<float>(n);
    const glm::vec3 fl = glm::floor(q);
    const glm::ivec3 c(fl);
    const glm::vec3 f = q - fl;
    const glm::vec3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    float v[8];
    for (int i = 0; i < 8; ++i) {
        const glm::ivec3 o(i & 1, (i >> 1) & 1, i >> 2);
        const glm::ivec3 k = c + o;
        const std::uint32_t h = cloudHash(static_cast<std::uint32_t>(wrapCell(k.x, n)), static_cast<std::uint32_t>(wrapCell(k.y, n)),
                                          static_cast<std::uint32_t>(wrapCell(k.z, n)), seed);
        // One of 12 cube-edge gradients.
        static const glm::vec3 grads[12] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                                            {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
        v[i] = glm::dot(grads[h % 12u], f - glm::vec3(o));
    }
    const float x00 = v[0] + (v[1] - v[0]) * u.x, x10 = v[2] + (v[3] - v[2]) * u.x;
    const float x01 = v[4] + (v[5] - v[4]) * u.x, x11 = v[6] + (v[7] - v[6]) * u.x;
    const float y0 = x00 + (x10 - x00) * u.y, y1 = x01 + (x11 - x01) * u.y;
    return y0 + (y1 - y0) * u.z;
}
float remap(float v, float lo, float hi, float a, float b)
{
    return a + (v - lo) / std::max(hi - lo, 1e-5f) * (b - a);
}
} // namespace

std::vector<std::uint8_t> buildCloudNoise(int size)
{
    size = std::max(size, 4);
    const std::size_t count = static_cast<std::size_t>(size) * size * size;
    std::vector<float> v(count * 4);
    for (int z = 0; z < size; ++z)
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const glm::vec3 p = (glm::vec3(x, y, z) + 0.5f) / static_cast<float>(size);
                float pf = 0.0f, amp = 1.0f, norm = 0.0f;
                for (int o = 0; o < 4; ++o) {
                    pf += perlin(p, 4 << o, 101u + static_cast<std::uint32_t>(o)) * amp;
                    norm += amp;
                    amp *= 0.5f;
                }
                pf = std::clamp(pf / norm * 0.5f + 0.5f, 0.0f, 1.0f);
                const float w = worleyFbm(p, 4, 11u);
                float* t = &v[((static_cast<std::size_t>(z) * size + y) * size + x) * 4];
                t[0] = remap(pf, w - 1.0f, 1.0f, 0.0f, 1.0f);
                t[1] = w;
                t[2] = worleyFbm(p, 8, 23u);
                t[3] = worleyFbm(p, 16, 37u);
            }
    // Each channel stretched to the full 0..1 range: the tracer's thresholds (cover) then mean
    // the same for every channel.
    float lo[4] = {1e9f, 1e9f, 1e9f, 1e9f}, hi[4] = {-1e9f, -1e9f, -1e9f, -1e9f};
    for (std::size_t i = 0; i < v.size(); ++i) {
        lo[i & 3] = std::min(lo[i & 3], v[i]);
        hi[i & 3] = std::max(hi[i & 3], v[i]);
    }
    std::vector<std::uint8_t> out(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        const float n = (v[i] - lo[i & 3]) / std::max(hi[i & 3] - lo[i & 3], 1e-5f);
        out[i] = static_cast<std::uint8_t>(std::lround(std::clamp(n, 0.0f, 1.0f) * 255.0f));
    }
    return out;
}

} // namespace engine
