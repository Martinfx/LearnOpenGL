#pragma once
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>

namespace engine {

// Physically based sky (after Hillaire, "A Scalable and Production Ready Sky and Atmosphere
// Rendering Technique", 2020): an Earth-like atmosphere of Rayleigh scattering (air, blue),
// Mie scattering (haze, white glow around the sun) and ozone absorption, seen from the ground.
// Evaluated on the CPU into a small sky-view table whenever the sun moves; the shaders read
// the table for the sky, and the same model gives the sunlight's colour (red at sunset), the
// sky light and the colour distant things fade to (aerial perspective).
struct AtmosphereParams {
    float haze = 1.0f;    // Mie density multiplier: 0.3 crisp, 1 normal, 3-8 hazy / smoggy
    float ozone = 1.0f;   // ozone multiplier (the blue of twilight)
    float air = 1.0f;     // Rayleigh density multiplier
};

// Linear RGB, for a sun illuminance of 1.
glm::vec3 atmosphereTransmittance(glm::vec3 toSun, const AtmosphereParams& p);           // sunlight reaching the ground
glm::vec3 atmosphereSkyRadiance(glm::vec3 view, glm::vec3 toSun, const AtmosphereParams& p); // without the sun disk
// Colour the air between the eye and a point `distanceM` away along `view` adds (in-scatter, for a
// sun illuminance of 1) and how much of the point is left (transmittance), at ground level.
void atmosphereAerial(glm::vec3 view, glm::vec3 toSun, float distanceM, const AtmosphereParams& p, glm::vec3& inscatter,
                      glm::vec3& transmittance);

// Sky-view table: azimuth relative to the sun (u, 0 toward the sun .. 1 away) by elevation
// (v, non-linear: most rows near the horizon). Shared mapping with common.glsl skyLutUv().
struct SkyLut {
    static constexpr int kWidth = 64, kHeight = 48;
    std::vector<glm::vec3> texels; // row-major, kWidth * kHeight, linear RGB radiance
};
SkyLut buildSkyLut(glm::vec3 toSun, const AtmosphereParams& p);
glm::vec2 skyLutUv(glm::vec3 view, glm::vec3 toSun);
glm::vec3 skyLutDirection(glm::vec2 uv, glm::vec3 toSun);
glm::vec3 sampleSkyLut(const SkyLut& lut, glm::vec3 view, glm::vec3 toSun); // bilinear, for CPU uses

} // namespace engine
