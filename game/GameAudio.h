#pragma once
#include <engine/Ambience.h>
#include <engine/Audio.h>
#include <engine/Scene.h>
#include <engine/Surfaces.h>

#include <filesystem>
#include <map>
#include <random>
#include <memory>
#include <string>
#include <vector>

// The game's sounds: one AudioSystem, sounds cached by path, the level's sound
// entities (3D loops and ambient zones) and the player's footsteps.
class GameAudio {
public:
    void init(const std::filesystem::path& assetDir);

    // Empty or unreadable paths are silent (a missing file is reported once).
    void play(const std::filesystem::path& sound, glm::vec3 position, float volume = 1.0f);
    void play2D(const std::filesystem::path& sound, float volume = 1.0f);
    // A path relative to assets/ (Lua scripts, entity properties).
    void playAsset(const std::string& relative, const glm::vec3* position, float volume);
    void preload(const std::filesystem::path& sound);
    // Like play/play2D (position null = 2D) but returns the voice, e.g. to stop a loop later; 0 = silent.
    engine::AudioSystem::Handle start(const std::filesystem::path& sound, const glm::vec3* position, float volume = 1.0f);

    // Starts the loops of the level's "sound" entities.
    void startLevel(const engine::Scene& scene);
    // Listener, zone volumes, footsteps; once per frame.
    void update(float dt, const engine::Scene& scene, glm::vec3 eye, glm::vec3 forward);
    // Footsteps from the distance walked on the ground (call per fixed step).
    // `surface` is what the player stands on (PhysicsWorld::raycast down); its step
    // sound from assets/surfaces.cfg wins over the level's default footstep.
    void walk(const engine::PlayerSettings& player, glm::vec3 feet, glm::vec3 velocity, bool grounded, float dt,
              const std::string& surface = {});
    // Bullet impact: the surface's impact sound, else `fallback` (the weapon's).
    void impact(glm::vec3 point, const std::string& surface, const std::filesystem::path& fallback);

    const engine::SurfaceTable& surfaces() const { return surfaces_; }

    engine::AudioSystem& system() { return audio_; }
    const std::filesystem::path& assetDirectory() const { return assetDir_; }
    // Under a roof: rain and wind loops are muffled.
    void setSheltered(bool sheltered) { sheltered_ = sheltered; }

private:
    std::shared_ptr<const engine::SoundDef> get(const std::filesystem::path& sound);
    void step(const engine::PlayerSettings& player, glm::vec3 feet, float volume, const std::string& surface);

    void updateReverb(const engine::Scene& scene, glm::vec3 eye); // reverb zones + the level's reverb
    // Ambience sets (the level's and its zones'): layers crossfaded by zone, random far sounds.
    void startAmbience(const engine::Scene& scene);
    void updateAmbience(float dt, const engine::Scene& scene, glm::vec3 eye);
    struct Ambience {
        std::filesystem::path file;
        engine::AmbienceSet set;
        std::vector<engine::AudioSystem::Handle> layers; // one loop per layer
        std::vector<float> timers;                       // per random sound: seconds to the next try
        float weight = 0.0f;
    };
    std::vector<Ambience> ambiences_;
    std::mt19937 rng_{std::random_device{}()};
    struct Source {
        std::size_t entity = 0;
        engine::AudioSystem::Handle handle = 0;
        bool zone = false;
        float fade = 10.0f;
        float volume = 1.0f;
        bool on = true; // point source: audible (entity enabled and alive)
    };

    engine::AudioSystem audio_;
    std::filesystem::path assetDir_;
    std::map<std::filesystem::path, std::shared_ptr<const engine::SoundDef>> cache_;
    std::vector<Source> sources_;
    engine::SurfaceTable surfaces_;
    engine::AudioSystem::Handle rain_ = 0, wind_ = 0; // weather loops, volume follows Environment::rain / wind
    bool sheltered_ = false;
    float shelterMix_ = 0.0f;   // 0 open .. 1 sheltered, eased
    float stride_ = 0.0f;       // metres walked since the last footstep
    bool wasGrounded_ = true;
    float fallSpeed_ = 0.0f;    // fastest downward speed while airborne
};
