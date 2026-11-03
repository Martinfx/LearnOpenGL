#pragma once
#include <engine/Audio.h>
#include <engine/Decals.h>
#include <engine/Particles.h>
#include <engine/Renderer.h>
#include <engine/Scene.h>
#include <engine/Weapon.h>

#include <glm/mat4x4.hpp>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

namespace engine {
class PhysicsWorld;
class ScriptSystem;
}
class GameAudio;

// Thrown weapons and the fires they start (incendiary bottles): the flight with its
// burning rag, the break (glass, a flash of fuel), the pool of burning fuel that runs
// on through dry grass (the ground cover of the terrain burns away and leaves soot),
// the damage to the player and to characters standing in it (they keep burning a moment
// after), the fire's light and roar. Scripts see the fire through game.fire_near().
// Everything is set in the weapon's .weapon file (WeaponSettings::Thrown, editor: Weapons).
class GameFire {
public:
    void init(const std::filesystem::path& assetDir, GameAudio& audio);
    // Reads the terrain's ground cover (grass and weeds burn, trees do not).
    void startLevel(const engine::Scene& scene);

    // A throwable leaves the hand at `from` (world) with `velocity`. thrower: entity index, -1 = the player.
    void throwObject(const engine::WeaponSettings& weapon, glm::vec3 from, glm::vec3 velocity, int thrower = -1);
    // Sets the fuel of `weapon` alight at `point` (where a bottle broke; console `ignite`).
    void ignite(const engine::WeaponSettings& weapon, glm::vec3 point, const engine::PhysicsWorld& physics, const engine::Scene& scene,
                int thrower = -1);

    // One fixed step: flight and breaking, burning and spreading, damage, hazards for the scripts.
    // The player's damage goes through scripts.setPlayerHealth (not in god mode).
    void fixedUpdate(float dt, engine::Scene& scene, const engine::PhysicsWorld& physics, glm::vec3 playerFeet,
                     engine::ScriptSystem& scripts, bool god);
    // Per frame: particles, the burnt grass taken out of the terrain (in batches), the roar's volume.
    void update(float dt, engine::Scene& scene);
    void fill(engine::RenderView& view, const glm::mat4& viewMatrix) const;
    // First person: the burning rag of the throwable in hand, on the view model's trail bone.
    // `held` null (another weapon, nothing in hand) puts it out.
    void fillHeld(const engine::Weapon* held, const glm::mat4& modelTransform, const glm::mat4& viewMatrix, engine::OverlayPass& overlay);
    // The release point of the throwable in hand (its trail bone, else Thrown::handOffset), view space.
    glm::vec3 heldReleasePoint(const engine::Weapon& held, const glm::mat4& modelTransform) const;

    float playerBurning() const { return playerBurn_ > 0.0f ? std::min(1.0f, playerBurn_) : 0.0f; } // screen effect
    int patchCount() const { return static_cast<int>(patches_.size()); }
    int flyingCount() const { return static_cast<int>(flying_.size()); }
    int particleCount() const { return world_.particleCount() + overlay_.particleCount(); }

private:
    struct Flying {
        engine::WeaponSettings::Thrown cfg;
        float damage = 0.0f;           // direct hit
        std::filesystem::path breakSound, hitSound;
        std::shared_ptr<const engine::Model> model;
        glm::vec3 position{0.0f}, velocity{0.0f};
        glm::vec3 axis{1.0f, 0.0f, 0.0f};
        glm::mat3 orientation{1.0f};
        float spin = 10.0f, age = 0.0f;
        int thrower = -1;
        engine::ParticleSystem::Handle trail = 0;
        bool done = false;
    };
    struct Fire {                      // what one bottle set alight
        engine::WeaponSettings::Thrown cfg;
        std::shared_ptr<const engine::ParticleEffect> effect;
        int budget = 0;                // patches it may still light
        int living = 0;
        int thrower = -1;
        glm::vec3 center{0.0f};
        engine::AudioSystem::Handle loop = 0;
        std::uint64_t key = 0;         // light key
        float age = 0.0f;
    };
    struct Patch {
        glm::vec3 position{0.0f};
        float life = 0.0f, duration = 10.0f;
        float radius = 0.75f;
        int fire = 0;                  // index into fires_
        std::uint64_t cell = 0;
        bool grass = false;
        float spreadTimer = 0.0f;
        engine::ParticleSystem::Handle fx = 0;
    };
    struct Cell {                      // 1 m of ground cover
        float fuel = 0.0f;             // 0..1
        std::vector<std::uint32_t> instances; // terrain foliage burning away with it
        bool burning = false;
    };
    struct Burning {                   // a character on fire
        float timer = 0.0f;
        float pending = 0.0f;          // damage collected until the next tick
        int fire = -1;
        engine::ParticleSystem::Handle fx = 0;
    };

    static std::uint64_t cellKey(int x, int z);
    static std::uint64_t cellAt(glm::vec3 p);
    std::shared_ptr<const engine::ParticleEffect> effect(const std::filesystem::path& file);
    std::shared_ptr<const engine::Model> model(const std::filesystem::path& file);
    bool groundAt(glm::vec3 near, const engine::PhysicsWorld& physics, const engine::Scene& scene, float reach, glm::vec3& out) const;
    void light(int fire, glm::vec3 position, std::uint64_t cell, float duration, bool grass);
    void breakBottle(Flying& f, glm::vec3 point, const engine::PhysicsWorld& physics, const engine::Scene& scene);

    std::filesystem::path assetDir_;
    GameAudio* audio_ = nullptr;
    engine::ParticleSystem world_;
    engine::ParticleSystem overlay_;   // view space: the rag in hand
    engine::ParticleSystem::Handle heldFx_ = 0;
    std::filesystem::path heldEffect_;
    engine::DecalPool scorch_{400};
    std::shared_ptr<engine::TextureAsset> scorchTexture_;
    std::vector<Flying> flying_;
    std::vector<Fire> fires_;
    std::vector<Patch> patches_;
    std::unordered_map<std::uint64_t, Cell> cells_;
    std::vector<std::uint32_t> burnt_;  // foliage instances to take out of the terrain
    float burntTimer_ = 0.0f;
    std::map<int, Burning> burning_;    // entity index -> on fire
    float damageTick_ = 0.0f;
    float playerBurn_ = 0.0f;           // s the player keeps burning
    float playerPending_ = 0.0f;        // hp collected until the next tick
    float playerBurnDamage_ = 6.0f;     // hp/s of the fire that set the player alight
    std::map<std::filesystem::path, std::shared_ptr<const engine::ParticleEffect>> effects_;
    std::map<std::filesystem::path, std::shared_ptr<const engine::Model>> models_;
    std::uint64_t nextKey_ = 0xF1E0000000ull;
    float wind_ = 0.0f;
    glm::vec2 windDir_{1.0f, 0.0f};
    float rain_ = 0.0f;
    double time_ = 0.0;
    std::mt19937 rng_{4242};
};
