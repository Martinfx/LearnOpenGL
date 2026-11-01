#include "GameWeather.h"
#include "GameAudio.h"

#include <engine/TimeOfDay.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

void GameWeather::startLevel(const engine::Scene& scene)
{
    const engine::Environment env = engine::environmentAt(scene.environment, scene.sunDirection);
    wetness_ = std::clamp(env.rain * 1.5f, 0.0f, 1.0f);
    flash_ = 0.0f;
    pulses_.clear();
    thunder_.clear();
    sinceStrike_ = 100.0f;
    nextStrike_ = std::uniform_real_distribution<float>(4.0f, 12.0f)(rng_);
}

void GameWeather::update(float dt, const engine::Scene& scene, glm::vec3 eye, bool sheltered, GameAudio& audio)
{
    const engine::Environment env = engine::environmentAt(scene.environment, scene.sunDirection);
    // Soaked in about a minute of heavy rain, dry again in about five.
    const float rain = std::clamp(env.rain, 0.0f, 1.0f);
    if (rain > 0.02f)
        wetness_ = std::min(1.0f, std::max(wetness_, 0.0f) + dt * (0.25f + rain) / 60.0f);
    else
        wetness_ = std::max(0.0f, wetness_ - dt / 300.0f);
    if (wetness_ > rain * 1.5f + 0.2f && rain > 0.02f) // light rain does not keep it soaked
        wetness_ = std::max(rain * 1.5f + 0.2f, wetness_ - dt / 300.0f);

    // Lightning: a strike every 60 s (storm 0.05) .. 5 s (storm 1), random distance.
    const float storm = std::clamp(env.storm, 0.0f, 1.0f);
    if (pendingStrike_ > 0.0f) {
        strike(env, eye, audio, pendingStrike_);
        pendingStrike_ = -1.0f;
    }
    if (storm > 0.01f) {
        nextStrike_ -= dt;
        if (nextStrike_ <= 0.0f) {
            std::uniform_real_distribution<float> u(0.0f, 1.0f);
            const float distance = 250.0f + 5000.0f * u(rng_) * u(rng_); // close strikes are rarer
            strike(env, eye, audio, distance);
            nextStrike_ = glm::mix(60.0f, 5.0f, storm) * (0.4f + 1.2f * u(rng_));
        }
    }
    sinceStrike_ += dt;
    flash_ = 0.0f;
    for (const Pulse& p : pulses_) {
        const float t = sinceStrike_ - p.start;
        if (t >= 0.0f && t < p.length)
            flash_ = std::max(flash_, p.strength * (1.0f - t / p.length));
    }
    if (sheltered)
        flash_ *= 0.6f; // seen through windows and doors
    if (const char* hold = std::getenv("FPS_ENGINE_LIGHTNING")) // test hook: a steady flash for screenshots
        flash_ = static_cast<float>(std::atof(hold));

    for (Thunder& t : thunder_)
        t.delay -= dt;
    for (const Thunder& t : thunder_)
        if (t.delay <= 0.0f) {
            const std::filesystem::path custom = scene.environment.thunderSound;
            const std::filesystem::path base = audio.assetDirectory() / "sounds/ambient";
            const std::filesystem::path file = !custom.empty() ? custom : base / (t.near ? "thunder.sound" : "thunder_far.sound");
            audio.play2D(file, t.volume * (sheltered ? 0.7f : 1.0f));
        }
    thunder_.erase(std::remove_if(thunder_.begin(), thunder_.end(), [](const Thunder& t) { return t.delay <= 0.0f; }), thunder_.end());
}

void GameWeather::strike(const engine::Environment& env, glm::vec3 eye, GameAudio& audio, float distance)
{
    (void)env;
    (void)eye;
    (void)audio;
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const float strength = std::clamp(1.1f - distance / 3000.0f, 0.1f, 0.8f);
    pulses_.clear();
    sinceStrike_ = 0.0f;
    float t = 0.0f;
    const int flickers = 2 + static_cast<int>(u(rng_) * 3.0f);
    for (int i = 0; i < flickers; ++i) {
        pulses_.push_back({t, 0.06f + 0.12f * u(rng_), strength * (i == 0 ? 1.0f : 0.5f + 0.5f * u(rng_))});
        t += 0.05f + 0.15f * u(rng_);
    }
    thunder_.push_back({distance / 343.0f, std::clamp(1.25f - distance / 4000.0f, 0.25f, 1.0f), distance < 1500.0f});
}
