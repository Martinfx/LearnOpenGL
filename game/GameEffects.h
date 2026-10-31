#pragma once
#include <engine/Decals.h>
#include <engine/Particles.h>
#include <engine/Renderer.h>
#include <engine/Scene.h>
#include <engine/Surfaces.h>
#include <engine/Weapon.h>

#include <glm/mat4x4.hpp>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <vector>

namespace engine {
class PhysicsWorld;
}
class GameAudio;

// Visual effects of the game: particles (bullet impacts per surface, gun smoke,
// rain, level effects), bullet holes and spent cases.
class GameEffects {
public:
    void init(const std::filesystem::path& assetDir, const engine::SurfaceTable& surfaces);
    // Starts the level's "particle" entities and the rain (follows the camera).
    void startLevel(const engine::Scene& scene);

    // A bullet hit level geometry (surface effect and bullet hole) or a character (blood).
    void impact(glm::vec3 point, glm::vec3 normal, const std::string& surface, bool character);
    // A bullet glancing off `surface` at `point`, flying on along `direction` (sparks).
    void ricochet(glm::vec3 point, glm::vec3 direction, const engine::SurfaceType& surface);
    // A shot: muzzle flash (first-person layer), gun smoke and a spent case.
    // `view` is the camera's view matrix; `muzzleView` the muzzle in view space.
    void shot(const engine::WeaponSettings& weapon, const glm::mat4& view, glm::vec3 muzzleView, glm::vec3 playerVelocity);

    // Simulation; casings bounce on the level and click via `audio`.
    void update(float dt, const engine::Scene& scene, const engine::PhysicsWorld& physics, glm::vec3 eye, GameAudio& audio);
    // World particles, decals and casings into the main view; muzzle flashes into the overlay.
    void fill(engine::RenderView& view, const glm::mat4& viewMatrix, engine::OverlayPass& overlay) const;

    int particleCount() const { return world_.particleCount() + overlay_.particleCount(); }

private:
    std::shared_ptr<const engine::ParticleEffect> effect(const std::filesystem::path& file);
    std::shared_ptr<const engine::Model> model(const std::filesystem::path& file);

    struct Casing {
        std::shared_ptr<const engine::Model> model;
        glm::vec3 position{0.0f}, velocity{0.0f};
        glm::vec3 axis{1.0f, 0.0f, 0.0f};
        float angle = 0.0f, spin = 0.0f; // radians, radians per second
        glm::mat3 orientation{1.0f};
        float age = 0.0f;
        int bounces = 0;
        bool resting = false;
    };

    void stepCasing(Casing& c, float dt, const engine::PhysicsWorld& physics, GameAudio& audio);

    std::filesystem::path assetDir_;
    const engine::SurfaceTable* surfaces_ = nullptr;
    engine::ParticleSystem world_;
    engine::ParticleSystem overlay_; // view space: muzzle flashes stay on the gun
    engine::DecalPool decals_{384};
    std::vector<Casing> casings_;
    std::map<std::filesystem::path, std::shared_ptr<const engine::ParticleEffect>> effects_;
    std::map<std::filesystem::path, std::shared_ptr<const engine::Model>> models_;
    std::map<std::filesystem::path, std::shared_ptr<engine::TextureAsset>> decalTextures_;
    engine::ParticleSystem::Handle rain_ = 0;
    std::mt19937 rng_{99};
};
