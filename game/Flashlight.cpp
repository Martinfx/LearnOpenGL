#include "Flashlight.h"

#include <engine/Camera.h>
#include <engine/Renderer.h>

#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace {
glm::vec3 linear(glm::vec3 srgb)
{
    return glm::pow(glm::max(srgb, glm::vec3(0.0f)), glm::vec3(2.2f));
}
} // namespace

void Flashlight::reset(const engine::PlayerSettings& settings)
{
    settings_ = settings;
    on_ = false;
    emptied_ = false;
    charge_ = 1.0f;
    placed_ = false;
}

bool Flashlight::toggle()
{
    if (!settings_.flashlight)
        return false;
    if (!on_ && hasBattery() && charge_ <= 0.02f)
        return false;
    on_ = !on_;
    return true;
}

void Flashlight::setCharge(float charge)
{
    charge_ = std::clamp(charge, 0.0f, 1.0f);
}

bool Flashlight::justEmptied()
{
    const bool e = emptied_;
    emptied_ = false;
    return e;
}

void Flashlight::update(float dt, double time, const engine::FpsCamera& camera)
{
    // Held low on the right; the beam trails the view (exponential follow).
    const glm::vec3 forward = camera.forward();
    const glm::vec3 right = camera.right();
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const glm::vec3 target = camera.position + right * 0.18f - up * 0.15f + forward * 0.1f;
    const float follow = settings_.flashlightLag > 0.0f ? 1.0f - std::exp(-dt / settings_.flashlightLag) : 1.0f;
    if (!placed_) {
        position_ = target;
        direction_ = forward;
        placed_ = true;
    }
    position_ = target; // the hand moves with the body; only the aim lags
    direction_ = glm::normalize(glm::mix(direction_, forward, follow));
    if (glm::dot(direction_, forward) < 0.5f) // a fast turn never leaves the beam far behind
        direction_ = glm::normalize(glm::mix(forward, direction_, 0.5f));

    flicker_ = 1.0f;
    if (!hasBattery())
        return;
    if (on_) {
        charge_ -= dt / settings_.flashlightBattery;
        if (charge_ <= 0.0f) {
            charge_ = 0.0f;
            on_ = false;
            emptied_ = true;
        } else if (charge_ < 0.15f) {
            // Nearly empty: dimmer, with a stutter that gets worse.
            const float weak = charge_ / 0.15f;
            flicker_ = (0.35f + 0.65f * weak) *
                       std::max(0.15f, engine::lightFlickerFactor(engine::LightFlicker::Broken, 0.6f + (1.0f - weak), time, 99));
        }
    } else if (settings_.flashlightRecharge > 0.0f) {
        charge_ = std::min(1.0f, charge_ + dt / settings_.flashlightRecharge);
    }
}

void Flashlight::fill(engine::RenderView& view) const
{
    if (!on_ || !settings_.flashlight)
        return;
    engine::LocalLight l;
    l.type = engine::LightType::Spot;
    l.position = position_;
    l.direction = direction_;
    l.color = linear(settings_.flashlightColor) * settings_.flashlightIntensity * flicker_;
    l.range = settings_.flashlightRange;
    l.outerAngle = std::clamp(settings_.flashlightAngle, 2.0f, 80.0f);
    l.innerAngle = l.outerAngle * 0.55f;
    l.castShadows = settings_.flashlightShadows;
    l.cookie = true;
    l.key = 0xF1A5u;
    view.lights.push_back(l);
}

void addCharacterFlashlights(const engine::Scene& scene, engine::RenderView& view)
{
    engine::SkyState sky;
    engine::environmentAt(scene.environment, scene.sunDirection, &sky);
    const bool dark = sky.night > 0.35f;
    for (const engine::Entity& e : scene.entities) {
        if (!e.alive)
            continue;
        // "1" always on, "night" only after dusk (day/night cycle).
        const auto it = e.properties.find("flashlight");
        if (it == e.properties.end() || !(it->second == "1" || (it->second == "night" && dark)))
            continue;
        const float yaw = glm::radians(e.yawDegrees);
        engine::LocalLight l;
        l.type = engine::LightType::Spot;
        l.direction = glm::normalize(glm::vec3(std::cos(yaw), -0.25f, std::sin(yaw)));
        l.position = e.position + glm::vec3(0.0f, e.size.y * 0.8f, 0.0f) + glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw)) * (e.size.x * 0.5f + 0.3f); // past the face (and the marker nose)
        l.color = linear({1.0f, 0.93f, 0.82f}) * 45.0f;
        l.range = 20.0f;
        l.outerAngle = 24.0f;
        l.innerAngle = 13.0f;
        l.castShadows = true;
        l.cookie = true;
        l.key = e.id;
        view.lights.push_back(l);
    }
}
