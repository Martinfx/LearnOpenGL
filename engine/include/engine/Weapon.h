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
    enum class Type { Firearm, Melee };
    Type type = Type::Firearm;   // melee: no ammo, `fire_interval` is the swing time, `spread` the swing arc
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
    glm::vec3 viewRotation{0.0f, 180.0f, 0.0f}; // degrees (pitch, yaw, roll) turning the model to point -Z
    glm::vec3 muzzle{0.0f, 0.45f, 1.35f};       // muzzle flash position (model space, before view_scale)
    // Clips of an animated view model (first-person arms + weapon in one glTF).
    // Empty = not used. Reload and swing clips are time-stretched to reload_time / fire_interval.
    std::string animIdle;
    std::string animFire;
    std::string animReload;
    std::string animDraw;

    bool melee() const { return type == Type::Melee; }
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

// Melee strike: a fan of rays across `arcDegrees` (horizontal). The closest
// entity hit wins over level geometry so a swing at a guard next to a wall connects.
ShotHit traceMelee(const Scene& scene, const PhysicsWorld& physics, glm::vec3 origin, glm::vec3 dir, float range,
                   float arcDegrees);

// Hitscan or melee weapon: fire rate, magazine and reload. Rendering and
// effects are up to the game; recoil() and flash() drive the animation.
class Weapon {
public:
    WeaponSettings settings;
    std::shared_ptr<const Model> viewModel;

    Weapon();
    explicit Weapon(WeaponSettings s);

    // One fixed step. Returns how many shots were fired in this step
    // (each shot fires settings.pellets bullets; a melee swing counts as one shot).
    int update(float dt, bool triggerHeld, bool reloadPressed);

    // Direction of the next bullet with random spread around `aim`.
    glm::vec3 spreadDirection(glm::vec3 aim);

    int ammo() const { return ammo_; }   // melee weapons always report 0
    float swing() const;                 // melee: 0..1 through the current swing, 0 when idle

    // Plays the view model clips for what happened since the last call
    // (fire, reload, draw) and advances them. Call once per frame or step.
    void animate(float dt);
    void drawn();                        // weapon was just switched to: play the draw clip
    Animator animator;                   // pose of viewModel
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
    float swingTimer_ = 0.0f;
    bool firedEvent_ = false;
    bool reloadEvent_ = false;
    bool drawEvent_ = true;
    unsigned rng_ = 0x9e3779b9u;
};

} // namespace engine
