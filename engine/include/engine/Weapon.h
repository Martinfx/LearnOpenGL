#pragma once
#include <algorithm>
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
    std::string animReloadEmpty;  // reload from an empty magazine (with chambering), optional
    float reloadEmptyTime = 0.0f; // s, 0 = reload_time
    std::string animWalk;         // loops while the player moves, optional
    // Aiming down the sights (right mouse). aim_fov 0 = the weapon cannot aim.
    glm::vec3 aimOffset{0.0f};    // view offset while aiming: puts the sights on the screen center
    glm::vec3 aimRotation{0.0f};  // extra pitch, yaw, roll (degrees) while aiming, to line up the sights
    float aimFov = 0.0f;          // vertical camera FOV while aiming (degrees)
    float aimTime = 0.2f;         // s to raise the weapon to the eye
    float aimSpread = 0.35f;      // spread multiplier while aiming
    // Sounds (.sound or audio files, absolute paths; empty = silent). Melee: `soundFire` is the swing.
    std::filesystem::path soundFire;
    std::filesystem::path soundReload;
    std::filesystem::path soundDraw;
    std::filesystem::path soundImpact;  // bullet or blade hitting level geometry
    std::filesystem::path soundHit;     // hitting a character
    std::filesystem::path soundReloadEmpty; // reload from an empty magazine (mag out, in, bolt/slide); empty = soundReload
    std::filesystem::path soundDryFire;     // trigger pulled on an empty magazine
    // Effects (absolute .particle / .glb paths, empty = none). Directions in view space:
    // x right, y up, -z forward.
    std::filesystem::path effectFlash;      // muzzle flash, drawn with the first-person weapon (+Y = barrel)
    std::filesystem::path effectSmoke;      // gun smoke left in the world at the muzzle (+Y = barrel)
    std::filesystem::path casingModel;      // spent case thrown out per shot (assets/models/weapons/casings/)
    glm::vec3 casingOffset{0.08f, -0.06f, -0.3f};  // ejection port, view space (m)
    glm::vec3 casingVelocity{1.6f, 1.2f, 0.3f};    // m/s, view space (right, up, back)

    bool melee() const { return type == Type::Melee; }
};

bool saveWeaponFile(const WeaponSettings& weapon, const std::filesystem::path& file, std::string* error = nullptr);
bool loadWeaponFile(WeaponSettings& weapon, const std::filesystem::path& file, std::string* error = nullptr);

struct ShotHit {
    bool hit = false;
    glm::vec3 point{0.0f};
    float distance = 0.0f;
    int entity = -1;             // index into Scene::entities, -1 = level geometry / nothing
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    std::string surface;         // engine/Surfaces.h; "flesh" for characters, empty = unknown
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

    int ammo() const { return ammo_; }
    void refill() { ammo_ = settings.melee() ? 0 : settings.magazineSize; } // cheat `give ammo`
    void setAmmo(int ammo) { ammo_ = settings.melee() ? 0 : std::clamp(ammo, 0, settings.magazineSize); } // level change, saves
    bool reloadingFromEmpty() const { return reloadFromEmpty_; } // the current / last reload started on an empty magazine   // melee weapons always report 0
    float swing() const;                 // melee: 0..1 through the current swing, 0 when idle

    // Plays the view model clips for what happened since the last call
    // (fire, reload, draw) and advances them. Call once per frame or step.
    void animate(float dt, float moveSpeed = 0.0f);
    void drawn();
    bool animated() const;               // the view model has clips (arms): no procedural swing/reload motion                        // weapon was just switched to: play the draw clip
    Animator animator;                   // pose of viewModel
    bool reloading() const { return reloadTimer_ > 0.0f; }
    float reloadProgress() const; // 0..1 while reloading
    float recoil() const { return recoil_; } // 0..1, decays after each shot
    // Raises the sights while `held` (not while reloading or for melee); 0 hip .. 1 aimed.
    void aim(float dt, bool held);
    float aimAmount() const { return aim_; }
    bool canAim() const { return !settings.melee() && settings.aimFov > 0.0f; }
    bool flash() const { return flashTimer_ > 0.0f; }

private:
    int ammo_ = 0;
    bool triggerWasHeld_ = false;
    float cooldown_ = 0.0f;
    float reloadTimer_ = 0.0f;
    float reloadDuration_ = 1.0f;
    float aim_ = 0.0f;
    bool reloadEmpty_ = false;
    bool reloadFromEmpty_ = false;
    float recoil_ = 0.0f;
    float flashTimer_ = 0.0f;
    float swingTimer_ = 0.0f;
    bool firedEvent_ = false;
    bool reloadEvent_ = false;
    bool drawEvent_ = true;
    unsigned rng_ = 0x9e3779b9u;
};

} // namespace engine
