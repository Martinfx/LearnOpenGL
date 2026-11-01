#include "GameAnomalies.h"
#include "GameAudio.h"

#include <engine/Log.h>
#include <engine/Physics.h>
#include <engine/PlayerController.h>
#include <engine/Scripting.h>
#include <engine/Ui2D.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

std::filesystem::path fromUtf8(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

constexpr float kBoltGravity = 9.81f;
constexpr std::size_t kMaxBolts = 24;

} // namespace

void GameAnomalies::init(const std::filesystem::path& assetDir, GameAudio& audio)
{
    assetDir_ = assetDir;
    audio_ = &audio;
    for (const char* s : {"sounds/anomalies/geiger_click.sound", "sounds/anomalies/detector_beep.sound", "sounds/anomalies/bolt_throw.sound",
                          "sounds/anomalies/bolt_hit.sound"})
        audio.preload(asset(s));
}

std::filesystem::path GameAnomalies::asset(const std::string& relative) const
{
    if (relative.empty())
        return {};
    std::filesystem::path p = fromUtf8(relative);
    return (p.is_relative() ? assetDir_ / p : p).lexically_normal();
}

std::shared_ptr<const engine::ParticleEffect> GameAnomalies::effect(const std::string& relative)
{
    if (relative.empty())
        return nullptr;
    if (auto it = effects_.find(relative); it != effects_.end())
        return it->second;
    std::string error;
    auto fx = engine::loadParticleEffect(asset(relative), &error);
    if (!fx)
        LOG_WARN("anomaly", "Effect: %s", error.c_str());
    effects_[relative] = fx;
    return fx;
}

void GameAnomalies::startLevel(const engine::Scene& scene)
{
    for (const Live& l : live_)
        if (l.loop)
            audio_->system().stop(l.loop);
    particles_.clear();
    live_.clear();
    bolts_.clear();
    pendingDamage_.clear();
    dose_ = doseRate_ = signal_ = 0.0f;
    field_.build(scene);
    for (const engine::AnomalyField::State& s : field_.anomalies()) {
        Live l;
        const engine::AnomalySettings& cfg = s.settings;
        if (auto fx = effect(cfg.idleEffect)) {
            // Idle effects are authored for a field of 1 m radius: wider fields stretch the
            // emitter across (x, z) and emit more, so the density stays the same.
            const glm::mat3 spread(glm::vec3(s.radius, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, s.radius));
            l.idle = particles_.spawn(fx, s.center, spread);
            particles_.setRateScale(l.idle, std::clamp(s.radius * s.radius, 0.5f, 12.0f));
        }
        effect(cfg.burstEffect); // load now, not in the middle of a fight
        audio_->preload(asset(cfg.burstSound));
        if (!cfg.loopSound.empty()) {
            const glm::vec3 at = s.center + glm::vec3(0.0f, std::min(1.0f, s.height * 0.5f), 0.0f);
            l.loop = audio_->start(asset(cfg.loopSound), &at, 1.0f);
        }
        live_.push_back(l);
    }
    if (!field_.anomalies().empty())
        LOG_INFO("anomaly", "%zu anomalies in the level (G throws a bolt)", field_.anomalies().size());
}

void GameAnomalies::throwBolt(glm::vec3 eye, glm::vec3 forward, glm::vec3 playerVelocity)
{
    if (bolts_.size() >= kMaxBolts) // the oldest one goes
        bolts_.erase(bolts_.begin());
    Bolt b;
    b.position = eye + forward * 0.4f + glm::vec3(0.0f, -0.1f, 0.0f);
    b.velocity = glm::normalize(forward + glm::vec3(0.0f, 0.18f, 0.0f)) * 13.0f + playerVelocity * 0.5f;
    b.spin = std::uniform_real_distribution<float>(8.0f, 16.0f)(rng_);
    bolts_.push_back(b);
    audio_->play(asset("sounds/anomalies/bolt_throw.sound"), eye, 0.7f);
}

void GameAnomalies::stepBolt(Bolt& b, float dt, const engine::PhysicsWorld& physics)
{
    b.age += dt;
    if (b.resting)
        return;
    b.velocity.y -= kBoltGravity * dt;
    const glm::vec3 move = b.velocity * dt;
    const float len = glm::length(move);
    if (len < 1e-6f)
        return;
    if (auto hit = physics.raycast(b.position, move / len, len + 0.02f)) {
        b.position = hit->point + hit->normal * 0.02f;
        const float into = glm::dot(b.velocity, hit->normal);
        b.velocity -= (1.0f + 0.35f) * into * hit->normal; // bounce
        b.velocity *= 0.6f;                                 // friction
        if (-into > 1.5f)
            audio_->play(asset("sounds/anomalies/bolt_hit.sound"), b.position, std::min(1.0f, -into / 8.0f));
        if (glm::length(b.velocity) < 0.5f && hit->normal.y > 0.6f) {
            b.resting = true;
            b.velocity = glm::vec3(0.0f);
        }
    } else {
        b.position += move;
    }
    if (b.position.y < -100.0f)
        b.dead = true;
}

void GameAnomalies::handleEvent(const engine::AnomalyEvent& ev)
{
    if (ev.anomaly < 0 || ev.anomaly >= static_cast<int>(live_.size()))
        return;
    const engine::AnomalyField::State& s = field_.anomalies()[static_cast<std::size_t>(ev.anomaly)];
    Live& l = live_[static_cast<std::size_t>(ev.anomaly)];
    const engine::AnomalySettings& cfg = s.settings;
    using engine::AnomalyKind;
    static const bool log = std::getenv("FPS_ENGINE_ANOMALY_LOG") != nullptr; // test hook
    if (log)
        LOG_INFO("anomaly", "%s: %s at %.1f %.1f %.1f", engine::anomalyKindInfo(cfg.kind).id,
                    ev.type == engine::AnomalyEvent::Charge ? "charge" : ev.type == engine::AnomalyEvent::Burst ? "burst" : "end",
                    ev.position.x, ev.position.y, ev.position.z);
    switch (ev.type) {
    case engine::AnomalyEvent::Charge:
        // Build-up: a crackle for the discharge, a groan for the crusher, a rumble for the vent.
        if (cfg.kind == AnomalyKind::Discharge) {
            if (auto fx = effect("particles/anomaly_discharge_crackle.particle"))
                particles_.spawn(fx, ev.position);
            audio_->play(asset("sounds/anomalies/discharge_crackle.sound"), ev.position, 0.8f);
        } else if (cfg.kind == AnomalyKind::Crusher) {
            audio_->play(asset("sounds/anomalies/crusher_charge.sound"), ev.position);
        } else if (cfg.kind == AnomalyKind::Vent) {
            audio_->play(asset("sounds/anomalies/vent_charge.sound"), ev.position);
        }
        break;
    case engine::AnomalyEvent::Burst: {
        auto fx = effect(cfg.burstEffect);
        if (cfg.kind == AnomalyKind::Vent) {
            if (l.burst)
                particles_.stop(l.burst);
            const float r = std::clamp(s.radius, 0.5f, 4.0f);
            const float h = std::max(0.5f, cfg.get("column") / 5.0f); // column effect is authored 5 m tall
            l.burst = fx ? particles_.spawn(fx, ev.position, glm::mat3(glm::vec3(r, 0, 0), glm::vec3(0, h, 0), glm::vec3(0, 0, r))) : 0;
            if (l.burst)
                particles_.setRateScale(l.burst, std::clamp(r * r, 0.5f, 6.0f));
        } else if (fx) {
            const float r = std::clamp(s.radius, 0.5f, 6.0f);
            particles_.spawn(fx, ev.position, glm::mat3(glm::vec3(r, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, r)));
        }
        audio_->play(asset(cfg.burstSound), ev.position);
        break;
    }
    case engine::AnomalyEvent::End:
        if (l.burst) {
            particles_.stop(l.burst);
            l.burst = 0;
        }
        break;
    }
}

void GameAnomalies::fixedUpdate(float dt, engine::Scene& scene, const engine::PhysicsWorld& physics, engine::PlayerController& player,
                                engine::ScriptSystem& scripts)
{
    if (field_.anomalies().empty() && bolts_.empty())
        return;
    for (Bolt& b : bolts_)
        stepBolt(b, dt, physics);

    // Everything the anomalies can act on.
    bodies_.clear();
    bodyEntity_.clear();
    playerFeet_ = player.feet();
    bodies_.push_back({player.feet(), player.settings.height, player.velocity(), false});
    bodyEntity_.push_back(-1);
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const engine::Entity& e = scene.entities[i];
        if (!e.alive || e.helper() || (e.scriptPath.empty() && e.type != "enemy"))
            continue;
        bodies_.push_back({e.position, e.size.y, glm::vec3(0.0f), false});
        bodyEntity_.push_back(static_cast<int>(i));
    }
    for (std::size_t i = 0; i < bolts_.size(); ++i) {
        bodies_.push_back({bolts_[i].position, 0.05f, bolts_[i].velocity, true});
        bodyEntity_.push_back(-2 - static_cast<int>(i));
    }

    events_.clear();
    field_.step(dt, bodies_, influences_, events_);

    for (std::size_t b = 0; b < bodies_.size(); ++b) {
        const engine::AnomalyInfluence& in = influences_[b];
        const int who = bodyEntity_[b];
        if (who == -1) { // the player
            if (glm::dot(in.drift, in.drift) > 0.0f)
                player.drift(in.drift);
            if (glm::dot(in.impulse, in.impulse) > 0.0f)
                player.push(in.impulse);
            if (in.slow > 0.0f)
                player.slow(in.slow);
            if (in.teleport) {
                glm::vec3 to = in.teleportTo;
                if (auto ground = physics.raycast(to + glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 30.0f))
                    to = ground->point + glm::vec3(0.0f, 0.05f, 0.0f);
                player.teleport(to);
            }
            float damage = in.damage;
            // Radiation: the dose grows with the dose rate, wears off slowly and eats health.
            doseRate_ = in.radiation;
            dose_ = std::max(0.0f, dose_ + (in.radiation - player.settings.radiationDecay) * dt);
            healthDrain_ += dose_ / 100.0f * player.settings.radiationDamage * dt;
            if (healthDrain_ >= 0.25f) {
                damage += healthDrain_;
                healthDrain_ = 0.0f;
            }
            if (damage > 0.0f) {
                scripts.setPlayerHealth(scripts.playerHealth() - damage);
                hurtTimer_ -= dt;
                if (in.damage > 0.0f && (in.damage > 5.0f || hurtTimer_ <= 0.0f)) {
                    audio_->play2D(scene.player.hurtSound);
                    hurtTimer_ = 0.7f;
                }
            }
        } else if (who >= 0) { // a character
            if (glm::dot(in.drift, in.drift) > 0.0f || glm::dot(in.impulse, in.impulse) > 0.0f || in.slow > 0.0f)
                scripts.pushEntity(who, in.impulse, in.drift, in.slow);
            if (in.teleport) {
                glm::vec3 to = in.teleportTo;
                if (auto ground = physics.raycast(to + glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 30.0f))
                    to = ground->point + glm::vec3(0.0f, 0.05f, 0.0f);
                scripts.teleportEntity(who, to);
            }
            // Radiation hurts them too (no dose bookkeeping: the rate directly).
            float& pending = pendingDamage_[who];
            pending += in.damage + in.radiation * 0.02f * dt;
            if (pending >= 2.0f) {
                scripts.damageEntity(who, pending);
                pending = 0.0f;
            }
        } else { // a bolt
            Bolt& bolt = bolts_[static_cast<std::size_t>(-2 - who)];
            if (in.damage > 0.0f) { // dissolved in acid
                bolt.dead = true;
                continue;
            }
            if (glm::dot(in.drift, in.drift) > 0.0f) {
                bolt.resting = false;
                bolt.position += in.drift * dt;
            }
            if (glm::dot(in.impulse, in.impulse) > 0.0f) {
                bolt.resting = false;
                bolt.velocity += in.impulse;
            }
            if (in.teleport) {
                bolt.position = in.teleportTo + glm::vec3(0.0f, 1.0f, 0.0f);
                bolt.resting = false;
                bolt.velocity = glm::vec3(0.0f);
            }
        }
    }
    for (const engine::AnomalyEvent& ev : events_)
        handleEvent(ev);
    std::erase_if(bolts_, [](const Bolt& b) { return b.dead || b.age > 60.0f; });
}

void GameAnomalies::update(float dt, const engine::PhysicsWorld& physics, glm::vec3 eye)
{
    if (field_.anomalies().empty() && bolts_.empty())
        return;
    particles_.update(dt);
    const auto& states = field_.anomalies();
    for (std::size_t i = 0; i < states.size() && i < live_.size(); ++i) {
        const auto& s = states[i];
        Live& l = live_[i];
        // Loops swell while the anomaly works.
        if (l.loop)
            audio_->system().setVolume(l.loop, 0.55f + 0.45f * s.activity, 0.1f);
        // The warp shows only with a clear line of sight (it is drawn over everything).
        const glm::vec3 mid = s.center + glm::vec3(0.0f, std::min(1.0f, s.height * 0.5f), 0.0f);
        const glm::vec3 to = mid - eye;
        const float dist = glm::length(to);
        float target = 0.0f;
        if (dist < 90.0f && s.activity > 0.01f) {
            target = 1.0f;
            if (dist > s.radius + 0.5f)
                if (auto hit = physics.raycast(eye, to / dist, dist - s.radius * 0.5f))
                    target = 0.0f;
        }
        l.visible += (target - l.visible) * std::min(1.0f, dt * 6.0f);
    }

    // Detector: beeps faster the closer the nearest anomaly field is.
    signal_ = field_.detectorSignal(playerFeet_ + glm::vec3(0.0f, 1.0f, 0.0f));
    beepFlash_ = std::max(0.0f, beepFlash_ - dt * 6.0f);
    clickFlash_ = std::max(0.0f, clickFlash_ - dt * 10.0f);
    if (signal_ > 0.0f) {
        beepTimer_ -= dt;
        if (beepTimer_ <= 0.0f) {
            audio_->play2D(asset("sounds/anomalies/detector_beep.sound"), 0.35f + 0.4f * signal_);
            beepFlash_ = 1.0f;
            beepTimer_ = 1.4f - 1.28f * std::pow(signal_, 0.8f);
        }
    } else {
        beepTimer_ = std::min(beepTimer_, 0.2f);
    }
    // Geiger counter: random clicks, their rate follows the dose rate.
    if (doseRate_ > 0.05f) {
        const float rate = std::min(90.0f, 1.5f + doseRate_ * 1.6f); // clicks per second
        const float chance = 1.0f - std::exp(-rate * dt);
        int clicks = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_) < chance ? 1 : 0;
        if (rate * dt > 1.0f)
            clicks = static_cast<int>(rate * dt);
        for (int c = 0; c < std::min(clicks, 3); ++c)
            audio_->play2D(asset("sounds/anomalies/geiger_click.sound"), 0.55f);
        if (clicks)
            clickFlash_ = 1.0f;
    }
}

void GameAnomalies::fill(engine::RenderView& view, const glm::mat4& viewMatrix) const
{
    const glm::mat3 camera(glm::inverse(viewMatrix));
    particles_.appendSprites(view.cameraPosition, camera, view.sprites);
    particles_.appendLights(view.cameraPosition, view.lights, 4);
    const auto& states = field_.anomalies();
    for (std::size_t i = 0; i < states.size() && i < live_.size(); ++i) {
        const auto& s = states[i];
        const float strength = s.activity * live_[i].visible;
        if (strength < 0.01f)
            continue;
        using engine::AnomalyKind;
        engine::ScreenDistortion d;
        d.position = s.center + glm::vec3(0.0f, std::min(1.0f, s.height * 0.5f), 0.0f);
        d.radius = s.radius;
        d.strength = strength;
        switch (s.settings.kind) {
        case AnomalyKind::Crusher:
        case AnomalyKind::Rift:
            d.lens = true;
            break;
        case AnomalyKind::Vent: // haze over the crack, taller while it burns
            d.radius = std::max(s.radius, 1.2f) * (1.0f + s.activity);
            d.position = s.center + glm::vec3(0.0f, d.radius * 0.8f, 0.0f);
            break;
        case AnomalyKind::Acid:
            d.position = s.center + glm::vec3(0.0f, 0.4f, 0.0f);
            break;
        default:
            break;
        }
        view.distortions.push_back(d);
    }
    // Ground marks under the fields (soft round sprites lying on the ground): pressed
    // soil under a crusher, scorch around discharges and vents, the sludge of an acid pool.
    engine::SpriteBatch marks, glows;
    marks.decal = glows.decal = false; // plain flat sprites, 3 cm above the ground
    marks.distance = 1e6f; // under every other effect: drawn first
    glows.distance = 1e6f - 1.0f;
    glows.blend = engine::SpriteBlend::Additive;
    glows.lit = false;
    auto flat = [](glm::vec3 at, float radius, glm::vec4 color) {
        engine::SpriteInstance q;
        q.position = at + glm::vec3(0.0f, 0.03f, 0.0f);
        q.axisX = glm::vec3(0.0f, 0.0f, radius); // normal (axisX x axisY) points up: lit from above
        q.axisY = glm::vec3(radius, 0.0f, 0.0f);
        q.color = color;
        return q;
    };
    for (std::size_t i = 0; i < states.size(); ++i) {
        const auto& s = states[i];
        if (glm::distance(s.center, view.cameraPosition) > 120.0f)
            continue;
        using engine::AnomalyKind;
        // The soft round sprite is mostly edge: a few stacked layers make a solid middle with a soft rim.
        auto patch = [&](float radius, glm::vec4 color, int layers) {
            for (int k = 0; k < layers; ++k)
                marks.instances.push_back(flat(s.center, radius * (1.0f - 0.18f * static_cast<float>(k)), color));
        };
        switch (s.settings.kind) {
        case AnomalyKind::Crusher:
            patch(s.radius * 0.9f, {0.08f, 0.07f, 0.06f, 0.35f}, 3);
            patch(s.settings.get("core") * 1.8f, {0.03f, 0.03f, 0.03f, 0.5f}, 2);
            break;
        case AnomalyKind::Discharge:
            patch(s.radius * 0.7f, {0.03f, 0.03f, 0.035f, 0.4f}, 2);
            glows.instances.push_back(flat(s.center, s.radius * 0.5f, glm::vec4(0.15f, 0.25f, 0.6f, 0.25f + 0.6f * s.activity)));
            break;
        case AnomalyKind::Vent:
            patch(s.radius * 1.4f, {0.02f, 0.018f, 0.015f, 0.55f}, 3);
            glows.instances.push_back(flat(s.center, s.radius * 0.6f, glm::vec4(1.2f, 0.35f, 0.08f, 0.5f + 0.5f * s.activity)));
            break;
        case AnomalyKind::Acid:
            patch(s.radius * 1.1f, {0.09f, 0.12f, 0.04f, 0.6f}, 4);
            glows.instances.push_back(flat(s.center, s.radius * 0.8f, {0.25f, 0.45f, 0.05f, 0.3f}));
            break;
        default:
            break;
        }
    }
    if (!marks.instances.empty())
        view.sprites.push_back(std::move(marks));
    if (!glows.instances.empty())
        view.sprites.push_back(std::move(glows));
    for (const Bolt& b : bolts_)
        view.worldBoxes.push_back({b.position, glm::vec3(0.035f, 0.035f, 0.035f), glm::vec3(0.42f, 0.4f, 0.38f), false});
}

void GameAnomalies::drawHud(engine::Ui2D& ui, int width, int height) const
{
    (void)width;
    const float H = static_cast<float>(height);
    const float margin = std::round(H * 0.025f);
    const float text = std::round(std::clamp(H * 0.022f, 12.0f, 26.0f));
    const float barW = std::round(text * 7.0f), barH = std::max(4.0f, std::round(text * 0.35f));
    const glm::vec4 frame(0.06f, 0.065f, 0.05f, 0.75f), bone(0.86f, 0.82f, 0.72f, 1.0f), dim(0.55f, 0.55f, 0.5f, 1.0f);
    glm::vec2 pos(margin, H - margin - text * 4.2f);
    const glm::vec2 size(barW + text * 4.6f, text * 4.2f);
    ui.rect(pos, size, frame);
    const float x = pos.x + text * 0.6f;
    // Detector: LED that blinks with the beeps, and the signal strength.
    {
        const float y = pos.y + text * 0.5f;
        const glm::vec4 led = signal_ > 0.0f ? glm::mix(glm::vec4(0.25f, 0.12f, 0.05f, 1.0f), glm::vec4(1.0f, 0.55f, 0.1f, 1.0f), beepFlash_)
                                             : glm::vec4(0.15f, 0.15f, 0.13f, 1.0f);
        ui.rect({x, y + text * 0.2f}, {text * 0.6f, text * 0.6f}, led);
        ui.text("DET", {x + text * 1.0f, y}, text, signal_ > 0.0f ? bone : dim, true);
        const glm::vec2 bar(x + text * 3.4f, y + text * 0.35f);
        ui.rect(bar, {barW, barH}, {0.15f, 0.15f, 0.13f, 0.9f});
        ui.rect(bar, {barW * signal_, barH}, {0.95f, 0.55f, 0.15f, 0.95f});
    }
    // Radiation: dose (bar to 500 rad, yellow then red) and the dose rate.
    {
        const float y = pos.y + text * 2.2f;
        const float danger = std::clamp(dose_ / 500.0f, 0.0f, 1.0f);
        const glm::vec4 icon = doseRate_ > 0.05f ? glm::mix(glm::vec4(0.9f, 0.8f, 0.15f, 1.0f), glm::vec4(1.0f, 1.0f, 0.6f, 1.0f), clickFlash_)
                                                 : (dose_ > 1.0f ? glm::vec4(0.75f, 0.65f, 0.15f, 1.0f) : dim);
        // Trefoil-ish mark: three wedges around a dot.
        const glm::vec2 c(x + text * 0.3f, y + text * 0.5f);
        const float r = text * 0.42f;
        for (int k = 0; k < 3; ++k) {
            const float a0 = -1.5708f + static_cast<float>(k) * 2.0944f - 0.5f, a1 = a0 + 1.0f;
            ui.triangle(c + glm::vec2(std::cos(a0), std::sin(a0)) * r * 0.35f, c + glm::vec2(std::cos(a0), std::sin(a0)) * r,
                        c + glm::vec2(std::cos(a1), std::sin(a1)) * r, icon);
            ui.triangle(c + glm::vec2(std::cos(a0), std::sin(a0)) * r * 0.35f, c + glm::vec2(std::cos(a1), std::sin(a1)) * r,
                        c + glm::vec2(std::cos(a1), std::sin(a1)) * r * 0.35f, icon);
        }
        ui.rect(c - glm::vec2(r * 0.15f), glm::vec2(r * 0.3f), icon);
        ui.text("RAD", {x + text * 1.0f, y}, text, dose_ > 1.0f || doseRate_ > 0.05f ? bone : dim, true);
        const glm::vec2 bar(x + text * 3.4f, y + text * 0.35f);
        ui.rect(bar, {barW, barH}, {0.15f, 0.15f, 0.13f, 0.9f});
        ui.rect(bar, {barW * danger, barH}, glm::mix(glm::vec4(0.9f, 0.8f, 0.15f, 0.95f), glm::vec4(0.95f, 0.2f, 0.1f, 0.95f), danger));
        char line[64];
        std::snprintf(line, sizeof line, "%.0f rad  +%.1f/s", dose_, doseRate_);
        ui.text(line, {bar.x, y + text * 0.9f}, text * 0.8f, dim);
    }
}
