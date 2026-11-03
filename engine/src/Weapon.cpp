#include "engine/Weapon.h"
#include "engine/Vfs.h"
#include "engine/Water.h"
#include "engine/Physics.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
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
        best.normal = hit->normal;
        best.surface = hit->surface;
        best.model = hit->model;
        best.ragdoll = hit->ragdoll;
    }
    // Characters are not rigid bodies, so test entity bounds separately.
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const Entity& e = scene.entities[i];
        if (!e.alive || e.offline || e.helper() || e.type == "pickup")
            continue;
        if (auto t = rayAabb(origin, dir, e.bounds()); t && *t <= range && (!best.hit || *t < best.distance)) {
            best.hit = true;
            best.distance = *t;
            best.point = origin + dir * *t;
            best.entity = static_cast<int>(i);
            best.normal = -dir;
            best.surface = "flesh";
            best.model = -1;
            best.ragdoll = -1;
        }
    }
    // Water stops a shot from above at its surface (a splash; shallow angles may skip off it).
    if (dir.y < -1e-4f)
        for (const WaterBody& w : waterBodies(scene)) {
            if (origin.y <= w.level)
                continue;
            const float t = (w.level - origin.y) / dir.y;
            if (t < 0.0f || t > range || (best.hit && t >= best.distance))
                continue;
            const glm::vec3 p = origin + dir * t;
            if (!w.contains(p - glm::vec3(0.0f, 0.01f, 0.0f)))
                continue;
            best = ShotHit{};
            best.hit = true;
            best.point = p;
            best.distance = t;
            best.normal = {0.0f, 1.0f, 0.0f};
            best.surface = "water";
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

namespace {
// Every value line of a weapon, paths relative to `file`'s folder (a base and its variant write
// the same text for the same value, so the variant's file keeps only the differences).
std::string weaponLines(const WeaponSettings& w, const std::filesystem::path& file)
{
    std::ostringstream out;
    std::string model;
    if (!w.modelPath.empty()) {
        const auto rel = std::filesystem::absolute(w.modelPath).lexically_relative(std::filesystem::absolute(file).parent_path());
        model = pathToUtf8(rel.empty() ? std::filesystem::absolute(w.modelPath) : rel);
    }
    out << std::setprecision(6)
        << "type " << (w.melee() ? "melee" : w.throwable() ? "throwable" : "firearm") << '\n'
        << "name " << std::quoted(w.name) << '\n'
        << "model " << std::quoted(model) << '\n'
        << "damage " << w.damage << '\n'
        << "fire_interval " << w.fireInterval << '\n'
        << "automatic " << (w.automatic ? 1 : 0) << '\n'
        << "pellets " << w.pellets << '\n'
        << "magazine " << w.magazineSize << '\n'
        << "ammo_type " << std::quoted(w.ammoType) << ' ' << w.startReserve << '\n'
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
        << "anim_fire_last " << std::quoted(w.animFireLast) << '\n'
        << "anim_inspect " << std::quoted(w.animInspect) << '\n'
        << "aim_offset " << w.aimOffset.x << ' ' << w.aimOffset.y << ' ' << w.aimOffset.z << '\n'
        << "aim_rotation " << w.aimRotation.x << ' ' << w.aimRotation.y << ' ' << w.aimRotation.z << '\n'
        << "aim_fov " << w.aimFov << '\n'
        << "aim_time " << w.aimTime << '\n'
        << "aim_spread " << w.aimSpread << '\n'
        << "price " << w.price << '\n'
        << "weight " << w.weight << '\n'
        << "wear " << w.wearPerShot << ' ' << w.jamChance << '\n';
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
        << "sound_hit " << std::quoted(relative(w.soundHit)) << '\n'
        << "sound_reload_empty " << std::quoted(relative(w.soundReloadEmpty)) << '\n'
        << "sound_dry_fire " << std::quoted(relative(w.soundDryFire)) << '\n'
        << "effect_flash " << std::quoted(relative(w.effectFlash)) << '\n'
        << "effect_smoke " << std::quoted(relative(w.effectSmoke)) << '\n'
        << "casing " << std::quoted(relative(w.casingModel)) << '\n'
        << "casing_offset " << w.casingOffset.x << ' ' << w.casingOffset.y << ' ' << w.casingOffset.z << '\n'
        << "casing_velocity " << w.casingVelocity.x << ' ' << w.casingVelocity.y << ' ' << w.casingVelocity.z << '\n';
    if (w.throwable()) {
        const WeaponSettings::Thrown& t = w.thrown;
        out << "throw " << t.speed << ' ' << t.lift << ' ' << t.release << ' ' << t.gravity << '\n'
            << "throw_offset " << t.handOffset.x << ' ' << t.handOffset.y << ' ' << t.handOffset.z << '\n'
            << "throw_model " << std::quoted(relative(t.model)) << '\n'
            << "effect_trail " << std::quoted(relative(t.effectTrail)) << '\n'
            << "trail_offset " << t.trailOffset.x << ' ' << t.trailOffset.y << ' ' << t.trailOffset.z << '\n'
            << "trail_bone " << std::quoted(t.trailBone) << '\n'
            << "effect_burst " << std::quoted(relative(t.effectBurst)) << '\n'
            << "effect_fire " << std::quoted(relative(t.effectFire)) << '\n'
            << "sound_fire_loop " << std::quoted(relative(t.soundLoop)) << '\n'
            << "fuse " << t.fuse << '\n'
            << "fire " << t.fireRadius << ' ' << t.fireDuration << ' ' << t.fireDamage << '\n'
            << "burn " << t.burnTime << ' ' << t.burnDamage << '\n'
            << "fire_spread " << t.spread << ' ' << t.maxPatches << '\n'
            << "fire_light " << t.lightIntensity << ' ' << t.lightRange << ' ' << t.lightColor.x << ' ' << t.lightColor.y << ' '
            << t.lightColor.z << '\n';
    }
    return out.str();
}

std::map<std::string, std::string> linesByKey(const std::string& text)
{
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
        out[line.substr(0, line.find(' '))] = line;
    return out;
}
} // namespace

std::filesystem::path weaponBaseFile(const WeaponSettings& w, const std::filesystem::path& file)
{
    if (w.base.empty())
        return {};
    std::filesystem::path p = pathFromUtf8(w.base);
    if (p.is_relative())
        p = std::filesystem::absolute(file).parent_path() / p;
    return p.lexically_normal();
}

std::set<std::string> weaponOverrides(const WeaponSettings& w, const std::filesystem::path& file)
{
    std::set<std::string> out;
    WeaponSettings base;
    if (w.base.empty() || !loadWeaponFile(base, weaponBaseFile(w, file)))
        return out;
    const auto mine = linesByKey(weaponLines(w, file)), theirs = linesByKey(weaponLines(base, file));
    for (const auto& [key, line] : mine)
        if (const auto it = theirs.find(key); it == theirs.end() || it->second != line)
            out.insert(key);
    return out;
}

bool saveWeaponFile(const WeaponSettings& w, const std::filesystem::path& file, std::string* error)
{
    std::string body = weaponLines(w, file);
    if (!w.base.empty()) {
        WeaponSettings base;
        if (!loadWeaponFile(base, weaponBaseFile(w, file), error))
            return false;
        // Only what differs from the base: the rest follows the base when it changes.
        const auto theirs = linesByKey(weaponLines(base, file));
        std::string kept;
        std::istringstream in(body);
        for (std::string line; std::getline(in, line);)
            if (const auto it = theirs.find(line.substr(0, line.find(' '))); it == theirs.end() || it->second != line)
                kept += line + '\n';
        body = kept;
    }
    std::ofstream out(file);
    if (!out) {
        if (error)
            *error = "Cannot write " + file.string();
        return false;
    }
    out << "# FPS engine weapon\nversion 2\n";
    if (!w.base.empty())
        out << "base " << std::quoted(w.base) << '\n';
    out << body;
    return static_cast<bool>(out);
}

bool loadWeaponFile(WeaponSettings& weapon, const std::filesystem::path& file, std::string* error)
{
    engine::VfsStream in(file);
    if (!in) {
        if (error)
            *error = "Cannot open " + file.string();
        return false;
    }
    WeaponSettings w;
    // A base first: this file's lines then replace its values.
    for (std::string line; std::getline(in, line);) {
        std::istringstream ls(line);
        std::string key, base;
        if (ls >> key && key == "base" && ls >> std::quoted(base) && !base.empty()) {
            static thread_local int depth = 0;
            if (depth > 8) {
                if (error)
                    *error = file.filename().string() + ": base chain too deep (a loop?)";
                return false;
            }
            std::filesystem::path p = pathFromUtf8(base);
            if (p.is_relative())
                p = std::filesystem::absolute(file).parent_path() / p;
            ++depth;
            std::string baseError;
            const bool ok = loadWeaponFile(w, p.lexically_normal(), &baseError);
            --depth;
            if (!ok) {
                if (error)
                    *error = file.filename().string() + ": base: " + baseError;
                return false;
            }
            w.base = base;
            break;
        }
    }
    in.clear();
    in.seekg(0);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#')
            continue;
        bool ok = true;
        if (key == "base") { std::string b; ok = static_cast<bool>(ls >> std::quoted(b)); }
        else if (key == "version") { int v = 0; ok = static_cast<bool>(ls >> v) && v >= 1 && v <= 2; }
        else if (key == "type") {
            std::string t;
            ok = static_cast<bool>(ls >> t) && (t == "firearm" || t == "melee" || t == "throwable");
            w.type = t == "melee" ? WeaponSettings::Type::Melee
                   : t == "throwable" ? WeaponSettings::Type::Throwable : WeaponSettings::Type::Firearm;
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
        else if (key == "ammo_type") ok = static_cast<bool>(ls >> std::quoted(w.ammoType) >> w.startReserve);
        else if (key == "reload_time") ok = static_cast<bool>(ls >> w.reloadTime);
        else if (key == "spread") ok = static_cast<bool>(ls >> w.spreadDegrees);
        else if (key == "price") ok = static_cast<bool>(ls >> w.price);
        else if (key == "weight") ok = static_cast<bool>(ls >> w.weight);
        else if (key == "wear") ok = static_cast<bool>(ls >> w.wearPerShot >> w.jamChance);
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
        else if (key == "anim_fire_last") ok = static_cast<bool>(ls >> std::quoted(w.animFireLast));
        else if (key == "anim_inspect") ok = static_cast<bool>(ls >> std::quoted(w.animInspect));
        else if (key == "aim_offset") ok = static_cast<bool>(ls >> w.aimOffset.x >> w.aimOffset.y >> w.aimOffset.z);
        else if (key == "aim_rotation") ok = static_cast<bool>(ls >> w.aimRotation.x >> w.aimRotation.y >> w.aimRotation.z);
        else if (key == "aim_fov") ok = static_cast<bool>(ls >> w.aimFov);
        else if (key == "effect_flash" || key == "effect_smoke" || key == "casing") {
            std::string m;
            ok = static_cast<bool>(ls >> std::quoted(m));
            std::filesystem::path* target = key == "effect_flash" ? &w.effectFlash : key == "effect_smoke" ? &w.effectSmoke : &w.casingModel;
            if (ok && !m.empty()) {
                std::filesystem::path p = pathFromUtf8(m);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                *target = p.lexically_normal();
            }
        }
        else if (key == "casing_offset") ok = static_cast<bool>(ls >> w.casingOffset.x >> w.casingOffset.y >> w.casingOffset.z);
        else if (key == "casing_velocity") ok = static_cast<bool>(ls >> w.casingVelocity.x >> w.casingVelocity.y >> w.casingVelocity.z);
        else if (key == "aim_time") ok = static_cast<bool>(ls >> w.aimTime);
        else if (key == "aim_spread") ok = static_cast<bool>(ls >> w.aimSpread);
        else if (key.rfind("sound_", 0) == 0) {
            std::filesystem::path* target = key == "sound_fire" ? &w.soundFire : key == "sound_reload" ? &w.soundReload
                                          : key == "sound_draw" ? &w.soundDraw : key == "sound_impact" ? &w.soundImpact
                                          : key == "sound_hit" ? &w.soundHit
                                          : key == "sound_reload_empty" ? &w.soundReloadEmpty
                                          : key == "sound_dry_fire" ? &w.soundDryFire : nullptr;
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
        // Throwables
        else if (key == "throw") ok = static_cast<bool>(ls >> w.thrown.speed >> w.thrown.lift >> w.thrown.release >> w.thrown.gravity);
        else if (key == "throw_offset") ok = static_cast<bool>(ls >> w.thrown.handOffset.x >> w.thrown.handOffset.y >> w.thrown.handOffset.z);
        else if (key == "trail_offset") ok = static_cast<bool>(ls >> w.thrown.trailOffset.x >> w.thrown.trailOffset.y >> w.thrown.trailOffset.z);
        else if (key == "trail_bone") ok = static_cast<bool>(ls >> std::quoted(w.thrown.trailBone));
        else if (key == "fuse") ok = static_cast<bool>(ls >> w.thrown.fuse);
        else if (key == "fire") ok = static_cast<bool>(ls >> w.thrown.fireRadius >> w.thrown.fireDuration >> w.thrown.fireDamage);
        else if (key == "burn") ok = static_cast<bool>(ls >> w.thrown.burnTime >> w.thrown.burnDamage);
        else if (key == "fire_spread") ok = static_cast<bool>(ls >> w.thrown.spread >> w.thrown.maxPatches);
        else if (key == "fire_light")
            ok = static_cast<bool>(ls >> w.thrown.lightIntensity >> w.thrown.lightRange >> w.thrown.lightColor.x >> w.thrown.lightColor.y >>
                                   w.thrown.lightColor.z);
        else if (key == "throw_model" || key == "effect_trail" || key == "effect_burst" || key == "effect_fire" || key == "sound_fire_loop") {
            std::filesystem::path* target = key == "throw_model" ? &w.thrown.model : key == "effect_trail" ? &w.thrown.effectTrail
                                          : key == "effect_burst" ? &w.thrown.effectBurst : key == "effect_fire" ? &w.thrown.effectFire
                                                                  : &w.thrown.soundLoop;
            std::string m;
            ok = static_cast<bool>(ls >> std::quoted(m));
            target->clear();
            if (ok && !m.empty()) {
                std::filesystem::path p = pathFromUtf8(m);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                *target = p.lexically_normal();
            }
        }
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
    w.thrown.release = std::clamp(w.thrown.release, 0.0f, w.fireInterval);
    w.thrown.maxPatches = std::clamp(w.thrown.maxPatches, 1, 400);
    weapon = std::move(w);
    return true;
}

int Weapon::update(float dt, bool triggerHeld, bool reloadPressed)
{
    cooldown_ = std::max(0.0f, cooldown_ - dt);
    flashTimer_ = std::max(0.0f, flashTimer_ - dt);
    recoil_ = std::max(0.0f, recoil_ - dt * 8.0f);
    swingTimer_ = std::max(0.0f, swingTimer_ - dt);

    if (settings.throwable()) {
        // Wind up, let go at `release` (one taken from the stock), then the next one comes to hand.
        ammo_ = 0;
        reloadTimer_ = 0.0f;
        const bool pulled = triggerHeld && (settings.automatic || !triggerWasHeld_);
        triggerWasHeld_ = triggerHeld;
        if (throwTimer_ > 0.0f) {
            throwTimer_ -= dt;
            if (throwTimer_ > 0.0f)
                return 0;
            throwTimer_ = 0.0f;
            if (reserve_)
                *reserve_ = std::max(0, *reserve_ - 1);
            redraw_ = true;
            return 1;
        }
        if (!pulled || cooldown_ > 0.0f || (reserve_ && *reserve_ <= 0))
            return 0;
        cooldown_ = settings.fireInterval;
        swingTimer_ = settings.fireInterval;
        throwTimer_ = std::max(settings.thrown.release, 1e-4f);
        firedEvent_ = true;
        return 0;
    }

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
            int take = settings.magazineSize - ammo_;
            if (reserve_) { // rounds from the stock; what is left in the old magazine stays in it
                take = std::min(take, std::max(0, *reserve_));
                *reserve_ -= take;
            }
            ammo_ += take;
        }
        return 0;
    }
    const bool canReload = !reserve_ || *reserve_ > 0;
    if (canReload && ((reloadPressed && ammo_ < settings.magazineSize) || (triggerHeld && ammo_ == 0))) {
        triggerWasHeld_ = triggerHeld;
        reloadFromEmpty_ = ammo_ == 0;
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

bool Weapon::holding() const
{
    return !settings.throwable() || throwTimer_ > 0.0f || !reserve_ || *reserve_ > 0;
}

bool Weapon::animated() const
{
    return viewModel && viewModel->skeleton && !viewModel->skeleton->clips.empty();
}

void Weapon::drawn()
{
    drawEvent_ = true;
}

void Weapon::inspect()
{
    if (!settings.animInspect.empty() && !reloading() && !slideLocked())
        inspectEvent_ = true;
}

bool Weapon::slideLocked() const
{
    const Skeleton* skel = viewModel ? viewModel->skeleton.get() : nullptr;
    return skel && !settings.melee() && ammo_ == 0 && !reloading() && !settings.animFireLast.empty() &&
           skel->findClip(settings.animFireLast) >= 0;
}

void Weapon::animate(float dt, float moveSpeed)
{
    const Skeleton* skel = viewModel ? viewModel->skeleton.get() : nullptr;
    if (!skel || skel->clips.empty()) {
        firedEvent_ = reloadEvent_ = drawEvent_ = inspectEvent_ = false;
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
    const bool locked = slideLocked();
    const bool looping = animator.current.clip >= 0 && animator.current.loop;
    if (reloadEvent_)
        play(reloadEmpty_ ? settings.animReloadEmpty : settings.animReload, false, reloadDuration_, 0.1f);
    else if (firedEvent_)
        play(locked ? settings.animFireLast : settings.animFire, false, settings.melee() ? settings.fireInterval : 0.0f, 0.03f);
    else if (drawEvent_)
        play(settings.animDraw, false, 0.0f, 0.0f);
    else if (inspectEvent_ && looping && !locked)
        play(settings.animInspect, false, 0.0f, 0.15f);
    firedEvent_ = reloadEvent_ = drawEvent_ = inspectEvent_ = false;
    // Throwable: once the throw clip has played out, the next one is taken in hand (draw clip).
    if (redraw_ && animator.current.clip >= 0 && !animator.current.loop && animator.finished(*skel)) {
        redraw_ = false;
        if (holding())
            play(settings.animDraw, false, 0.0f, 0.05f);
    }
    // Idle and walk loop; a one-shot clip (fire, reload, draw, inspect) plays to its end first.
    const bool nowLooping = animator.current.clip >= 0 && animator.current.loop;
    if (animator.current.clip < 0 || animator.finished(*skel) || nowLooping) {
        const int last = locked ? skel->findClip(settings.animFireLast) : -1;
        if (last >= 0) { // empty: hold the end of the last shot (slide back) until the reload
            if (animator.current.clip != last) {
                play(settings.animFireLast, false, 0.0f, 0.2f);
                animator.current.time = skel->clips[static_cast<std::size_t>(last)].duration;
            }
        } else {
            const bool walking = moveSpeed > 0.5f && !settings.animWalk.empty() && skel->findClip(settings.animWalk) >= 0;
            if (!play(walking ? settings.animWalk : settings.animIdle, true, 0.0f, 0.25f) && animator.current.clip < 0)
                animator.play(*skel, skel->clips.front().name, true);
        }
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

float wornSpreadScale(float condition)
{
    return 1.0f + 1.5f * (1.0f - std::clamp(condition, 0.0f, 1.0f));
}

float jamProbability(const WeaponSettings& weapon, float condition)
{
    if (weapon.melee() || weapon.throwable() || condition >= 0.6f)
        return 0.0f;
    return std::max(0.0f, weapon.jamChance) * (0.6f - std::max(condition, 0.0f)) / 0.6f;
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
    const float spread = settings.spreadDegrees * (1.0f + (settings.aimSpread - 1.0f) * aim_) * wornSpreadScale(condition);
    const float angle = glm::radians(spread) * std::sqrt(random01());
    const float phi = 6.2831853f * random01();
    return glm::normalize(aim + (u * std::cos(phi) + v * std::sin(phi)) * std::tan(angle));
}

} // namespace engine
