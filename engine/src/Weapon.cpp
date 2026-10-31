#include "engine/Weapon.h"
#include "engine/Physics.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace engine {

ShotHit traceShot(const Scene& scene, const PhysicsWorld& physics, glm::vec3 origin, glm::vec3 dir, float range)
{
    ShotHit best;
    dir = glm::normalize(dir);
    if (auto hit = physics.raycast(origin, dir, range)) {
        best.hit = true;
        best.point = hit->point;
        best.distance = hit->distance;
    }
    // Characters are not rigid bodies, so test entity bounds separately.
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const Entity& e = scene.entities[i];
        if (!e.alive)
            continue;
        if (auto t = rayAabb(origin, dir, e.bounds()); t && *t <= range && (!best.hit || *t < best.distance)) {
            best.hit = true;
            best.distance = *t;
            best.point = origin + dir * *t;
            best.entity = static_cast<int>(i);
        }
    }
    return best;
}

ShotHit traceMelee(const Scene& scene, const PhysicsWorld& physics, glm::vec3 origin, glm::vec3 dir, float range,
                   float arcDegrees)
{
    dir = glm::normalize(dir);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 side = glm::cross(dir, up);
    side = glm::length(side) > 1e-4f ? glm::normalize(side) : glm::vec3(1.0f, 0.0f, 0.0f);
    constexpr int kRays = 7;
    ShotHit best;
    for (int i = 0; i < kRays; ++i) {
        const float t = kRays > 1 ? static_cast<float>(i) / (kRays - 1) * 2.0f - 1.0f : 0.0f;
        const float angle = glm::radians(arcDegrees) * 0.5f * t;
        const glm::vec3 d = glm::normalize(dir * std::cos(angle) + side * std::sin(angle));
        const ShotHit hit = traceShot(scene, physics, origin, d, range);
        if (!hit.hit)
            continue;
        const bool better = !best.hit || (hit.entity >= 0 && best.entity < 0) ||
                            ((hit.entity >= 0) == (best.entity >= 0) && hit.distance < best.distance);
        if (better)
            best = hit;
    }
    return best;
}

Weapon::Weapon()
{
    ammo_ = settings.magazineSize;
}

Weapon::Weapon(WeaponSettings s) : settings(std::move(s))
{
    ammo_ = settings.magazineSize;
}

namespace {
std::filesystem::path pathFromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
std::string pathToUtf8(const std::filesystem::path& p)
{
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}
} // namespace

bool saveWeaponFile(const WeaponSettings& w, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        if (error)
            *error = "Cannot write " + file.string();
        return false;
    }
    std::string model;
    if (!w.modelPath.empty()) {
        const auto rel = std::filesystem::absolute(w.modelPath).lexically_relative(std::filesystem::absolute(file).parent_path());
        model = pathToUtf8(rel.empty() ? std::filesystem::absolute(w.modelPath) : rel);
    }
    out << std::setprecision(6) << "# FPS engine weapon\nversion 2\n"
        << "type " << (w.melee() ? "melee" : "firearm") << '\n'
        << "name " << std::quoted(w.name) << '\n'
        << "model " << std::quoted(model) << '\n'
        << "damage " << w.damage << '\n'
        << "fire_interval " << w.fireInterval << '\n'
        << "automatic " << (w.automatic ? 1 : 0) << '\n'
        << "pellets " << w.pellets << '\n'
        << "magazine " << w.magazineSize << '\n'
        << "reload_time " << w.reloadTime << '\n'
        << "spread " << w.spreadDegrees << '\n'
        << "range " << w.range << '\n'
        << "view_offset " << w.viewOffset.x << ' ' << w.viewOffset.y << ' ' << w.viewOffset.z << '\n'
        << "view_scale " << w.viewScale << '\n'
        << "view_rotation " << w.viewRotation.x << ' ' << w.viewRotation.y << ' ' << w.viewRotation.z << '\n'
        << "muzzle " << w.muzzle.x << ' ' << w.muzzle.y << ' ' << w.muzzle.z << '\n'
        << "anim_idle " << std::quoted(w.animIdle) << '\n'
        << "anim_fire " << std::quoted(w.animFire) << '\n'
        << "anim_reload " << std::quoted(w.animReload) << '\n'
        << "anim_draw " << std::quoted(w.animDraw) << '\n'
        << "anim_reload_empty " << std::quoted(w.animReloadEmpty) << '\n'
        << "reload_empty_time " << w.reloadEmptyTime << '\n'
        << "anim_walk " << std::quoted(w.animWalk) << '\n'
        << "aim_offset " << w.aimOffset.x << ' ' << w.aimOffset.y << ' ' << w.aimOffset.z << '\n'
        << "aim_rotation " << w.aimRotation.x << ' ' << w.aimRotation.y << ' ' << w.aimRotation.z << '\n'
        << "aim_fov " << w.aimFov << '\n'
        << "aim_time " << w.aimTime << '\n'
        << "aim_spread " << w.aimSpread << '\n';
    auto relative = [&](const std::filesystem::path& p) {
        if (p.empty())
            return std::string();
        const auto rel = std::filesystem::absolute(p).lexically_relative(std::filesystem::absolute(file).parent_path());
        return pathToUtf8(rel.empty() ? std::filesystem::absolute(p) : rel);
    };
    out << "sound_fire " << std::quoted(relative(w.soundFire)) << '\n'
        << "sound_reload " << std::quoted(relative(w.soundReload)) << '\n'
        << "sound_draw " << std::quoted(relative(w.soundDraw)) << '\n'
        << "sound_impact " << std::quoted(relative(w.soundImpact)) << '\n'
        << "sound_hit " << std::quoted(relative(w.soundHit)) << '\n';
    return static_cast<bool>(out);
}

bool loadWeaponFile(WeaponSettings& weapon, const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        if (error)
            *error = "Cannot open " + file.string();
        return false;
    }
    WeaponSettings w;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#')
            continue;
        bool ok = true;
        if (key == "version") { int v = 0; ok = static_cast<bool>(ls >> v) && v >= 1 && v <= 2; }
        else if (key == "type") {
            std::string t;
            ok = static_cast<bool>(ls >> t) && (t == "firearm" || t == "melee");
            w.type = t == "melee" ? WeaponSettings::Type::Melee : WeaponSettings::Type::Firearm;
        }
        else if (key == "name") ok = static_cast<bool>(ls >> std::quoted(w.name));
        else if (key == "model") {
            std::string m;
            ok = static_cast<bool>(ls >> std::quoted(m));
            if (ok && !m.empty()) {
                std::filesystem::path p = pathFromUtf8(m);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                w.modelPath = p.lexically_normal();
            }
        }
        else if (key == "damage") ok = static_cast<bool>(ls >> w.damage);
        else if (key == "fire_interval") ok = static_cast<bool>(ls >> w.fireInterval);
        else if (key == "automatic") { int a = 1; ok = static_cast<bool>(ls >> a); w.automatic = a != 0; }
        else if (key == "pellets") ok = static_cast<bool>(ls >> w.pellets);
        else if (key == "magazine") ok = static_cast<bool>(ls >> w.magazineSize);
        else if (key == "reload_time") ok = static_cast<bool>(ls >> w.reloadTime);
        else if (key == "spread") ok = static_cast<bool>(ls >> w.spreadDegrees);
        else if (key == "range") ok = static_cast<bool>(ls >> w.range);
        else if (key == "view_offset") ok = static_cast<bool>(ls >> w.viewOffset.x >> w.viewOffset.y >> w.viewOffset.z);
        else if (key == "view_scale") ok = static_cast<bool>(ls >> w.viewScale);
        else if (key == "view_rotation") ok = static_cast<bool>(ls >> w.viewRotation.x >> w.viewRotation.y >> w.viewRotation.z);
        else if (key == "anim_idle") ok = static_cast<bool>(ls >> std::quoted(w.animIdle));
        else if (key == "anim_fire") ok = static_cast<bool>(ls >> std::quoted(w.animFire));
        else if (key == "anim_reload") ok = static_cast<bool>(ls >> std::quoted(w.animReload));
        else if (key == "anim_draw") ok = static_cast<bool>(ls >> std::quoted(w.animDraw));
        else if (key == "anim_reload_empty") ok = static_cast<bool>(ls >> std::quoted(w.animReloadEmpty));
        else if (key == "reload_empty_time") ok = static_cast<bool>(ls >> w.reloadEmptyTime);
        else if (key == "anim_walk") ok = static_cast<bool>(ls >> std::quoted(w.animWalk));
        else if (key == "aim_offset") ok = static_cast<bool>(ls >> w.aimOffset.x >> w.aimOffset.y >> w.aimOffset.z);
        else if (key == "aim_rotation") ok = static_cast<bool>(ls >> w.aimRotation.x >> w.aimRotation.y >> w.aimRotation.z);
        else if (key == "aim_fov") ok = static_cast<bool>(ls >> w.aimFov);
        else if (key == "aim_time") ok = static_cast<bool>(ls >> w.aimTime);
        else if (key == "aim_spread") ok = static_cast<bool>(ls >> w.aimSpread);
        else if (key.rfind("sound_", 0) == 0) {
            std::filesystem::path* target = key == "sound_fire" ? &w.soundFire : key == "sound_reload" ? &w.soundReload
                                          : key == "sound_draw" ? &w.soundDraw : key == "sound_impact" ? &w.soundImpact
                                          : key == "sound_hit" ? &w.soundHit : nullptr;
            std::string m;
            ok = static_cast<bool>(ls >> std::quoted(m));
            if (ok && target && !m.empty()) {
                std::filesystem::path p = pathFromUtf8(m);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                *target = p.lexically_normal();
            }
        }
        else if (key == "muzzle") ok = static_cast<bool>(ls >> w.muzzle.x >> w.muzzle.y >> w.muzzle.z);
        // Unknown keys are ignored so newer files still load.
        if (!ok) {
            if (error)
                *error = file.string() + ":" + std::to_string(lineNo) + ": invalid line: " + line;
            return false;
        }
    }
    w.pellets = std::max(1, w.pellets);
    w.magazineSize = std::max(1, w.magazineSize);
    w.fireInterval = std::max(0.01f, w.fireInterval);
    weapon = std::move(w);
    return true;
}

int Weapon::update(float dt, bool triggerHeld, bool reloadPressed)
{
    cooldown_ = std::max(0.0f, cooldown_ - dt);
    flashTimer_ = std::max(0.0f, flashTimer_ - dt);
    recoil_ = std::max(0.0f, recoil_ - dt * 8.0f);
    swingTimer_ = std::max(0.0f, swingTimer_ - dt);

    if (settings.melee()) {
        const bool pulled = triggerHeld && (settings.automatic || !triggerWasHeld_);
        triggerWasHeld_ = triggerHeld;
        ammo_ = 0;
        reloadTimer_ = 0.0f;
        if (!pulled || cooldown_ > 0.0f)
            return 0;
        cooldown_ = settings.fireInterval;
        swingTimer_ = settings.fireInterval;
        firedEvent_ = true;
        return 1;
    }

    if (reloadTimer_ > 0.0f) {
        reloadTimer_ -= dt;
        if (reloadTimer_ <= 0.0f) {
            reloadTimer_ = 0.0f;
            ammo_ = settings.magazineSize;
        }
        return 0;
    }
    if ((reloadPressed && ammo_ < settings.magazineSize) || (triggerHeld && ammo_ == 0)) {
        triggerWasHeld_ = triggerHeld;
        reloadEmpty_ = ammo_ == 0 && !settings.animReloadEmpty.empty();
        reloadDuration_ = reloadEmpty_ && settings.reloadEmptyTime > 0.0f ? settings.reloadEmptyTime : settings.reloadTime;
        reloadDuration_ = std::max(reloadDuration_, 0.01f);
        reloadTimer_ = reloadDuration_;
        reloadEvent_ = true;
        return 0;
    }

    // Semi-automatic weapons need the trigger released between shots.
    const bool pulled = triggerHeld && (settings.automatic || !triggerWasHeld_);
    triggerWasHeld_ = triggerHeld;
    int shots = 0;
    while (pulled && ammo_ > 0 && cooldown_ <= 0.0f && (settings.automatic || shots == 0)) {
        cooldown_ += settings.fireInterval;
        --ammo_;
        ++shots;
        recoil_ = 1.0f;
        flashTimer_ = 0.05f;
        firedEvent_ = true;
    }
    return shots;
}

bool Weapon::animated() const
{
    return viewModel && viewModel->skeleton && !viewModel->skeleton->clips.empty();
}

void Weapon::drawn()
{
    drawEvent_ = true;
}

void Weapon::animate(float dt, float moveSpeed)
{
    const Skeleton* skel = viewModel ? viewModel->skeleton.get() : nullptr;
    if (!skel || skel->clips.empty()) {
        firedEvent_ = reloadEvent_ = drawEvent_ = false;
        return;
    }
    auto play = [&](const std::string& name, bool loop, float stretchTo, float fade) {
        if (name.empty())
            return false;
        const int clip = skel->findClip(name);
        if (clip < 0)
            return false;
        const float duration = skel->clips[static_cast<std::size_t>(clip)].duration;
        const float speed = stretchTo > 0.0f && duration > 0.0f ? duration / stretchTo : 1.0f;
        return animator.play(*skel, name, loop, speed, fade, !loop); // loops keep running
    };
    if (reloadEvent_)
        play(reloadEmpty_ ? settings.animReloadEmpty : settings.animReload, false, reloadDuration_, 0.1f);
    else if (firedEvent_)
        play(settings.animFire, false, settings.melee() ? settings.fireInterval : 0.0f, 0.03f);
    else if (drawEvent_)
        play(settings.animDraw, false, 0.0f, 0.0f);
    firedEvent_ = reloadEvent_ = drawEvent_ = false;
    // Idle and walk loop; a one-shot clip (fire, reload, draw) plays to its end first.
    const bool looping = animator.current.clip >= 0 && animator.current.loop;
    if (animator.current.clip < 0 || animator.finished(*skel) || looping) {
        const bool walking = moveSpeed > 0.5f && !settings.animWalk.empty() && skel->findClip(settings.animWalk) >= 0;
        if (!play(walking ? settings.animWalk : settings.animIdle, true, 0.0f, 0.25f) && animator.current.clip < 0)
            animator.play(*skel, skel->clips.front().name, true);
    }
    animator.update(*skel, dt);
}

void Weapon::aim(float dt, bool held)
{
    const bool up = held && canAim() && !reloading();
    const float step = dt / std::max(settings.aimTime, 0.01f);
    aim_ = std::clamp(aim_ + (up ? step : -step), 0.0f, 1.0f);
}

float Weapon::swing() const
{
    return swingTimer_ > 0.0f ? 1.0f - swingTimer_ / settings.fireInterval : 0.0f;
}

float Weapon::reloadProgress() const
{
    return reloading() ? 1.0f - reloadTimer_ / reloadDuration_ : 0.0f;
}

glm::vec3 Weapon::spreadDirection(glm::vec3 aim)
{
    auto random01 = [this] {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(rng_ & 0xffffffu) / static_cast<float>(0xffffff);
    };
    aim = glm::normalize(aim);
    const glm::vec3 helper = std::abs(aim.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 u = glm::normalize(glm::cross(aim, helper));
    const glm::vec3 v = glm::cross(u, aim);
    const float spread = settings.spreadDegrees * (1.0f + (settings.aimSpread - 1.0f) * aim_);
    const float angle = glm::radians(spread) * std::sqrt(random01());
    const float phi = 6.2831853f * random01();
    return glm::normalize(aim + (u * std::cos(phi) + v * std::sin(phi)) * std::tan(angle));
}

} // namespace engine
