#pragma once
#include "engine/Scene.h"
#include <glm/vec3.hpp>
#include <filesystem>
#include <memory>
#include <string>

namespace engine {

class PhysicsWorld;

// Weapon definition, stored as a text .weapon file (see assets/weapons/).
struct WeaponSettings {
    std::string name = "Weapon";
    std::filesystem::path modelPath; // absolute path of the first-person glTF model
    float damage = 20.0f;        // per pellet
    float fireInterval = 0.11f;  // s between shots
    bool automatic = true;       // false = one shot per trigger pull
    int pellets = 1;             // > 1 for shotguns
    int magazineSize = 30;
    float reloadTime = 1.5f;     // s
    float spreadDegrees = 1.0f;  // random cone half-angle
    float range = 200.0f;        // m
    glm::vec3 viewOffset{0.17f, -0.17f, -0.36f}; // first-person position (view space, m)
    float viewScale = 0.15f;
};

bool saveWeaponFile(const WeaponSettings& weapon, const std::filesystem::path& file, std::string* error = nullptr);
bool loadWeaponFile(WeaponSettings& weapon, const std::filesystem::path& file, std::string* error = nullptr);

struct ShotHit {
    bool hit = false;
    glm::vec3 point{0.0f};
    float distance = 0.0f;
    int entity = -1;             // index into Scene::entities, -1 = level geometry / nothing
};

// Closest hit along a ray: level geometry (Jolt) or a living entity.
ShotHit traceShot(const Scene& scene, const PhysicsWorld& physics, glm::vec3 origin, glm::vec3 dir, float range);

// Automatic hitscan weapon: fire rate, magazine and reload. Rendering and
// effects are up to the game; recoil() and flash() drive the animation.
class Weapon {
public:
    WeaponSettings settings;
    std::shared_ptr<const Model> viewModel;

    Weapon();
    explicit Weapon(WeaponSettings s);

    // One fixed step. Returns how many shots were fired in this step
    // (each shot fires settings.pellets bullets).
    int update(float dt, bool triggerHeld, bool reloadPressed);

    // Direction of the next bullet with random spread around `aim`.
    glm::vec3 spreadDirection(glm::vec3 aim);

    int ammo() const { return ammo_; }
    bool reloading() const { return reloadTimer_ > 0.0f; }
    float reloadProgress() const; // 0..1 while reloading
    float recoil() const { return recoil_; } // 0..1, decays after each shot
    bool flash() const { return flashTimer_ > 0.0f; }

private:
    int ammo_ = 0;
    bool triggerWasHeld_ = false;
    float cooldown_ = 0.0f;
    float reloadTimer_ = 0.0f;
    float recoil_ = 0.0f;
    float flashTimer_ = 0.0f;
    unsigned rng_ = 0x9e3779b9u;
};

} // namespace engine
