#include "GameFire.h"
#include "GameAudio.h"

#include <engine/Animation.h>
#include <engine/Factions.h>
#include <engine/Log.h>
#include <engine/Material.h>
#include <engine/Physics.h>
#include <engine/Scripting.h>
#include <engine/Terrain.h>
#include <engine/TimeOfDay.h>
#include <engine/Water.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kGravity = 9.81f;
constexpr std::size_t kMaxFlying = 16;
constexpr std::size_t kMaxPatches = 400;  // all fires together (60 FPS budget)
constexpr float kDamageTick = 0.25f;      // s between damage applications
constexpr float kPatchRadius = 0.75f;     // a 1 m cell's flames reach this far
constexpr int kMaxFireLights = 4;
} // namespace

std::uint64_t GameFire::cellKey(int x, int z)
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(z);
}

std::uint64_t GameFire::cellAt(glm::vec3 p)
{
    return cellKey(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.z)));
}

void GameFire::init(const std::filesystem::path& assetDir, GameAudio& audio)
{
    assetDir_ = assetDir;
    audio_ = &audio;
    world_.setParticleBudget(6000);
    patches_.reserve(kMaxPatches); // light() appends while the spread loop holds references
    scorchTexture_ = engine::loadTextureAsset(assetDir / "textures/decals/scorch.png", nullptr, true);
}

std::shared_ptr<const engine::ParticleEffect> GameFire::effect(const std::filesystem::path& file)
{
    if (file.empty())
        return nullptr;
    if (auto it = effects_.find(file); it != effects_.end())
        return it->second;
    std::string error;
    auto fx = engine::loadParticleEffect(file, &error);
    if (!fx)
        LOG_WARN("fire", "%s", error.c_str());
    effects_[file] = fx;
    return fx;
}

std::shared_ptr<const engine::Model> GameFire::model(const std::filesystem::path& file)
{
    if (file.empty())
        return nullptr;
    if (auto it = models_.find(file); it != models_.end())
        return it->second;
    auto m = std::make_shared<engine::Model>();
    std::string error;
    if (!engine::loadModelFile(*m, file, &error)) {
        LOG_WARN("fire", "%s", error.c_str());
        m.reset();
    }
    models_[file] = m;
    return m;
}

void GameFire::startLevel(const engine::Scene& scene)
{
    for (const Fire& f : fires_)
        if (f.loop)
            audio_->system().stop(f.loop);
    world_.clear();
    overlay_.clear();
    heldFx_ = 0;
    scorch_.clear();
    flying_.clear();
    fires_.clear();
    patches_.clear();
    cells_.clear();
    burnt_.clear();
    burning_.clear();
    playerBurn_ = playerPending_ = 0.0f;
    // Ground cover that burns: low plants without collision (grass, weeds, nettles, small
    // bushes); trees and wrecks (collision) stay. Each 1 m cell gets fuel by how much grows there.
    const engine::Terrain* t = scene.terrain.get();
    if (!t)
        return;
    std::vector<char> burns(t->foliageTypes.size(), 0);
    for (std::size_t i = 0; i < t->foliageTypes.size(); ++i) {
        const engine::FoliageType& type = t->foliageTypes[i];
        float height = 1.0f;
        if (type.model) {
            const engine::Aabb& b = type.model->localBounds;
            height = (b.max.y - b.min.y) * type.maxScale;
        }
        burns[i] = !type.collision && height < 2.2f && type.sway > 0.0f;
    }
    for (std::size_t i = 0; i < t->foliage.size(); ++i) {
        const engine::FoliageInstance& f = t->foliage[i];
        if (f.type >= burns.size() || !burns[f.type])
            continue;
        Cell& c = cells_[cellAt(f.position)];
        c.fuel = std::min(1.0f, c.fuel + 0.34f);
        c.instances.push_back(static_cast<std::uint32_t>(i));
    }
    if (!cells_.empty())
        LOG_INFO("fire", "%zu m² of ground cover can burn", cells_.size());
}

void GameFire::throwObject(const engine::WeaponSettings& weapon, glm::vec3 from, glm::vec3 velocity, int thrower)
{
    if (flying_.size() >= kMaxFlying)
        flying_.erase(flying_.begin());
    Flying f;
    f.cfg = weapon.thrown;
    f.damage = weapon.damage;
    f.breakSound = weapon.soundImpact;
    f.hitSound = weapon.soundHit;
    f.model = model(weapon.thrown.model);
    f.position = from;
    f.velocity = velocity;
    f.thrower = thrower;
    // Tumbles end over end about the sideways axis.
    const glm::vec3 side = glm::cross(glm::normalize(velocity + glm::vec3(0.0f, 1e-3f, 0.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
    f.axis = glm::length(side) > 1e-3f ? glm::normalize(side) : glm::vec3(1.0f, 0.0f, 0.0f);
    f.spin = -std::uniform_real_distribution<float>(7.0f, 11.0f)(rng_);
    if (auto fx = effect(weapon.thrown.effectTrail))
        f.trail = world_.spawn(fx, from);
    flying_.push_back(std::move(f));
}

// Ground under `near` within `reach` m up or down: the level's geometry, else the terrain.
bool GameFire::groundAt(glm::vec3 near, const engine::PhysicsWorld& physics, const engine::Scene& scene, float reach, glm::vec3& out) const
{
    if (auto hit = physics.raycast(near + glm::vec3(0.0f, reach, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), reach * 2.0f)) {
        if (hit->normal.y < 0.45f)
            return false; // a wall or a steep roof: fuel runs off
        out = hit->point;
        return true;
    }
    if (scene.terrain && scene.terrain->contains(near.x, near.z)) {
        const float h = scene.terrain->heightAt(near.x, near.z);
        if (std::abs(h - near.y) <= reach) {
            out = {near.x, h, near.z};
            return true;
        }
    }
    return false;
}

void GameFire::light(int fire, glm::vec3 position, std::uint64_t cell, float duration, bool grass)
{
    if (patches_.size() >= kMaxPatches)
        return;
    Fire& f = fires_[static_cast<std::size_t>(fire)];
    Patch p;
    p.position = position + glm::vec3(0.0f, 0.02f, 0.0f);
    p.duration = std::max(1.0f, duration);
    p.radius = kPatchRadius;
    p.fire = fire;
    p.cell = cell;
    p.grass = grass;
    p.spreadTimer = std::uniform_real_distribution<float>(0.0f, 0.25f)(rng_);
    if (f.effect)
        p.fx = world_.spawn(f.effect, p.position);
    if (auto it = cells_.find(cell); it != cells_.end())
        it->second.burning = true;
    --f.budget;
    ++f.living;
    patches_.push_back(p);
}

void GameFire::ignite(const engine::WeaponSettings& weapon, glm::vec3 point, const engine::PhysicsWorld& physics,
                      const engine::Scene& scene, int thrower)
{
    const engine::WeaponSettings::Thrown& cfg = weapon.thrown;
    glm::vec3 ground;
    // Broke on a wall or a car: the fuel runs down to the ground below.
    if (!groundAt(point, physics, scene, 1.0f, ground)) {
        if (auto hit = physics.raycast(point + glm::vec3(0.0f, 0.05f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 30.0f); hit && hit->normal.y > 0.45f)
            ground = hit->point;
        else
            return;
    }
    if (engine::waterAt(scene, ground + glm::vec3(0.0f, 0.05f, 0.0f)))
        return; // fizzles out in the water
    // Rain: the fuel still burns, but shorter.
    const float wet = 1.0f - 0.45f * rain_;
    Fire f;
    f.cfg = cfg;
    f.effect = effect(cfg.effectFire);
    f.budget = cfg.maxPatches;
    f.thrower = thrower;
    f.center = ground;
    f.key = nextKey_++;
    if (!cfg.soundLoop.empty())
        f.loop = audio_->start(cfg.soundLoop, &ground, 0.0f);
    fires_.push_back(f);
    const int index = static_cast<int>(fires_.size()) - 1;
    // The splash: every 1 m cell within the radius that has ground near the impact's height.
    std::uniform_real_distribution<float> jitter(-0.25f, 0.25f), share(0.75f, 1.0f);
    const int r = static_cast<int>(std::ceil(cfg.fireRadius));
    const int gx = static_cast<int>(std::floor(ground.x)), gz = static_cast<int>(std::floor(ground.z));
    light(index, ground, cellAt(ground), cfg.fireDuration * wet, false);
    std::vector<std::pair<float, glm::ivec2>> ring;
    for (int z = -r; z <= r; ++z)
        for (int x = -r; x <= r; ++x) {
            const glm::vec2 c(static_cast<float>(gx + x) + 0.5f, static_cast<float>(gz + z) + 0.5f);
            const float d = glm::distance(c, glm::vec2(ground.x, ground.z));
            if (d <= cfg.fireRadius && cellAt(glm::vec3(c.x, 0.0f, c.y)) != cellAt(ground))
                ring.emplace_back(d, glm::ivec2(gx + x, gz + z));
        }
    std::sort(ring.begin(), ring.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [d, c] : ring) {
        if (fires_[static_cast<std::size_t>(index)].budget <= 0)
            break;
        glm::vec3 at(static_cast<float>(c.x) + 0.5f + jitter(rng_), ground.y, static_cast<float>(c.y) + 0.5f + jitter(rng_));
        glm::vec3 g;
        if (!groundAt(at, physics, scene, 0.8f, g) || engine::waterAt(scene, g + glm::vec3(0.0f, 0.05f, 0.0f)))
            continue;
        const std::uint64_t key = cellKey(c.x, c.y);
        const auto cell = cells_.find(key);
        const bool grass = cell != cells_.end() && cell->second.fuel > 0.0f;
        // Thinner at the rim: burns out sooner.
        const float rim = 1.0f - 0.45f * d / std::max(cfg.fireRadius, 0.1f);
        light(index, g, key, cfg.fireDuration * wet * rim * share(rng_), grass);
    }
}

void GameFire::breakBottle(Flying& f, glm::vec3 point, const engine::PhysicsWorld& physics, const engine::Scene& scene)
{
    f.done = true;
    if (f.trail)
        world_.stop(f.trail);
    if (auto fx = effect(f.cfg.effectBurst))
        world_.spawn(fx, point);
    audio_->play(f.breakSound, point, 1.0f);
    engine::WeaponSettings w;
    w.thrown = f.cfg;
    ignite(w, point, physics, scene, f.thrower);
}

void GameFire::fixedUpdate(float dt, engine::Scene& scene, const engine::PhysicsWorld& physics, glm::vec3 playerFeet,
                           engine::ScriptSystem& scripts, bool god)
{
    time_ += dt;
    {
        const engine::Environment env = engine::environmentAt(scene.environment, scene.sunDirection);
        rain_ = std::clamp(env.rain, 0.0f, 1.0f);
        wind_ = std::clamp(env.wind, 0.0f, 1.0f);
        const float a = glm::radians(env.windDirection);
        windDir_ = {std::cos(a), std::sin(a)};
    }

    // Flight: gravity, tumbling, the rag's flame; breaks on whatever it meets.
    for (Flying& f : flying_) {
        if (f.done)
            continue;
        f.age += dt;
        f.velocity.y -= kGravity * f.cfg.gravity * dt;
        const glm::vec3 move = f.velocity * dt;
        const float len = glm::length(move);
        f.orientation = glm::mat3(glm::rotate(glm::mat4(1.0f), f.spin * dt, f.axis)) * f.orientation;
        if (len > 1e-6f) {
            const glm::vec3 dir = move / len;
            float best = len + 0.05f;
            glm::vec3 at(0.0f);
            int entity = -1;
            bool hit = false;
            if (auto h = physics.raycast(f.position, dir, len + 0.05f)) {
                best = h->distance;
                at = h->point;
                hit = true;
            }
            for (std::size_t i = 0; i < scene.entities.size(); ++i) {
                const engine::Entity& e = scene.entities[i];
                if (!e.alive || e.offline || e.helper() || e.type == "pickup" || static_cast<int>(i) == f.thrower)
                    continue;
                if (auto t = engine::rayAabb(f.position, dir, e.bounds()); t && *t < best) {
                    best = *t;
                    at = f.position + dir * *t;
                    entity = static_cast<int>(i);
                    hit = true;
                }
            }
            if (hit) {
                if (entity >= 0 && f.damage > 0.0f) {
                    scripts.damageEntity(entity, f.damage, f.thrower);
                    audio_->play(f.hitSound, at, 1.0f);
                }
                breakBottle(f, at - dir * 0.05f, physics, scene);
                scripts.makeNoise(at, 35.0f); // glass and a whoosh: heard around
                continue;
            }
            // Landing in water puts it out.
            if (engine::waterAt(scene, f.position + move)) {
                f.done = true;
                if (f.trail)
                    world_.kill(f.trail);
                audio_->play(f.breakSound, f.position, 0.4f);
                continue;
            }
            f.position += move;
        }
        if (f.cfg.fuse > 0.0f && f.age >= f.cfg.fuse) {
            breakBottle(f, f.position, physics, scene);
            continue;
        }
        if (f.trail) {
            const glm::vec3 rag = f.position + f.orientation * f.cfg.trailOffset;
            world_.setTransform(f.trail, rag);
        }
        if (f.position.y < -200.0f || f.age > 20.0f)
            f.done = true;
    }
    std::erase_if(flying_, [](const Flying& f) { return f.done; });

    // Burning: patches age, light their neighbours in dry grass (more readily downwind,
    // hardly in the rain) and go out.
    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    const std::size_t count = patches_.size(); // patches lit now start next step
    for (std::size_t i = 0; i < count; ++i) {
        Patch& p = patches_[i];
        p.life += dt;
        Fire& fire = fires_[static_cast<std::size_t>(p.fire)];
        if (p.life >= p.duration)
            continue;
        p.spreadTimer -= dt;
        if (p.spreadTimer > 0.0f || fire.cfg.spread <= 0.0f || fire.budget <= 0)
            continue;
        p.spreadTimer = 0.25f;
        if (p.life < 0.6f) // takes a moment to catch
            continue;
        const int cx = static_cast<int>(std::floor(p.position.x)), cz = static_cast<int>(std::floor(p.position.z));
        for (int dz = -1; dz <= 1 && fire.budget > 0; ++dz)
            for (int dx = -1; dx <= 1 && fire.budget > 0; ++dx) {
                if (dx == 0 && dz == 0)
                    continue;
                const auto it = cells_.find(cellKey(cx + dx, cz + dz));
                if (it == cells_.end() || it->second.burning || it->second.fuel <= 0.0f)
                    continue;
                const glm::vec2 dir = glm::normalize(glm::vec2(static_cast<float>(dx), static_cast<float>(dz)));
                const float downwind = 1.0f + 1.5f * wind_ * glm::dot(dir, windDir_);
                const float chance = 0.25f * 0.55f * fire.cfg.spread * it->second.fuel * std::max(0.1f, downwind) * (1.0f - 0.85f * rain_);
                if (roll(rng_) >= chance)
                    continue;
                glm::vec3 at(static_cast<float>(cx + dx) + 0.5f + (roll(rng_) - 0.5f) * 0.5f, p.position.y,
                             static_cast<float>(cz + dz) + 0.5f + (roll(rng_) - 0.5f) * 0.5f);
                glm::vec3 g;
                if (!groundAt(at, physics, scene, 1.0f, g)) {
                    it->second.fuel = 0.0f; // nothing to stand on there (a wall's top, a pit)
                    continue;
                }
                const float grassBurn = std::max(4.0f, fire.cfg.fireDuration * 0.35f) * (0.8f + 0.4f * roll(rng_));
                light(p.fire, g, it->first, grassBurn, true);
            }
    }
    // Out: the grass there is gone (soot instead), the patch's flames die down.
    for (Patch& p : patches_) {
        if (p.life < p.duration)
            continue;
        if (p.fx)
            world_.stop(p.fx);
        if (auto it = cells_.find(p.cell); it != cells_.end()) {
            it->second.burning = false;
            it->second.fuel = 0.0f;
            burnt_.insert(burnt_.end(), it->second.instances.begin(), it->second.instances.end());
            it->second.instances.clear();
        }
        if (scorchTexture_)
            scorch_.add(scorchTexture_, 2, 2, -1, p.position + glm::vec3(0.0f, 0.01f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
                        p.grass ? 1.5f : 1.8f, 240.0f);
        --fires_[static_cast<std::size_t>(p.fire)].living;
    }
    std::erase_if(patches_, [](const Patch& p) { return p.life >= p.duration; });

    // Fires: the roar follows the size of the fire; gone when its last patch is out.
    for (std::size_t i = 0; i < fires_.size(); ++i) {
        Fire& f = fires_[i];
        f.age += dt;
        if (f.living > 0 && f.loop)
            audio_->system().setVolume(f.loop, std::min(1.0f, 0.45f + 0.12f * std::sqrt(static_cast<float>(f.living)) * 2.0f), 0.3f);
        if (f.living <= 0 && f.loop) {
            audio_->system().stop(f.loop, 1.5f);
            f.loop = 0;
        }
    }
    // Drop finished fires from the end (indices of the others stay valid).
    while (!fires_.empty() && fires_.back().living <= 0 && fires_.back().loop == 0)
        fires_.pop_back();
    if (patches_.empty() && !fires_.empty()) {
        for (const Fire& f : fires_)
            if (f.loop)
                audio_->system().stop(f.loop, 1.5f);
        fires_.clear();
    }

    // Hazards for the scripts: every patch (NPCs and dogs step out of it).
    {
        std::vector<engine::ScriptSystem::Hazard> hazards;
        hazards.reserve(patches_.size());
        for (const Patch& p : patches_)
            hazards.push_back({p.position, p.radius + 0.2f});
        scripts.setHazards(std::move(hazards));
    }

    // Damage: standing in the flames sets you alight; burning goes on a moment after leaving.
    // Water puts it out. Applied a few times a second (one hurt sound, not sixty).
    auto inFire = [&](glm::vec3 feet, float halfWidth, int* fire) {
        for (const Patch& p : patches_) {
            const glm::vec3 d = feet - p.position;
            if (std::abs(d.y) < 1.2f && d.x * d.x + d.z * d.z < (p.radius + halfWidth) * (p.radius + halfWidth)) {
                *fire = p.fire;
                return true;
            }
        }
        return false;
    };
    int fireAtPlayer = -1;
    const bool playerWet = engine::waterAt(scene, playerFeet + glm::vec3(0.0f, 0.4f, 0.0f));
    if (!patches_.empty() && inFire(playerFeet, 0.2f, &fireAtPlayer) && !playerWet) {
        const auto& cfg = fires_[static_cast<std::size_t>(fireAtPlayer)].cfg;
        playerPending_ += cfg.fireDamage * dt;
        playerBurn_ = std::max(playerBurn_, cfg.burnTime);
        playerBurnDamage_ = cfg.burnDamage;
    } else if (playerBurn_ > 0.0f) {
        playerBurn_ = playerWet ? 0.0f : std::max(0.0f, playerBurn_ - dt);
        playerPending_ += playerBurnDamage_ * dt;
    }
    if (!patches_.empty())
        for (std::size_t i = 0; i < scene.entities.size(); ++i) {
            const engine::Entity& e = scene.entities[i];
            if (!e.alive || e.offline || e.helper() || engine::entityFaction(e).empty())
                continue;
            int fire = -1;
            if (inFire(e.position, std::min(e.size.x, e.size.z) * 0.5f, &fire)) {
                Burning& b = burning_[static_cast<int>(i)];
                const auto& cfg = fires_[static_cast<std::size_t>(fire)].cfg;
                b.pending += cfg.fireDamage * dt;
                b.timer = std::max(b.timer, cfg.burnTime);
                b.fire = fire;
            }
        }
    for (auto it = burning_.begin(); it != burning_.end();) {
        const int index = it->first;
        Burning& b = it->second;
        const bool valid = index >= 0 && static_cast<std::size_t>(index) < scene.entities.size();
        const engine::Entity* e = valid ? &scene.entities[static_cast<std::size_t>(index)] : nullptr;
        if (!e || !e->alive || engine::waterAt(scene, e->position + glm::vec3(0.0f, 0.4f, 0.0f))) {
            if (b.fx)
                world_.stop(b.fx);
            it = burning_.erase(it);
            continue;
        }
        b.timer -= dt;
        if (b.fire >= 0 && static_cast<std::size_t>(b.fire) < fires_.size())
            b.pending += fires_[static_cast<std::size_t>(b.fire)].cfg.burnDamage * dt;
        else
            b.pending += 6.0f * dt;
        // Flames on the body while it burns.
        if (!b.fx) {
            const std::shared_ptr<const engine::ParticleEffect> fx =
                b.fire >= 0 && static_cast<std::size_t>(b.fire) < fires_.size() ? fires_[static_cast<std::size_t>(b.fire)].effect : nullptr;
            if (fx) {
                const glm::mat3 body(glm::vec3(0.35f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.35f));
                b.fx = world_.spawn(fx, e->position + glm::vec3(0.0f, e->size.y * 0.3f, 0.0f), body);
            }
        } else {
            const glm::mat3 body(glm::vec3(0.35f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.35f));
            world_.setTransform(b.fx, e->position + glm::vec3(0.0f, e->size.y * 0.3f, 0.0f), body);
        }
        if (b.timer <= 0.0f) {
            if (b.fx)
                world_.stop(b.fx);
            b.fx = 0;
            if (b.pending <= 0.0f) {
                it = burning_.erase(it);
                continue;
            }
        }
        ++it;
    }
    damageTick_ -= dt;
    if (damageTick_ <= 0.0f) {
        damageTick_ = kDamageTick;
        if (playerPending_ > 0.0f) {
            if (!god && scripts.playerHealth() > 0.0f)
                scripts.setPlayerHealth(std::max(0.0f, scripts.playerHealth() - playerPending_));
            playerPending_ = 0.0f;
        }
        for (auto& [index, b] : burning_) {
            if (b.pending <= 0.0f)
                continue;
            const int attacker = b.fire >= 0 && static_cast<std::size_t>(b.fire) < fires_.size() ? fires_[static_cast<std::size_t>(b.fire)].thrower : -2;
            const float amount = b.pending;
            b.pending = 0.0f;
            scripts.damageEntity(index, amount, attacker);
        }
    }
}

void GameFire::update(float dt, engine::Scene& scene)
{
    world_.setWind(glm::vec3(windDir_.x, 0.0f, windDir_.y) * wind_ * 12.0f);
    world_.update(dt);
    overlay_.update(dt);
    scorch_.update(dt);
    // Burnt grass leaves the terrain in batches: every removal rebuilds the foliage
    // instances on the GPU, so not more often than every 2 s.
    burntTimer_ -= dt;
    if (burnt_.empty() || burntTimer_ > 0.0f || !scene.terrain)
        return;
    burntTimer_ = 2.0f;
    engine::Terrain& t = *scene.terrain;
    std::sort(burnt_.begin(), burnt_.end());
    burnt_.erase(std::unique(burnt_.begin(), burnt_.end()), burnt_.end());
    std::vector<std::uint32_t> remap(t.foliage.size());
    std::size_t out = 0, b = 0;
    for (std::size_t i = 0; i < t.foliage.size(); ++i) {
        if (b < burnt_.size() && burnt_[b] == i) {
            ++b;
            remap[i] = UINT32_MAX;
            continue;
        }
        remap[i] = static_cast<std::uint32_t>(out);
        t.foliage[out++] = t.foliage[i];
    }
    if (out == t.foliage.size()) {
        burnt_.clear();
        return;
    }
    t.foliage.resize(out);
    t.foliageVersion = engine::newResourceId();
    for (auto& [key, cell] : cells_) {
        std::size_t k = 0;
        for (std::uint32_t idx : cell.instances)
            if (idx < remap.size() && remap[idx] != UINT32_MAX)
                cell.instances[k++] = remap[idx];
        cell.instances.resize(k);
    }
    burnt_.clear();
}

void GameFire::fill(engine::RenderView& view, const glm::mat4& viewMatrix) const
{
    const glm::mat3 camera(glm::inverse(viewMatrix));
    scorch_.appendSprites(view.cameraPosition, 90.0f, view.sprites);
    world_.appendSprites(view.cameraPosition, camera, view.sprites);
    for (const Flying& f : flying_) {
        if (!f.model)
            continue;
        glm::mat4 m(f.orientation);
        m[3] = glm::vec4(f.position, 1.0f);
        view.extraModels.emplace_back(f.model.get(), m);
    }
    // One flickering light per fire (at the middle of its flames), the nearest few; the
    // flying bottle's rag gives a small one of its own.
    struct Lit {
        float distance;
        engine::LocalLight light;
    };
    std::vector<Lit> lights;
    std::vector<glm::vec3> sum(fires_.size(), glm::vec3(0.0f));
    std::vector<int> n(fires_.size(), 0);
    std::vector<float> grow(fires_.size(), 0.0f);
    for (const Patch& p : patches_) {
        sum[static_cast<std::size_t>(p.fire)] += p.position;
        ++n[static_cast<std::size_t>(p.fire)];
        const float life = std::min(std::min(1.0f, p.life / 0.4f), std::clamp((p.duration - p.life) / 2.0f, 0.0f, 1.0f));
        grow[static_cast<std::size_t>(p.fire)] += life;
    }
    auto srgb = [](glm::vec3 c) { return glm::pow(c, glm::vec3(2.2f)); };
    for (std::size_t i = 0; i < fires_.size(); ++i) {
        if (n[i] == 0)
            continue;
        const Fire& f = fires_[i];
        const glm::vec3 centre = sum[i] / static_cast<float>(n[i]);
        const float size = std::sqrt(grow[i]);
        const double t = time_ * 9.0 + static_cast<double>(f.key % 97);
        const float flicker = 0.78f + 0.12f * static_cast<float>(std::sin(t)) + 0.1f * static_cast<float>(std::sin(t * 2.7 + 1.3));
        engine::LocalLight l;
        l.position = centre + glm::vec3(0.0f, 0.9f, 0.0f);
        l.color = srgb(f.cfg.lightColor) * f.cfg.lightIntensity * std::min(1.6f, 0.35f + 0.3f * size) * flicker;
        l.range = f.cfg.lightRange + std::min(8.0f, size);
        l.key = f.key;
        lights.push_back({glm::distance(view.cameraPosition, l.position), l});
    }
    for (const Flying& f : flying_)
        if (f.trail) {
            engine::LocalLight l;
            l.position = f.position;
            l.color = srgb(f.cfg.lightColor) * f.cfg.lightIntensity * 0.25f;
            l.range = f.cfg.lightRange * 0.5f;
            l.key = 0xF1E1000000ull + static_cast<std::uint64_t>(&f - flying_.data());
            lights.push_back({glm::distance(view.cameraPosition, l.position), l});
        }
    std::sort(lights.begin(), lights.end(), [](const Lit& a, const Lit& b) { return a.distance < b.distance; });
    for (std::size_t i = 0; i < lights.size() && i < static_cast<std::size_t>(kMaxFireLights); ++i)
        view.lights.push_back(lights[i].light);
}

namespace {
// Model-space matrix of the view model's `bone` in the current pose; false without it.
bool boneMatrix(const engine::Weapon& w, const std::string& bone, glm::mat4& out, float* scale = nullptr)
{
    if (bone.empty() || !w.viewModel || !w.viewModel->skeleton)
        return false;
    const engine::Skeleton& skel = *w.viewModel->skeleton;
    int node = -1;
    for (std::size_t i = 0; i < skel.nodes.size(); ++i)
        if (skel.nodes[i].name == bone) {
            node = static_cast<int>(i);
            break;
        }
    if (node < 0)
        return false;
    static thread_local std::vector<glm::mat4> globals;
    engine::evaluatePose(skel, &w.animator, globals);
    if (static_cast<std::size_t>(node) >= globals.size())
        return false;
    out = globals[static_cast<std::size_t>(node)];
    // Scale against the skeleton's root (the import may scale the whole rig): ~1 normally,
    // ~0 when the clip has shrunk the bone away (a thrown bottle is no longer in the hand).
    int root = node;
    while (skel.nodes[static_cast<std::size_t>(root)].parent >= 0)
        root = skel.nodes[static_cast<std::size_t>(root)].parent;
    const float rootScale = glm::length(glm::vec3(globals[static_cast<std::size_t>(root)][0]));
    if (scale)
        *scale = rootScale > 1e-8f ? glm::length(glm::vec3(out[0])) / rootScale : 0.0f;
    return true;
}
} // namespace

glm::vec3 GameFire::heldReleasePoint(const engine::Weapon& held, const glm::mat4& modelTransform) const
{
    glm::mat4 bone;
    if (boneMatrix(held, held.settings.thrown.trailBone, bone))
        return glm::vec3(modelTransform * bone * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    return held.settings.thrown.handOffset;
}

void GameFire::fillHeld(const engine::Weapon* held, const glm::mat4& modelTransform, const glm::mat4& viewMatrix, engine::OverlayPass& overlay)
{
    bool show = false;
    glm::vec3 at(0.0f);
    if (held && held->settings.throwable() && held->holding() && !held->settings.thrown.effectTrail.empty()) {
        glm::mat4 bone;
        float scale = 0.0f;
        if (boneMatrix(*held, held->settings.thrown.trailBone, bone, &scale)) {
            // The bottle is hidden (scaled to nothing) between the release and the next draw.
            show = scale > 0.2f;
            at = glm::vec3(modelTransform * bone * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        }
    }
    if (show && held->settings.thrown.effectTrail != heldEffect_ && heldFx_) {
        overlay_.kill(heldFx_);
        heldFx_ = 0;
    }
    if (!show) {
        if (heldFx_) {
            overlay_.kill(heldFx_);
            heldFx_ = 0;
        }
    } else {
        // Flames rise toward the world's up, whatever way the camera looks.
        const glm::mat3 rotation = engine::basisFromUp(glm::normalize(glm::mat3(viewMatrix) * glm::vec3(0.0f, 1.0f, 0.0f)));
        if (!heldFx_ || !overlay_.alive(heldFx_)) {
            heldEffect_ = held->settings.thrown.effectTrail;
            if (auto fx = effect(heldEffect_))
                heldFx_ = overlay_.spawn(fx, at, rotation);
        } else {
            overlay_.setTransform(heldFx_, at, rotation);
        }
    }
    overlay_.appendSprites(glm::vec3(0.0f), glm::mat3(1.0f), overlay.sprites);
}
