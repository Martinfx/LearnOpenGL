#include "engine/TimeOfDay.h"
#include "engine/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace engine {

namespace {
constexpr float kPi = 3.14159265f;

float radians(float degrees) { return degrees * kPi / 180.0f; }

float wrapHour(float hour)
{
    hour = std::fmod(hour, 24.0f);
    return hour < 0.0f ? hour + 24.0f : hour;
}

float smooth(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

glm::vec3 toLinear(glm::vec3 c) { return glm::pow(glm::max(c, glm::vec3(0.0f)), glm::vec3(2.2f)); }
glm::vec3 toSrgb(glm::vec3 c) { return glm::pow(glm::max(c, glm::vec3(0.0f)), glm::vec3(1.0f / 2.2f)); }

// Direction toward a body at declination `decl` and hour angle `hourAngle` (radians)
// seen from `latitude`: east +X, up +Y, north -Z, then turned by `north` degrees.
glm::vec3 skyDirection(float latitude, float decl, float hourAngle, float north)
{
    const float phi = radians(latitude);
    const float east = -std::cos(decl) * std::sin(hourAngle);
    const float northward = std::cos(phi) * std::sin(decl) - std::sin(phi) * std::cos(decl) * std::cos(hourAngle);
    const float up = std::sin(phi) * std::sin(decl) + std::cos(phi) * std::cos(decl) * std::cos(hourAngle);
    const float a = radians(north);
    const glm::vec3 local(east, up, -northward);
    return glm::normalize(glm::vec3(local.x * std::cos(a) - local.z * std::sin(a), local.y,
                                    local.x * std::sin(a) + local.z * std::cos(a)));
}

float solarDeclination(int dayOfYear)
{
    return radians(-23.44f) * std::cos(2.0f * kPi / 365.0f * static_cast<float>(dayOfYear + 10));
}
} // namespace

glm::vec3 sunPosition(const TimeOfDay& t, float hour)
{
    return skyDirection(t.latitude, solarDeclination(t.dayOfYear), radians(15.0f * (wrapHour(hour) - 12.0f)), t.north);
}

glm::vec3 moonPosition(const TimeOfDay& t, float hour)
{
    // The moon trails the sun by its phase (full moon: opposite the sun, rising at
    // sunset) and stands low in summer / high in winter when full.
    const float phase = t.moonPhase - std::floor(t.moonPhase);
    const float decl = solarDeclination(t.dayOfYear) * std::cos(2.0f * kPi * phase);
    return skyDirection(t.latitude, decl, radians(15.0f * (wrapHour(hour) - 12.0f - 24.0f * phase)), t.north);
}

WeatherKey weatherAt(const std::vector<WeatherKey>& keys, float hour)
{
    hour = wrapHour(hour);
    if (keys.size() == 1)
        return keys.front();
    std::vector<WeatherKey> sorted = keys;
    std::sort(sorted.begin(), sorted.end(), [](const WeatherKey& a, const WeatherKey& b) { return a.hour < b.hour; });
    // The key at or before `hour` and the next one, wrapping around midnight.
    std::size_t next = 0;
    while (next < sorted.size() && sorted[next].hour <= hour)
        ++next;
    const WeatherKey& b = sorted[next % sorted.size()];
    const WeatherKey& a = sorted[(next + sorted.size() - 1) % sorted.size()];
    float span = b.hour - a.hour;
    float into = hour - a.hour;
    if (span <= 0.0f)
        span += 24.0f;
    if (into < 0.0f)
        into += 24.0f;
    const float f = span > 1e-4f ? std::clamp(into / span, 0.0f, 1.0f) : 0.0f;
    const float s = f * f * (3.0f - 2.0f * f);
    WeatherKey w;
    w.hour = hour;
    w.cloudCover = glm::mix(a.cloudCover, b.cloudCover, s);
    w.fogDensity = glm::mix(a.fogDensity, b.fogDensity, s);
    w.rain = glm::mix(a.rain, b.rain, s);
    w.wind = glm::mix(a.wind, b.wind, s);
    w.exposure = glm::mix(a.exposure, b.exposure, s);
    w.storm = glm::mix(a.storm, b.storm, s);
    return w;
}

Environment environmentAt(const Environment& base, glm::vec3 authoredSun, SkyState* sky)
{
    SkyState state;
    Environment e = base;
    const TimeOfDay& t = base.time;
    state.cloudShadows = std::clamp(t.cloudShadows, 0.0f, 1.0f);
    if (!t.enabled) {
        state.lightDirection = glm::dot(authoredSun, authoredSun) > 1e-8f ? glm::normalize(authoredSun) : glm::vec3(0, -1, 0);
        state.moon = glm::normalize(glm::vec3(0.3f, 0.5f, 0.8f));
        if (sky)
            *sky = state;
        return e;
    }
    const float hour = wrapHour(t.hour);
    if (!t.keys.empty()) {
        const WeatherKey w = weatherAt(t.keys, hour);
        e.cloudCover = w.cloudCover;
        e.fogDensity = w.fogDensity;
        e.rain = w.rain;
        e.wind = w.wind;
        e.exposure = w.exposure;
        e.storm = w.storm;
    }
    const glm::vec3 sun = sunPosition(t, hour);
    const glm::vec3 moon = moonPosition(t, hour);
    const float sinAlt = sun.y;
    const float day = smooth(-0.05f, 0.20f, sinAlt);     // full daylight from ~12 degrees up
    const float dusk = smooth(-0.21f, -0.02f, sinAlt);   // twilight sky until ~12 degrees below
    state.night = 1.0f - dusk;
    state.twilight = (1.0f - smooth(0.0f, 0.30f, sinAlt)) * smooth(-0.18f, -0.02f, sinAlt);
    state.moon = moon;
    const float phase = t.moonPhase - std::floor(t.moonPhase);
    state.moonLight = 0.5f - 0.5f * std::cos(2.0f * kPi * phase); // 0 new .. 1 full
    state.stars = state.night * std::max(0.0f, t.nightBrightness > 0.0f ? 1.0f : 0.0f);
    state.cloudShadows *= day;

    // Sunlight through the atmosphere: the longer the path (air mass) the redder and dimmer.
    const float altDeg = std::asin(std::clamp(sinAlt, -1.0f, 1.0f)) * 180.0f / kPi;
    const float airMass = 1.0f / (std::max(sinAlt, 0.0f) + 0.50572f * std::pow(std::max(altDeg, 0.0f) + 6.07995f, -1.6364f));
    const glm::vec3 tau(0.025f, 0.06f, 0.14f);
    const glm::vec3 transmit = glm::exp(-tau * (airMass - 1.0f));
    const float peak = std::max({transmit.r, transmit.g, transmit.b, 1e-4f});
    const float luminance = glm::dot(transmit, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    const float nightLevel = std::max(0.0f, t.nightBrightness);
    const float moonUp = smooth(0.0f, 0.12f, moon.y);
    if (sinAlt > -0.03f) {
        state.lightDirection = -sun;
        e.sunColor = toSrgb(toLinear(base.sunColor) * transmit / peak);
        e.sunIntensity = base.sunIntensity * luminance * smooth(-0.02f, 0.06f, sinAlt);
    } else {
        // Night: the moon is the shadow-casting light (blue, faint; none when it is down).
        state.lightDirection = moon.y > 0.0f ? -moon : glm::vec3(0.0f, -1.0f, 0.0f);
        e.sunColor = glm::vec3(0.62f, 0.72f, 1.0f);
        e.sunIntensity = 0.16f * state.moonLight * nightLevel * moonUp * state.night;
    }

    // Sky, ground and fog: authored colors by day, warm twilight, near black at night
    // (lifted a little by the moon). Blended in linear space.
    const float moonSky = (0.25f + 0.75f * state.moonLight * moonUp) * nightLevel;
    const glm::vec3 nightZenith = toLinear(glm::vec3(0.07f, 0.09f, 0.15f)) * moonSky;
    const glm::vec3 nightHorizon = toLinear(glm::vec3(0.09f, 0.10f, 0.14f)) * moonSky;
    const glm::vec3 twilightZenith = toLinear(glm::vec3(0.17f, 0.21f, 0.36f));
    const glm::vec3 twilightHorizon = toLinear(glm::vec3(0.60f, 0.45f, 0.40f));
    auto blend = [&](glm::vec3 night, glm::vec3 twilight, glm::vec3 daylight) {
        return toSrgb(glm::mix(glm::mix(night, twilight, dusk), toLinear(daylight), day));
    };
    e.skyZenith = blend(nightZenith, twilightZenith, base.skyZenith);
    e.skyHorizon = blend(nightHorizon, twilightHorizon, base.skyHorizon);
    const float groundLight = glm::mix(glm::mix(0.05f * moonSky, 0.35f, dusk), 1.0f, day);
    e.groundColor = toSrgb(toLinear(base.groundColor) * groundLight);
    const glm::vec3 twilightFog = toLinear(base.fogColor) * glm::vec3(0.75f, 0.62f, 0.58f);
    e.fogColor = blend(nightHorizon * 0.8f, twilightFog, base.fogColor);
    // Eyes adapt: a little more exposure at night (still dark: about 1.5 stops).
    e.exposure *= 1.0f + 1.8f * state.night;
    if (sky)
        *sky = state;
    return e;
}

const char* formatHour(float hour, char (&buffer)[8])
{
    const int minutes = static_cast<int>(std::round(wrapHour(hour) * 60.0f)) % (24 * 60);
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes / 60, minutes % 60);
    return buffer;
}

} // namespace engine
