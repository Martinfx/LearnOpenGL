#pragma once
#include <engine/Demo.h>
#include <engine/Scene.h>

#include <glm/vec3.hpp>

#include <algorithm>
#include <random>
#include <vector>

class GameAudio;

// Weather that changes while playing: how wet the ground is (wets with rain, dries
// slowly), lightning strikes (flash now, thunder later: sound travels 343 m/s) and
// whether the player stands under a roof (rain sounds muffled).
class GameWeather {
public:
    void startLevel(const engine::Scene& scene); // already wet when the level starts in rain
    // `sheltered`: something above the player's head (a roof).
    void update(float dt, const engine::Scene& scene, glm::vec3 eye, bool sheltered, GameAudio& audio);

    float wetness() const { return wetness_; }
    float flash() const { return flash_; }  // 0..1 lightning light this frame
    void setWetness(float wetness) { wetness_ = wetness; } // saved games
    // A strike now (`distance` m away): Lua game.lightning, console "lightning".
    void strikeNow(float distance) { pendingStrike_ = std::max(50.0f, distance); }

private:
    void strike(const engine::Environment& env, glm::vec3 eye, GameAudio& audio, float distance);

    float wetness_ = 0.0f;
    float flash_ = 0.0f;
    float nextStrike_ = 8.0f;  // s until the next lightning
    struct Pulse { float start, length, strength; };
    std::vector<Pulse> pulses_; // flickers of the current flash (time since the strike)
    float sinceStrike_ = 100.0f;
    float pendingStrike_ = -1.0f;
    struct Thunder { float delay; float volume; bool near; };
    std::vector<Thunder> thunder_;
    std::mt19937 rng_{engine::seedFor("weather")};
};
