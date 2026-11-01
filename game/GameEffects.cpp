#include <engine/Log.h>
#include "GameEffects.h"

#include <engine/EntityIO.h>
#include "GameAudio.h"

#include <engine/Log.h>
#include <engine/Material.h>
#include <engine/Physics.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr std::size_t kMaxCasings = 96;
constexpr float kCasingLife = 30.0f; // s on the ground before they go

std::filesystem::path fromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

} // namespace

void GameEffects::init(const std::filesystem::path& assetDir, const engine::SurfaceTable& surfaces)
{
    assetDir_ = assetDir;
    surfaces_ = &surfaces;
    for (const engine::SurfaceType& t : surfaces.types) {
        effect(t.effect); // load now: the first shot does not hitch
        if (!t.decal.empty() && !decalTextures_.count(t.decal))
            decalTextures_[t.decal] = engine::loadTextureAsset(t.decal, nullptr, true);
    }
}

std::shared_ptr<const engine::ParticleEffect> GameEffects::effect(const std::filesystem::path& file)
{
    if (file.empty())
        return nullptr;
    if (auto it = effects_.find(file); it != effects_.end())
        return it->second;
    std::string error;
    auto fx = engine::loadParticleEffect(file, &error);
    if (!fx)
        LOG_WARN("effects", "%s\n", error.c_str());
    effects_[file] = fx; // failures are reported once
    return fx;
}

std::shared_ptr<const engine::Model> GameEffects::model(const std::filesystem::path& file)
{
    if (file.empty())
        return nullptr;
    if (auto it = models_.find(file); it != models_.end())
        return it->second;
    auto m = std::make_shared<engine::Model>();
    std::string error;
    if (!engine::loadModelFile(*m, file, &error)) {
        LOG_WARN("effects", "%s\n", error.c_str());
        m.reset();
    }
    models_[file] = m;
    return m;
}

void GameEffects::startLevel(const engine::Scene& scene)
{
    world_.clear();
    overlay_.clear();
    decals_.clear();
    casings_.clear();
    entityEffects_.clear();
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const engine::Entity& e = scene.entities[i];
        if (e.type != "particle")
            continue;
        const auto it = e.properties.find("effect");
        if (it == e.properties.end() || it->second.empty())
            continue;
        std::filesystem::path p = fromUtf8(it->second);
        if (p.is_relative())
            p = assetDir_ / p;
        const glm::mat3 yaw(glm::rotate(glm::mat4(1.0f), glm::radians(-e.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)));
        const auto handle = world_.spawn(effect(p.lexically_normal()), e.position, yaw);
        if (handle)
            entityEffects_.push_back({i, handle});
    }
    const engine::Environment& env = scene.environment;
    rain_ = world_.spawn(effect(env.rainEffect.empty() ? (assetDir_ / "particles/rain.particle").lexically_normal() : env.rainEffect),
                         glm::vec3(0.0f));
    world_.setRateScale(rain_, env.rain);
}

void GameEffects::impact(glm::vec3 point, glm::vec3 normal, const std::string& surface, bool character)
{
    const engine::SurfaceType* t = surfaces_ ? surfaces_->find(character ? std::string("flesh") : surface) : nullptr;
    if (!t)
        return;
    if (auto fx = effect(t->effect))
        world_.spawn(fx, point, engine::basisFromUp(normal));
    if (!character && t->decalSize > 0.0f && !t->decal.empty()) {
        auto& tex = decalTextures_[t->decal];
        if (!tex)
            tex = engine::loadTextureAsset(t->decal, nullptr, true);
        decals_.add(tex, t->decalColumns, t->decalRows, t->decalRow, point, normal, t->decalSize);
    }
}

void GameEffects::ricochet(glm::vec3 point, glm::vec3 direction, const engine::SurfaceType& surface)
{
    if (auto fx = effect(surface.ricochetEffect))
        world_.spawn(fx, point, engine::basisFromUp(direction));
}

void GameEffects::shot(const engine::WeaponSettings& weapon, const glm::mat4& view, glm::vec3 muzzleView, glm::vec3 playerVelocity)
{
    const glm::mat4 toWorld = glm::inverse(view);
    const glm::mat3 camera(toWorld);
    // Barrel along -Z in view space: the effects' +Y points there.
    const glm::mat3 barrelView = engine::basisFromUp(glm::vec3(0.0f, 0.0f, -1.0f));
    if (auto fx = effect(weapon.effectFlash))
        overlay_.spawn(fx, muzzleView, barrelView);
    if (auto fx = effect(weapon.effectSmoke))
        world_.spawn(fx, glm::vec3(toWorld * glm::vec4(muzzleView, 1.0f)), camera * barrelView);
    if (auto m = model(weapon.casingModel)) {
        if (casings_.size() >= kMaxCasings)
            casings_.erase(casings_.begin());
        std::uniform_real_distribution<float> jitter(-0.25f, 0.25f);
        Casing c;
        c.model = m;
        c.position = glm::vec3(toWorld * glm::vec4(weapon.casingOffset, 1.0f));
        const glm::vec3 v = weapon.casingVelocity * glm::vec3(1.0f + jitter(rng_), 1.0f + jitter(rng_), 1.0f);
        c.velocity = camera * glm::vec3(v.x, v.y, v.z) + playerVelocity;
        c.orientation = camera; // case axis (+Z) along the barrel
        c.axis = glm::normalize(camera * glm::vec3(0.2f + jitter(rng_), 0.1f, 1.0f));
        c.spin = 18.0f + jitter(rng_) * 20.0f;
        casings_.push_back(c);
    }
}

void GameEffects::update(float dt, const engine::Scene& scene, const engine::PhysicsWorld& physics, glm::vec3 eye, GameAudio& audio)
{
    if (rain_) {
        world_.setTransform(rain_, eye + glm::vec3(0.0f, 9.0f, 0.0f));
        world_.setRateScale(rain_, engine::environmentAt(scene.environment, scene.sunDirection).rain);
    }
    for (const auto& [entity, handle] : entityEffects_) // switched by Enable / Disable: no new particles while off
        if (entity < scene.entities.size())
            world_.setRateScale(handle, scene.entities[entity].alive && engine::entityEnabled(scene.entities[entity]) ? 1.0f : 0.0f);
    world_.update(dt);
    overlay_.update(dt);
    decals_.update(dt);
    // Spent cases: fall, bounce off what the player collides with, click on the first hits.
    // Small sub-steps so a slow frame does not make them hop.
    const int substeps = std::clamp(static_cast<int>(std::ceil(dt / 0.008f)), 1, 16);
    const float h = dt / static_cast<float>(substeps);
    for (Casing& c : casings_) {
        c.age += dt;
        for (int step = 0; step < substeps && !c.resting; ++step)
            stepCasing(c, h, physics, audio);
        if (c.position.y < scene.player.killY)
            c.age = kCasingLife + 1.0f;
    }
    std::erase_if(casings_, [](const Casing& c) { return c.age > kCasingLife; });
}

void GameEffects::stepCasing(Casing& c, float dt, const engine::PhysicsWorld& physics, GameAudio& audio)
{
    {
        c.velocity.y -= 9.81f * dt;
        const glm::vec3 move = c.velocity * dt;
        const float len = glm::length(move);
        if (len > 1e-6f) {
            if (auto hit = physics.raycast(c.position, move / len, len + 0.01f)) {
                const glm::vec3 n = hit->normal;
                const float into = glm::dot(c.velocity, n);
                const float speed = glm::length(c.velocity);
                c.position = hit->point + n * 0.006f;
                // Bounce: lose most of the normal speed, slide with friction, tumble a little.
                c.velocity = (c.velocity - n * into) * 0.55f - n * into * 0.3f;
                c.spin *= 0.5f;
                if (c.bounces < 3 && speed > 1.0f && surfaces_) {
                    const engine::SurfaceType* t = surfaces_->find(hit->surface);
                    if (t && !t->casing.empty())
                        audio.play(t->casing, c.position, std::clamp(speed / 5.0f, 0.3f, 1.0f) * (c.bounces == 0 ? 1.0f : 0.6f));
                }
                ++c.bounces;
                // Settles once a hit is too soft to throw it back up.
                if ((glm::length(c.velocity) < 0.25f || -into < 1.2f) && n.y > 0.6f) {
                    c.resting = true;
                    // Lie on the side: the case's axis (+Z of the model) flat on the ground.
                    glm::vec3 along = c.orientation[2] - n * glm::dot(c.orientation[2], n);
                    if (glm::dot(along, along) < 1e-6f)
                        along = glm::cross(n, glm::vec3(1.0f, 0.0f, 0.0f));
                    along = glm::normalize(along);
                    c.orientation = glm::mat3(glm::cross(n, along), n, along);
                    c.position = hit->point + n * 0.005f;
                }
            } else {
                c.position += move;
            }
        }
        if (!c.resting)
            c.orientation = glm::mat3(glm::rotate(glm::mat4(1.0f), c.spin * dt, c.axis)) * c.orientation;
    }
}

void GameEffects::fill(engine::RenderView& view, const glm::mat4& viewMatrix, engine::OverlayPass& overlay) const
{
    const glm::mat4 toWorld = glm::inverse(viewMatrix);
    const glm::mat3 camera(toWorld);
    decals_.appendSprites(view.cameraPosition, 80.0f, view.sprites);
    world_.appendSprites(view.cameraPosition, camera, view.sprites);
    overlay_.appendSprites(glm::vec3(0.0f), glm::mat3(1.0f), overlay.sprites);
    for (const Casing& c : casings_) {
        if (!c.model)
            continue;
        glm::mat4 m(c.orientation);
        m[3] = glm::vec4(c.position, 1.0f);
        view.extraModels.emplace_back(c.model.get(), m);
    }
}
