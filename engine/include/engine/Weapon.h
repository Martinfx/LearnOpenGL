#pragma once
#include <algorithm>
#include "engine/Scene.h"
#include <glm/vec3.hpp>
#include <filesystem>
#include <set>
#include <memory>
#include <string>

namespace engine {

class PhysicsWorld;

// Weapon definition, stored as a text .weapon file (see assets/weapons/).
struct WeaponSettings {
    enum class Type { Firearm, Melee, Throwable };
    // melee: no ammo, `fire_interval` is the swing time, `spread` the swing arc.
    // throwable (incendiary bottle, grenade): every throw takes one from the stock of `ammo_type`
    // (no magazine), `fire_interval` is the whole throw until the next one is in hand, `damage`
    // what a direct hit does; the rest is in `thrown`.
    Type type = Type::Firearm;
    std::string name = "Weapon";
    std::filesystem::path modelPath; // absolute path of the first-person glTF model
    float damage = 20.0f;        // per pellet
    float fireInterval = 0.11f;  // s between shots
    bool automatic = true;       // false = one shot per trigger pull
    int pellets = 1;             // > 1 for shotguns
    int magazineSize = 30;
    // Rounds come from the player's stock of this ammo type ("5.45x39"); empty = endless.
    std::string ammoType;
    int startReserve = 90;       // rounds of ammoType the player gets with the weapon
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
    std::string animFireLast;     // shot that empties the magazine (slide locks back); its last frame is held
                                  // until the reload, which should then use anim_reload_empty. Optional
    std::string animInspect;      // looking the weapon over (key in_inspect), optional
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

    // Trade and wear (assets/items.cfg has the rest of the price list).
    int price = 1000;             // what a trader asks for a new one
    float weight = 3.0f;          // kg carried
    float wearPerShot = 0.0005f;  // condition lost per shot or swing (1 = new, 0 = wrecked)
    float jamChance = 0.08f;      // chance per shot to jam at condition 0 (none above 60 %)

    // Throwable: the flight, the break and the fire it leaves (Type::Throwable only).
    struct Thrown {
        float speed = 15.0f;           // m/s at release, along the view
        float lift = 10.0f;            // degrees above the view direction (an arc rather than a line)
        float release = 0.42f;         // s from the trigger to the moment it leaves the hand (throw clip)
        float gravity = 1.0f;          // multiples of 9.81 m/s²
        glm::vec3 handOffset{0.18f, 0.08f, -0.35f}; // release point, view space (m)
        std::filesystem::path model;   // the flying object (world glTF), absolute
        std::filesystem::path effectTrail; // particles on it in flight and on the view model's `trail_bone` (burning rag)
        glm::vec3 trailOffset{0.0f, 0.15f, 0.0f}; // trail position on the flying model (model space, m)
        std::string trailBone;         // view model node the trail follows in first person, empty = none
        std::filesystem::path effectBurst; // where it breaks (glass, flash of fuel)
        std::filesystem::path effectFire;  // one burning patch of ground (loops while it burns)
        std::filesystem::path soundLoop;   // the fire's roar (loop)
        float fuse = 0.0f;             // s until it goes off by itself, 0 = breaks on impact (a bottle)
        // Fire: a pool of burning fuel where it breaks, spreading through dry grass.
        float fireRadius = 1.8f;       // m of the splash
        float fireDuration = 14.0f;    // s a patch burns (grass burns a third of that)
        float fireDamage = 22.0f;      // hp/s to someone standing in the flames
        float burnTime = 3.0f;         // s a character keeps burning after leaving the fire
        float burnDamage = 7.0f;       // hp/s while burning
        float spread = 1.0f;           // how readily the fire runs through grass (0 = stays in the pool)
        int maxPatches = 60;           // patches (1 m) one throw may set alight, pool and grass together
        float lightIntensity = 6.0f;   // fire light, flickering
        float lightRange = 9.0f;       // m
        glm::vec3 lightColor{1.0f, 0.55f, 0.22f}; // sRGB
        bool operator==(const Thrown&) const = default;
    };
    Thrown thrown;

    // Inheritance: `base "ak74.weapon"` (relative to this file) takes every value this file does
    // not set itself from that weapon, so a variant lists only what differs and follows the base.
    std::string base;

    bool melee() const { return type == Type::Melee; }
    bool throwable() const { return type == Type::Throwable; }
};

// Wear: a worn weapon scatters more (x1 new .. x2.5 wrecked) and jams below 60 % condition.
float wornSpreadScale(float condition);
float jamProbability(const WeaponSettings& weapon, float condition);

// With a base, only the lines that differ from the base are written.
bool saveWeaponFile(const WeaponSettings& weapon, const std::filesystem::path& file, std::string* error = nullptr);
bool loadWeaponFile(WeaponSettings& weapon, const std::filesystem::path& file, std::string* error = nullptr);
// Keys ("damage", "spread"...) in which `weapon` (to be saved as `file`) differs from its base;
// empty without a base. The editor marks them, the rest shows the inherited values.
std::set<std::string> weaponOverrides(const WeaponSettings& weapon, const std::filesystem::path& file);
// The base's file for a weapon saved as `file` (empty without a base).
std::filesystem::path weaponBaseFile(const WeaponSettings& weapon, const std::filesystem::path& file);

struct ShotHit {
    bool hit = false;
    glm::vec3 point{0.0f};
    float distance = 0.0f;
    int entity = -1;             // index into Scene::entities, -1 = level geometry / nothing
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    std::string surface;         // engine/Surfaces.h; "flesh" for characters, empty = unknown
    int model = -1;              // dynamic prop hit (Scene::models index), -1 = none
    int ragdoll = -1;            // ragdoll hit (PhysicsWorld handle), -1 = none
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
    // Throwable: something in hand to throw (one left in the stock, or the throw in progress).
    bool holding() const;
    bool throwing() const { return throwTimer_ > 0.0f; } // wind-up before the release
    // Shared stock of the weapon's ammo type (the player's inventory); null = endless reserve.
    void setReserve(int* stock) { reserve_ = stock; }
    int reserve() const { return reserve_ ? *reserve_ : -1; } // -1 = endless
    void refill() { ammo_ = settings.melee() || settings.throwable() ? 0 : settings.magazineSize; } // cheat `give ammo`
    void setAmmo(int ammo) { ammo_ = settings.melee() || settings.throwable() ? 0 : std::clamp(ammo, 0, settings.magazineSize); } // level change, saves
    bool reloadingFromEmpty() const { return reloadFromEmpty_; } // the current / last reload started on an empty magazine   // melee weapons always report 0
    float swing() const;                 // melee: 0..1 through the current swing, 0 when idle

    // Plays the view model clips for what happened since the last call
    // (fire, reload, draw) and advances them. Call once per frame or step.
    void animate(float dt, float moveSpeed = 0.0f);
    void drawn();
    // Inspect key: plays anim_inspect when idle (not while firing, reloading or with the slide locked back).
    void inspect();
    bool slideLocked() const; // anim_fire_last has run: the view model holds the empty pose until a reload
    bool animated() const;               // the view model has clips (arms): no procedural swing/reload motion                        // weapon was just switched to: play the draw clip
    Animator animator;                   // pose of viewModel
    bool reloading() const { return reloadTimer_ > 0.0f; }
    float reloadProgress() const; // 0..1 while reloading
    float recoil() const { return recoil_; } // 0..1, decays after each shot
    // Raises the sights while `held` (not while reloading or for melee); 0 hip .. 1 aimed.
    void aim(float dt, bool held);
    float aimAmount() const { return aim_; }
    bool canAim() const { return settings.type == WeaponSettings::Type::Firearm && settings.aimFov > 0.0f; }
    bool flash() const { return flashTimer_ > 0.0f; }
    float condition = 1.0f; // 1 new .. 0 wrecked: widens the spread (wornSpreadScale); the game wears it per shot

private:
    int ammo_ = 0;
    int* reserve_ = nullptr;
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
    float throwTimer_ = 0.0f;   // throwable: s until the release
    bool redraw_ = false;       // throwable: take the next one in hand when the throw clip ends
    bool firedEvent_ = false;
    bool reloadEvent_ = false;
    bool drawEvent_ = true;
    bool inspectEvent_ = false;
    unsigned rng_ = 0x9e3779b9u;
};

} // namespace engine
