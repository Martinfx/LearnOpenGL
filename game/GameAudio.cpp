#include <engine/Log.h>
#include "GameAudio.h"

#include <cmath>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdio>

void GameAudio::init(const std::filesystem::path& assetDir)
{
    assetDir_ = assetDir;
    std::string error;
    if (!audio_.init(&error))
        LOG_WARN("audio", "%s", error.c_str());
    else
        LOG_INFO("audio", "OpenAL, %s, HRTF %s, sounds from %s\n", audio_.deviceName().c_str(), audio_.hrtf() ? "on" : "off",
                    (assetDir / "sounds").string().c_str());
    if (!engine::loadSurfaces(surfaces_, assetDir / "surfaces.cfg", &error))
        LOG_WARN("audio", "%s (footsteps and impacts use the defaults)\n", error.c_str());
    for (const engine::SurfaceType& t : surfaces_.types) {
        preload(t.footstep);
        preload(t.impact);
    }
}

std::shared_ptr<const engine::SoundDef> GameAudio::get(const std::filesystem::path& sound)
{
    if (sound.empty())
        return nullptr;
    if (auto it = cache_.find(sound); it != cache_.end())
        return it->second;
    std::string error;
    auto def = audio_.loadSound(sound, &error);
    if (!def)
        LOG_WARN("audio", "%s\n", error.c_str());
    cache_.emplace(sound, def); // also caches failures: reported once
    return def;
}

void GameAudio::preload(const std::filesystem::path& sound)
{
    if (auto def = get(sound))
        audio_.preload(*def);
}

void GameAudio::play(const std::filesystem::path& sound, glm::vec3 position, float volume)
{
    if (auto def = get(sound))
        audio_.play(*def, position, volume);
}

void GameAudio::play2D(const std::filesystem::path& sound, float volume)
{
    if (auto def = get(sound))
        audio_.play2D(*def, volume);
}

void GameAudio::playAsset(const std::string& relative, const glm::vec3* position, float volume)
{
    if (relative.empty())
        return;
    std::filesystem::path p = std::filesystem::path(std::u8string(relative.begin(), relative.end()));
    if (p.is_relative())
        p = assetDir_ / p;
    p = p.lexically_normal();
    if (position)
        play(p, *position, volume);
    else
        play2D(p, volume);
}

void GameAudio::startLevel(const engine::Scene& scene)
{
    for (const Source& s : sources_)
        audio_.stop(s.handle);
    sources_.clear();
    audio_.stop(rain_);
    audio_.stop(wind_);
    rain_ = wind_ = 0;
    // Weather loops run all the time; update() sets their volume from the environment.
    const engine::Environment& env = scene.environment;
    auto weather = [&](const std::filesystem::path& custom, const char* standard) -> engine::AudioSystem::Handle {
        auto def = get(custom.empty() ? (assetDir_ / standard).lexically_normal() : custom);
        if (!def)
            return 0;
        const engine::AudioSystem::Handle h = audio_.play2D(*def, 1.0f);
        audio_.setVolume(h, 0.0f);
        return h;
    };
    rain_ = weather(env.rainSound, "sounds/ambient/rain.sound");
    wind_ = weather(env.windSound, "sounds/ambient/wind_storm.sound");
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const engine::Entity& e = scene.entities[i];
        // Sounds scripts will play (e.g. enemy.lua sound_shot) decode now, so the first play is not clipped.
        for (const auto& [key, value] : e.properties)
            if (value.size() > 6 && value.ends_with(".sound")) {
                std::filesystem::path p(std::u8string(value.begin(), value.end()));
                preload((p.is_relative() ? assetDir_ / p : p).lexically_normal());
            }
        if (e.type != "sound")
            continue;
        auto prop = [&](const char* key, const char* fallback) {
            const auto it = e.properties.find(key);
            return it == e.properties.end() ? std::string(fallback) : it->second;
        };
        const std::string rel = prop("sound", "");
        std::filesystem::path p = std::filesystem::path(std::u8string(rel.begin(), rel.end()));
        if (p.empty())
            continue;
        if (p.is_relative())
            p = assetDir_ / p;
        auto def = get(p.lexically_normal());
        if (!def)
            continue;
        Source s;
        s.entity = i;
        s.zone = prop("zone", "0") == "1";
        s.fade = std::strtof(prop("fade", "10").c_str(), nullptr);
        s.volume = std::strtof(prop("volume", "1").c_str(), nullptr);
        const engine::Aabb box = e.bounds();
        // Zones play in the listener's head and are faded by distance in update();
        // point sources are 3D at the centre of the entity box.
        s.handle = s.zone ? audio_.play2D(*def, s.volume) : audio_.play(*def, (box.min + box.max) * 0.5f, s.volume);
        if (s.zone)
            audio_.setVolume(s.handle, 0.0f);
        sources_.push_back(s);
    }
}

void GameAudio::update(float dt, const engine::Scene& scene, glm::vec3 eye, glm::vec3 forward)
{
    audio_.setListener(eye, forward);
    for (Source& s : sources_) {
        if (s.entity >= scene.entities.size())
            continue;
        const engine::Entity& e = scene.entities[s.entity];
        const engine::Aabb box = e.bounds();
        if (s.zone)
            audio_.setVolume(s.handle, engine::zoneGain(box.min, box.max, s.fade, eye), 0.1f);
        else
            audio_.setPosition(s.handle, (box.min + box.max) * 0.5f); // scripts may move it
    }
    // Weather: louder than linear at low intensity so a drizzle is still heard.
    audio_.setVolume(rain_, std::sqrt(std::clamp(scene.environment.rain, 0.0f, 1.0f)), 0.5f);
    audio_.setVolume(wind_, std::sqrt(std::clamp(scene.environment.wind, 0.0f, 1.0f)), 0.5f);
    audio_.update(dt);
}

void GameAudio::step(const engine::PlayerSettings& player, glm::vec3 feet, float volume, const std::string& surface)
{
    if (const engine::SurfaceType* t = surface.empty() ? nullptr : surfaces_.find(surface); t && !t->footstep.empty())
        play(t->footstep, feet, volume * t->stepVolume);
    else
        play(player.footstepSound, feet, volume);
}

void GameAudio::impact(glm::vec3 point, const std::string& surface, const std::filesystem::path& fallback)
{
    if (const engine::SurfaceType* t = surface.empty() ? nullptr : surfaces_.find(surface); t && !t->impact.empty())
        play(t->impact, point);
    else
        play(fallback, point);
}

void GameAudio::walk(const engine::PlayerSettings& player, glm::vec3 feet, glm::vec3 velocity, bool grounded, float dt,
                     const std::string& surface)
{
    const float speed = glm::length(glm::vec3(velocity.x, 0.0f, velocity.z));
    if (grounded && !wasGrounded_) {
        // Landing: louder the harder the fall; small hops are just a step.
        if (fallSpeed_ > 6.0f)
            play(player.landSound, feet, std::clamp(fallSpeed_ / 12.0f, 0.4f, 1.0f));
        else if (fallSpeed_ > 2.0f)
            step(player, feet, 0.8f, surface);
        stride_ = 0.0f;
    }
    if (!grounded)
        fallSpeed_ = std::max(fallSpeed_, -velocity.y);
    else
        fallSpeed_ = 0.0f;
    wasGrounded_ = grounded;
    if (!grounded || speed < 0.5f) {
        stride_ = std::min(stride_, 1.0f); // the first step after stopping comes soon
        return;
    }
    // Longer strides when running: about 2 steps/s walking, 2.7 sprinting.
    const float strideLength = 1.6f + 0.12f * speed;
    stride_ += speed * dt;
    if (stride_ >= strideLength) {
        stride_ -= strideLength;
        step(player, feet, std::clamp(0.55f + speed * 0.06f, 0.5f, 1.0f), surface);
    }
}
