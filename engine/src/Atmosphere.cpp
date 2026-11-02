#include "engine/Atmosphere.h"

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

} // namespace engine
