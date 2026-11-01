#pragma once
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

namespace engine {

struct Entity;

// Local light sources: point lights (bulbs, fires) and spot lights (lamps,
// flashlight). In a level they are entities of type "light" whose settings are
// entity properties (edited in the editor's Details panel); the game adds its own
// (flashlight, muzzle flash) through RenderView::lights.
enum class LightType { Point, Spot };

// How the brightness changes over time.
enum class LightFlicker {
    None,
    Fire,        // soft, restless (campfire, candle, burning barrel)
    Fluorescent, // steady with short drop-outs and a buzz on start (tube lights)
    Broken,      // long dark gaps and stuttering (damaged lamps)
    Pulse,       // slow sine (alarm, anomaly glow)
};

// Settings of a "light" entity (its properties; missing keys use these defaults).
struct LightSettings {
    LightType type = LightType::Point;
    float intensity = 15.0f;     // at 1 m: color * intensity / 2 (falls off with distance squared)
    float range = 8.0f;          // m: no light beyond this (smooth cut-off)
    float innerAngle = 25.0f;    // spot: degrees from the axis at full brightness
    float outerAngle = 40.0f;    // spot: degrees from the axis where the light ends
    float pitch = -90.0f;        // spot: degrees, -90 = straight down; direction yaw is the entity's
    bool castShadows = false;
    LightFlicker flicker = LightFlicker::None;
    float flickerSpeed = 1.0f;   // multiplier on the pattern's tempo
    bool enabled = true;         // property "enabled" = "0": switched off
};

LightSettings lightSettings(const Entity& entity);
void setLightSettings(Entity& entity, const LightSettings& settings); // writes only non-default keys
const char* lightFlickerName(LightFlicker flicker);
LightFlicker lightFlickerFromName(const std::string& name);

// One light as the renderer uses it (world space, linear color with intensity).
struct LocalLight {
    LightType type = LightType::Point;
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f}; // spot axis (unit)
    glm::vec3 color{1.0f};                  // linear RGB * intensity
    float range = 8.0f;
    float innerAngle = 25.0f, outerAngle = 40.0f; // degrees
    bool castShadows = false;
    std::uint64_t key = 0; // stable per light (keeps its shadow slot between frames)
};

// Brightness factor 0..1 of a flicker pattern at `time` seconds; `seed` gives
// each light its own rhythm.
float lightFlickerFactor(LightFlicker flicker, float speed, double time, std::uint64_t seed);

// The light of a "light" entity at `time`; false when it is off (disabled, dead or flickered out).
bool evaluateLight(const Entity& entity, double time, LocalLight& out);

} // namespace engine
