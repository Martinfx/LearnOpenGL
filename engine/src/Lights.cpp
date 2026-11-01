#include "engine/Lights.h"

#include "engine/Scene.h"

#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace engine {

namespace {

std::string prop(const Entity& e, const char* key)
{
    const auto it = e.properties.find(key);
    return it == e.properties.end() ? std::string() : it->second;
}

float propFloat(const Entity& e, const char* key, float fallback)
{
    const std::string s = prop(e, key);
    if (s.empty())
        return fallback;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    return end == s.c_str() || !std::isfinite(v) ? fallback : v;
}

std::string number(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(v));
    return buf;
}

// 0..1 from two integers (SplitMix64 finaliser).
float hash01(std::uint64_t seed, std::int64_t n)
{
    std::uint64_t z = seed + static_cast<std::uint64_t>(n) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<float>(z >> 40) / static_cast<float>(1ull << 24);
}

// Smooth 1D value noise 0..1.
float noise1(std::uint64_t seed, double t)
{
    const double f = std::floor(t);
    const auto i = static_cast<std::int64_t>(f);
    float u = static_cast<float>(t - f);
    u = u * u * (3.0f - 2.0f * u);
    return hash01(seed, i) + (hash01(seed, i + 1) - hash01(seed, i)) * u;
}

} // namespace

const char* lightFlickerName(LightFlicker flicker)
{
    switch (flicker) {
    case LightFlicker::Fire: return "fire";
    case LightFlicker::Fluorescent: return "fluorescent";
    case LightFlicker::Broken: return "broken";
    case LightFlicker::Pulse: return "pulse";
    default: return "none";
    }
}

LightFlicker lightFlickerFromName(const std::string& name)
{
    if (name == "fire")
        return LightFlicker::Fire;
    if (name == "fluorescent")
        return LightFlicker::Fluorescent;
    if (name == "broken")
        return LightFlicker::Broken;
    if (name == "pulse")
        return LightFlicker::Pulse;
    return LightFlicker::None;
}

LightSettings lightSettings(const Entity& e)
{
    LightSettings s;
    s.type = prop(e, "light") == "spot" ? LightType::Spot : LightType::Point;
    s.intensity = std::max(0.0f, propFloat(e, "intensity", s.intensity));
    s.range = std::clamp(propFloat(e, "range", s.range), 0.1f, 200.0f);
    s.outerAngle = std::clamp(propFloat(e, "outer", s.outerAngle), 1.0f, 89.0f);
    s.innerAngle = std::clamp(propFloat(e, "inner", s.innerAngle), 0.0f, s.outerAngle);
    s.pitch = std::clamp(propFloat(e, "pitch", s.pitch), -90.0f, 90.0f);
    s.castShadows = prop(e, "shadows") == "1";
    s.flicker = lightFlickerFromName(prop(e, "flicker"));
    s.flickerSpeed = std::clamp(propFloat(e, "flicker_speed", s.flickerSpeed), 0.05f, 10.0f);
    s.enabled = prop(e, "enabled") != "0";
    return s;
}

void setLightSettings(Entity& e, const LightSettings& s)
{
    const LightSettings d;
    auto set = [&e](const char* key, bool isDefault, const std::string& value) {
        if (isDefault)
            e.properties.erase(key);
        else
            e.properties[key] = value;
    };
    set("light", s.type == d.type, s.type == LightType::Spot ? "spot" : "point");
    set("intensity", s.intensity == d.intensity, number(s.intensity));
    set("range", s.range == d.range, number(s.range));
    set("inner", s.innerAngle == d.innerAngle, number(s.innerAngle));
    set("outer", s.outerAngle == d.outerAngle, number(s.outerAngle));
    set("pitch", s.pitch == d.pitch, number(s.pitch));
    set("shadows", s.castShadows == d.castShadows, s.castShadows ? "1" : "0");
    set("flicker", s.flicker == d.flicker, lightFlickerName(s.flicker));
    set("flicker_speed", s.flickerSpeed == d.flickerSpeed, number(s.flickerSpeed));
    set("enabled", s.enabled == d.enabled, s.enabled ? "1" : "0");
}

float lightFlickerFactor(LightFlicker flicker, float speed, double time, std::uint64_t seed)
{
    const double t = time * static_cast<double>(speed);
    switch (flicker) {
    case LightFlicker::Fire:
        // Two octaves of restless noise, never dark.
        return 0.72f + 0.18f * noise1(seed, t * 6.0) + 0.10f * noise1(seed ^ 0x5bd1e995u, t * 15.0);
    case LightFlicker::Fluorescent: {
        // Steady with a faint hum; now and then (per ~3 s window) a burst of drop-outs.
        const double window = t / 3.0;
        const auto w = static_cast<std::int64_t>(std::floor(window));
        const double inWindow = (window - std::floor(window)) * 3.0; // seconds into the window
        if (hash01(seed, w) < 0.3f && inWindow < 0.5) {
            const auto slot = static_cast<std::int64_t>(std::floor(t * 22.0));
            return hash01(seed ^ 0x1234567u, slot) > 0.45f ? 1.0f : 0.05f;
        }
        return 0.97f + 0.03f * static_cast<float>(std::sin(t * 2.0 * 3.14159265 * 50.0));
    }
    case LightFlicker::Broken: {
        // Mostly dark gaps with stuttering on-periods.
        const auto w = static_cast<std::int64_t>(std::floor(t / 1.2));
        if (hash01(seed, w) < 0.45f)
            return hash01(seed ^ 0x77u, static_cast<std::int64_t>(std::floor(t * 9.0))) > 0.93f ? 0.6f : 0.0f;
        const auto slot = static_cast<std::int64_t>(std::floor(t * 14.0));
        return hash01(seed ^ 0x99u, slot) > 0.3f ? 0.9f + 0.1f * hash01(seed, slot) : 0.1f;
    }
    case LightFlicker::Pulse:
        return 0.55f + 0.45f * static_cast<float>(std::sin(t * 3.14159265 + static_cast<double>(hash01(seed, 0)) * 6.28));
    default:
        return 1.0f;
    }
}

bool evaluateLight(const Entity& e, double time, LocalLight& out)
{
    if (!e.alive)
        return false;
    const LightSettings s = lightSettings(e);
    if (!s.enabled || s.intensity <= 0.0f)
        return false;
    const std::uint64_t seed = e.id != 0 ? e.id : std::hash<std::string>{}(e.name);
    const float flicker = lightFlickerFactor(s.flicker, s.flickerSpeed, time, seed);
    if (flicker <= 0.001f)
        return false;
    out.type = s.type;
    out.position = e.position;
    const float yaw = glm::radians(e.yawDegrees), pitch = glm::radians(s.pitch);
    out.direction = glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
    out.color = glm::pow(glm::max(e.color, glm::vec3(0.0f)), glm::vec3(2.2f)) * s.intensity * flicker;
    out.range = s.range;
    out.innerAngle = s.innerAngle;
    out.outerAngle = s.outerAngle;
    out.castShadows = s.castShadows;
    out.key = seed;
    return true;
}

} // namespace engine
