#include <engine/Log.h>
#include "GameAudio.h"

#include <engine/EntityIO.h>

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

engine::AudioSystem::Handle GameAudio::start(const std::filesystem::path& sound, const glm::vec3* position, float volume)
{
    auto def = get(sound);
    if (!def)
        return 0;
    return position ? audio_.play(*def, *position, volume) : audio_.play2D(*def, volume);
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
    startAmbience(scene);
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
        s.on = s.zone || (e.alive && engine::entityEnabled(e));
        if (!s.on || s.zone)
            audio_.setVolume(s.handle, 0.0f); // switched off in the level: waits for an Enable input
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
        // Switched by entity I/O (Enable / Disable / Kill): fades out instead of stopping.
        const bool on = e.alive && engine::entityEnabled(e);
        if (s.zone) {
            audio_.setVolume(s.handle, on ? engine::zoneGain(box.min, box.max, s.fade, eye) : 0.0f, 0.1f);
        } else {
            audio_.setPosition(s.handle, (box.min + box.max) * 0.5f); // scripts may move it
            if (on != s.on) {
                audio_.setVolume(s.handle, on ? s.volume : 0.0f, 0.3f);
                s.on = on;
            }
        }
    }
    updateReverb(scene, eye);
    updateAmbience(dt, scene, eye);
    // Weather: louder than linear at low intensity so a drizzle is still heard.
    const engine::Environment env = engine::environmentAt(scene.environment, scene.sunDirection); // weather keys by hour
    shelterMix_ += ((sheltered_ ? 1.0f : 0.0f) - shelterMix_) * std::min(1.0f, dt * 3.0f);
    audio_.setVolume(rain_, std::sqrt(std::clamp(env.rain, 0.0f, 1.0f)) * (1.0f - 0.6f * shelterMix_), 0.5f);
    audio_.setVolume(wind_, std::sqrt(std::clamp(env.wind, 0.0f, 1.0f)) * (1.0f - 0.45f * shelterMix_), 0.5f);
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

void GameAudio::updateReverb(const engine::Scene& scene, glm::vec3 eye)
{
    // Weighted candidates: every reverb zone around the listener (1 inside, fading to 0 at
    // `fade` metres outside) and the level's own reverb for what the zones leave over.
    struct Candidate {
        std::string preset;
        float weight;
        float amount;
    };
    std::vector<Candidate> found;
    float zones = 0.0f;
    for (const engine::Entity& e : scene.entities) {
        if (e.type != "reverb" || !e.alive || !engine::entityEnabled(e))
            continue;
        auto prop = [&e](const char* key, const char* fallback) {
            const auto it = e.properties.find(key);
            return it == e.properties.end() || it->second.empty() ? std::string(fallback) : it->second;
        };
        const engine::Aabb box = e.bounds();
        const float w = engine::zoneGain(box.min, box.max, std::strtof(prop("fade", "2").c_str(), nullptr), eye);
        if (w <= 0.0f)
            continue;
        found.push_back({prop("preset", "room"), w, std::clamp(std::strtof(prop("amount", "1").c_str(), nullptr), 0.0f, 1.0f)});
        zones = std::max(zones, w);
    }
    const engine::Environment& env = scene.environment;
    if (zones < 1.0f && env.reverb != "none")
        found.push_back({env.reverb, 1.0f - zones, env.reverbAmount});
    std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return a.weight > b.weight; });
    std::string a = "none", b = "none";
    float amountA = 0.0f, amountB = 0.0f;
    if (!found.empty()) {
        a = found[0].preset;
        amountA = found[0].weight * found[0].amount;
    }
    if (found.size() > 1) {
        b = found[1].preset;
        amountB = found[1].weight * found[1].amount;
    }
    // Keep a preset in the slot it already has, so a crossfade only changes the amounts.
    if (a != audio_.reverb(0) && (a == audio_.reverb(1) || b == audio_.reverb(0))) {
        std::swap(a, b);
        std::swap(amountA, amountB);
    }
    if (a != audio_.reverb(0) || b != audio_.reverb(1))
        LOG_INFO("audio", "reverb: %s %.2f + %s %.2f", a.c_str(), amountA, b.c_str(), amountB);
    audio_.setReverbMix(a, amountA, b, amountB);
}

namespace {

std::string propertyOf(const engine::Entity& e, const char* key, const char* fallback)
{
    const auto it = e.properties.find(key);
    return it == e.properties.end() || it->second.empty() ? std::string(fallback) : it->second;
}

} // namespace

void GameAudio::startAmbience(const engine::Scene& scene)
{
    for (const Ambience& a : ambiences_)
        for (engine::AudioSystem::Handle h : a.layers)
            audio_.stop(h, 1.0f);
    ambiences_.clear();
    auto add = [this](std::filesystem::path file) {
        if (file.empty())
            return;
        if (file.is_relative())
            file = assetDir_ / file;
        file = file.lexically_normal();
        for (const Ambience& a : ambiences_)
            if (a.file == file)
                return;
        Ambience a;
        a.file = file;
        std::string error;
        if (!engine::loadAmbienceFile(a.set, file, &error)) {
            LOG_WARN("audio", "%s", error.c_str());
            return;
        }
        // Every layer loops from the start, silent until its set is heard (no gap when crossing a zone).
        for (const engine::AmbienceLayer& l : a.set.layers) {
            const engine::AudioSystem::Handle h = start(l.sound, nullptr, 1.0f);
            audio_.setVolume(h, 0.0f);
            a.layers.push_back(h);
        }
        for (const engine::AmbienceRandom& r : a.set.randoms) {
            preload(r.sound);
            // The first try comes sooner than the usual interval, so a level does not start mute.
            a.timers.push_back(std::uniform_real_distribution<float>(0.3f, 1.0f)(rng_) * r.minInterval);
        }
        ambiences_.push_back(std::move(a));
    };
    add(scene.environment.ambience);
    for (const engine::Entity& e : scene.entities)
        if (e.type == "ambience") {
            const std::string set = propertyOf(e, "set", "");
            add(std::filesystem::path(std::u8string(set.begin(), set.end())));
        }
}

void GameAudio::updateAmbience(float dt, const engine::Scene& scene, glm::vec3 eye)
{
    if (ambiences_.empty())
        return;
    // Weights: each zone 1 inside, fading to 0 at `fade` metres outside; the level's set
    // fills what the zones leave (indoors a zone may still let `outside` of it through).
    for (Ambience& a : ambiences_)
        a.weight = 0.0f;
    auto find = [this](std::filesystem::path file) -> Ambience* {
        if (file.empty())
            return nullptr;
        if (file.is_relative())
            file = assetDir_ / file;
        file = file.lexically_normal();
        for (Ambience& a : ambiences_)
            if (a.file == file)
                return &a;
        return nullptr;
    };
    float covered = 0.0f;
    for (const engine::Entity& e : scene.entities) {
        if (e.type != "ambience" || !e.alive || !engine::entityEnabled(e))
            continue;
        const engine::Aabb box = e.bounds();
        const float w = engine::zoneGain(box.min, box.max, std::strtof(propertyOf(e, "fade", "4").c_str(), nullptr), eye);
        if (w <= 0.0f)
            continue;
        const float outside = std::clamp(std::strtof(propertyOf(e, "outside", "0").c_str(), nullptr), 0.0f, 1.0f);
        covered = std::max(covered, w * (1.0f - outside));
        const std::string set = propertyOf(e, "set", "");
        if (Ambience* a = find(std::filesystem::path(std::u8string(set.begin(), set.end()))))
            a->weight = std::max(a->weight, w);
    }
    if (Ambience* level = find(scene.environment.ambience))
        level->weight = std::max(level->weight, 1.0f - covered);

    engine::SkyState sky;
    engine::environmentAt(scene.environment, scene.sunDirection, &sky);
    const float night = scene.environment.time.enabled ? sky.night : engine::nightFactor(scene.sunDirection);
    for (Ambience& a : ambiences_) {
        for (std::size_t i = 0; i < a.layers.size(); ++i)
            audio_.setVolume(a.layers[i], a.weight * engine::ambienceLayerVolume(a.set.layers[i], night), 0.3f);
        if (a.weight < 0.05f)
            continue;
        for (std::size_t i = 0; i < a.set.randoms.size(); ++i) {
            const engine::AmbienceRandom& r = a.set.randoms[i];
            a.timers[i] -= dt;
            if (a.timers[i] > 0.0f)
                continue;
            a.timers[i] = std::uniform_real_distribution<float>(r.minInterval, r.maxInterval)(rng_);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            if (unit(rng_) >= engine::ambienceChance(r, night) * std::min(1.0f, a.weight * 1.5f))
                continue;
            // Somewhere around the listener: a direction, a distance, a height.
            const float angle = unit(rng_) * 6.2831853f;
            const float distance = r.minDistance + (r.maxDistance - r.minDistance) * unit(rng_);
            const float height = r.height * (0.5f + unit(rng_));
            const glm::vec3 at = eye + glm::vec3(std::cos(angle) * distance, height, std::sin(angle) * distance);
            play(r.sound, at, a.weight);
            if (r.duck > 0.0f) // a far event the ambience makes room for
                audio_.duck(engine::AudioGroup::Ambient, 1.0f - std::clamp(r.duck, 0.0f, 1.0f), 1.5f, 3.0f);
        }
    }
}
