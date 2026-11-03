#include "UiScale.h"
#include "GameAnomalies.h"
#include "GameAudio.h"

#include <engine/Log.h>
#include <engine/Material.h>
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
                          "sounds/anomalies/bolt_hit.sound", "sounds/anomalies/discharge_arc.sound"})
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
    arcs_.clear();
    pendingDamage_.clear();
    if (!arcTexture_) {
        arcTexture_ = engine::loadTextureAsset(asset("textures/particles/arc.png"), nullptr, true);
        sheenTexture_ = engine::loadTextureAsset(asset("textures/particles/acid_sheen.png"), nullptr, true);
        marksTexture_ = engine::loadTextureAsset(asset("textures/particles/anomaly_marks_2x2.png"), nullptr, true);
    }
    dose_ = doseRate_ = signal_ = 0.0f;
    field_.build(scene);
    for (const engine::AnomalyField::State& s : field_.anomalies()) {
        Live l;
        const engine::AnomalySettings& cfg = s.settings;
        l.angle = std::uniform_real_distribution<float>(0.0f, 6.2831853f)(rng_);
        l.arcTimer = std::uniform_real_distribution<float>(0.0f, 0.5f)(rng_);
        // Radiation dust is a look option: "dust" scales it, 0 = invisible.
        const float density = cfg.kind == engine::AnomalyKind::Radiation ? cfg.get("dust") : 1.0f;
        if (auto fx = density > 0.0f ? effect(cfg.idleEffect) : nullptr) {
            // Idle effects are authored for a field of 1 m radius: wider fields stretch the
            // emitter across (x, z) and emit more, so the density stays the same.
            const glm::mat3 spread(glm::vec3(s.radius, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, s.radius));
            l.idle = particles_.spawn(fx, s.center, spread);
            particles_.setRateScale(l.idle, std::clamp(s.radius * s.radius, 0.5f, 12.0f) * density);
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
            // Build-up: a few arcs crawl from the spot over the ground.
            for (int k = 0; k < 3; ++k)
                spawnArc(ev.anomaly, ev.position + glm::vec3(0.0f, 0.1f, 0.0f), randomFieldPoint(s, 0.05f, 1.2f), 0.18f, 0.9f, 0.025f, 0.22f, 0.3f,
                         0.06f);
            l.flash = std::max(l.flash, 0.35f);
            audio_->play(asset("sounds/anomalies/discharge_crackle.sound"), ev.position, 0.8f);
        } else if (cfg.kind == AnomalyKind::Crusher) {
            audio_->play(asset("sounds/anomalies/crusher_charge.sound"), ev.position);
        } else if (cfg.kind == AnomalyKind::Vent) {
            audio_->play(asset("sounds/anomalies/vent_charge.sound"), ev.position);
            l.flash = std::max(l.flash, 0.3f);
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
        if (cfg.kind == AnomalyKind::Discharge) {
            // The strike: a thick bolt from the top of the field into the target, re-shaping
            // while it lasts (flicker), and arcs splashing from the target into the ground.
            const glm::vec3 top = s.center + glm::vec3(std::uniform_real_distribution<float>(-0.4f, 0.4f)(rng_) * s.radius,
                                                       std::max(1.5f, s.height), std::uniform_real_distribution<float>(-0.4f, 0.4f)(rng_) * s.radius);
            spawnArc(ev.anomaly, top, ev.position, 0.4f, 3.0f, 0.06f, 0.16f, 0.45f, 0.05f);
            spawnArc(ev.anomaly, top + glm::vec3(0.3f, 0.2f, -0.2f), ev.position, 0.3f, 1.6f, 0.035f, 0.22f, 0.35f, 0.04f);
            for (int k = 0; k < 5; ++k) {
                glm::vec3 g = randomFieldPoint(s, 0.0f, 0.05f);
                g = ev.position + (g - ev.position) * 0.7f;
                g.y = s.center.y + 0.05f;
                spawnArc(ev.anomaly, ev.position, g, 0.25f, 1.3f, 0.025f, 0.25f, 0.3f, 0.05f);
            }
            l.flash = 1.0f;
        } else if (cfg.kind == AnomalyKind::Vent || cfg.kind == AnomalyKind::Crusher || cfg.kind == AnomalyKind::Rift) {
            l.flash = 1.0f;
        } else if (cfg.kind == AnomalyKind::Acid) {
            l.flash = std::max(l.flash, 0.5f);
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
    time_ += dt;
    const auto& states = field_.anomalies();
    // Lightning: age, re-shape the flickering ones, drop the finished ones.
    for (Arc& a : arcs_) {
        a.age += dt;
        a.flicker = std::uniform_real_distribution<float>(0.55f, 1.0f)(rng_);
        if (a.rebuild > 0.0f && std::fmod(a.age, a.rebuild) < dt)
            shapeArc(a);
    }
    std::erase_if(arcs_, [](const Arc& a) { return a.age >= a.life; });
    for (std::size_t i = 0; i < states.size() && i < live_.size(); ++i) {
        const auto& s = states[i];
        Live& l = live_[i];
        l.flash = std::max(0.0f, l.flash - dt * (s.settings.kind == engine::AnomalyKind::Vent ? 1.5f : 7.0f));
        // Idle arcs crawl over a discharge field, more often while it charges.
        if (s.settings.kind == engine::AnomalyKind::Discharge && glm::distance(s.center, eye) < 80.0f) {
            if (const float rate = s.settings.get("arcs") * (1.0f + 3.0f * s.activity); rate > 0.0f) {
                l.arcTimer -= dt;
                if (l.arcTimer <= 0.0f) {
                    l.arcTimer = std::exponential_distribution<float>(rate)(rng_);
                    const glm::vec3 from = randomFieldPoint(s, 0.02f, 0.3f);
                    glm::vec3 to = randomFieldPoint(s, 0.1f, std::min(2.0f, s.height * 0.7f));
                    const float len = glm::distance(from, to);
                    if (len > 1.6f)
                        to = from + (to - from) * (1.6f / len);
                    spawnArc(static_cast<int>(i), from, to, std::uniform_real_distribution<float>(0.06f, 0.16f)(rng_), 0.8f, 0.018f, 0.25f, 0.2f,
                             0.0f);
                    l.flash = std::max(l.flash, 0.18f);
                    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_) < 0.4f)
                        audio_->play(asset("sounds/anomalies/discharge_arc.sound"), from, 0.6f);
                }
            }
        }
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

glm::vec3 GameAnomalies::randomFieldPoint(const engine::AnomalyField::State& s, float minHeight, float maxHeight)
{
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const float a = u(rng_) * 6.2831853f, r = std::sqrt(u(rng_)) * s.radius * 0.85f;
    return s.center + glm::vec3(std::cos(a) * r, minHeight + (maxHeight - minHeight) * u(rng_), std::sin(a) * r);
}

void GameAnomalies::spawnArc(int anomaly, glm::vec3 from, glm::vec3 to, float life, float intensity, float width, float jag, float branch,
                             float rebuild)
{
    if (arcs_.size() >= 96)
        return;
    Arc a;
    a.anomaly = anomaly;
    a.from = from;
    a.to = to;
    a.life = life;
    a.intensity = intensity;
    a.width = width;
    a.jag = jag;
    a.branch = branch;
    a.rebuild = rebuild;
    shapeArc(a);
    arcs_.push_back(std::move(a));
}

void GameAnomalies::shapeArc(Arc& arc)
{
    // Midpoint displacement: each pass splits every piece and pushes the new point
    // sideways by a share of the piece's length; side branches fork from some points.
    std::uniform_real_distribution<float> u(-1.0f, 1.0f), p(0.0f, 1.0f);
    auto bolt = [&](glm::vec3 from, glm::vec3 to, float width, float jag, int passes, std::vector<glm::vec3>& points) {
        points.assign({from, to});
        float amount = jag;
        for (int pass = 0; pass < passes; ++pass) {
            std::vector<glm::vec3> next;
            next.reserve(points.size() * 2);
            for (std::size_t k = 0; k + 1 < points.size(); ++k) {
                const glm::vec3 a = points[k], b = points[k + 1];
                const glm::vec3 d = b - a;
                const float len = glm::length(d);
                glm::vec3 side = glm::cross(d, glm::vec3(0.0f, 1.0f, 0.0f));
                if (glm::dot(side, side) < 1e-6f)
                    side = glm::cross(d, glm::vec3(1.0f, 0.0f, 0.0f));
                side = glm::normalize(side);
                const glm::vec3 up = glm::normalize(glm::cross(side, d));
                next.push_back(a);
                next.push_back((a + b) * 0.5f + (side * u(rng_) + up * u(rng_) * 0.6f) * len * amount);
            }
            next.push_back(points.back());
            points.swap(next);
            amount *= 0.62f;
        }
        (void)width;
    };
    arc.segments.clear();
    std::vector<glm::vec3> main, side;
    const float length = glm::distance(arc.from, arc.to);
    const int passes = std::clamp(static_cast<int>(std::log2(std::max(length, 0.1f) / 0.08f)), 2, 6);
    bolt(arc.from, arc.to, arc.width, arc.jag, passes, main);
    for (std::size_t k = 0; k + 1 < main.size(); ++k) {
        // Thinner toward the end of a strike that comes down from the top.
        const float t = static_cast<float>(k) / static_cast<float>(main.size());
        arc.segments.push_back({main[k], main[k + 1], arc.width * (1.0f - 0.35f * t)});
        if (k > 0 && p(rng_) < arc.branch * 0.35f) {
            const glm::vec3 dir = main[k + 1] - main[k];
            const float remain = length * (1.0f - t);
            glm::vec3 kick(u(rng_), u(rng_) * 0.5f, u(rng_));
            const glm::vec3 end = main[k] + glm::normalize(glm::normalize(dir) + kick) * remain * (0.25f + 0.3f * p(rng_));
            bolt(main[k], end, arc.width * 0.5f, arc.jag * 1.2f, std::max(2, passes - 2), side);
            for (std::size_t m = 0; m + 1 < side.size(); ++m)
                arc.segments.push_back({side[m], side[m + 1], arc.width * 0.45f});
        }
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
        using engine::AnomalyKind;
        // Lens bends (crusher, rift); heat haze comes from fillHeat.
        if (s.settings.kind != AnomalyKind::Crusher && s.settings.kind != AnomalyKind::Rift)
            continue;
        const float strength = s.activity * live_[i].visible;
        if (strength < 0.01f)
            continue;
        engine::ScreenDistortion d;
        d.position = s.center + glm::vec3(0.0f, std::min(1.0f, s.height * 0.5f), 0.0f);
        d.radius = s.radius;
        d.strength = strength;
        d.lens = true;
        view.distortions.push_back(d);
    }
    fillHeat(view, viewMatrix);
    fillGround(view);
    fillArcs(view);
    fillLights(view);

    // Grass leans around the nearest anomalies: blown away by a vent, pulled into a crusher.
    std::vector<std::pair<float, engine::GrassBender>> benders;
    for (std::size_t i = 0; i < states.size(); ++i) {
        const auto& s = states[i];
        const float dist = glm::distance(s.center, view.cameraPosition);
        const float bend = s.settings.get("bend");
        if (dist > 70.0f || bend <= 0.0f)
            continue;
        engine::GrassBender g;
        g.position = s.center;
        using engine::AnomalyKind;
        switch (s.settings.kind) {
        case AnomalyKind::Crusher:
            g.mode = engine::GrassBender::Pull;
            g.radius = s.radius * 1.3f;
            g.strength = bend * (0.5f + 0.8f * s.activity);
            break;
        case AnomalyKind::Vent:
            g.radius = s.radius * 2.2f;
            g.strength = bend * (0.15f + 1.3f * s.activity * s.activity + 0.4f * live_[i].flash);
            break;
        case AnomalyKind::Discharge:
            g.radius = s.radius * 1.1f;
            g.strength = bend * (0.1f + 1.2f * s.activity + 0.6f * live_[i].flash);
            break;
        case AnomalyKind::Rift:
            g.mode = engine::GrassBender::Swirl;
            g.radius = s.radius * 1.4f;
            g.strength = bend * (0.4f + 0.4f * s.activity);
            break;
        default:
            continue;
        }
        benders.push_back({dist, g});
    }
    std::sort(benders.begin(), benders.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (std::size_t k = 0; k < benders.size() && k < static_cast<std::size_t>(engine::kMaxGrassBenders); ++k)
        view.benders.push_back(benders[k].second);

    for (const Bolt& b : bolts_)
        view.worldBoxes.push_back({b.position, glm::vec3(0.035f, 0.035f, 0.035f), glm::vec3(0.42f, 0.4f, 0.38f), false});
}

void GameAnomalies::fillArcs(engine::RenderView& view) const
{
    if (arcs_.empty())
        return;
    // Two strips per piece: a hot white core (kept at least ~1.5 px wide so far arcs do
    // not break up) and a wide blue glow. Additive and HDR, so the bloom adds the halo.
    engine::SpriteBatch batch;
    batch.texture = arcTexture_;
    batch.blend = engine::SpriteBlend::Additive;
    batch.lit = false;
    float nearest = 1e9f;
    const float pixel = 2.0f * std::tan(glm::radians(35.0f)) / static_cast<float>(std::max(view.viewportWidth, 1));
    for (const Arc& a : arcs_) {
        const float fade = std::sqrt(std::max(0.0f, 1.0f - a.age / a.life)) * a.flicker * a.intensity;
        if (fade <= 0.01f)
            continue;
        for (const ArcSegment& seg : a.segments) {
            const glm::vec3 mid = (seg.a + seg.b) * 0.5f;
            const glm::vec3 along = seg.b - seg.a;
            const glm::vec3 toEye = view.cameraPosition - mid;
            const float dist = glm::length(toEye);
            nearest = std::min(nearest, dist);
            glm::vec3 across = glm::cross(along, toEye);
            const float len = glm::length(across);
            if (len < 1e-6f)
                continue;
            across /= len;
            const float core = std::max(seg.width, dist * pixel * 4.0f);
            engine::SpriteInstance q;
            q.position = mid;
            q.axisY = along * 0.5f * 1.12f;
            q.axisX = across * core;
            q.color = glm::vec4(glm::vec3(2.2f, 2.8f, 4.5f) * fade, 1.0f);
            batch.instances.push_back(q);
            q.axisX = across * core * 4.0f;
            q.color = glm::vec4(glm::vec3(0.18f, 0.32f, 1.1f) * fade * 0.6f, 1.0f);
            batch.instances.push_back(q);
        }
    }
    if (batch.instances.empty())
        return;
    batch.distance = nearest;
    view.sprites.push_back(std::move(batch));
}

void GameAnomalies::fillLights(engine::RenderView& view) const
{
    // Light from the anomalies themselves: the blue flicker of a discharge, the glow and
    // roar of a vent, the green shine of acid. Only the nearest few (no shadows).
    struct Candidate {
        float distance;
        engine::LocalLight light;
    };
    std::vector<Candidate> lights;
    const auto& states = field_.anomalies();
    using engine::AnomalyKind;
    for (std::size_t i = 0; i < states.size() && i < live_.size(); ++i) {
        const auto& s = states[i];
        const Live& l = live_[i];
        const float amount = s.settings.get("light");
        const float dist = glm::distance(s.center, view.cameraPosition);
        if (amount <= 0.0f || dist > 90.0f)
            continue;
        engine::LocalLight light;
        light.key = 0xA40A11ull * 1000 + i;
        const float wobble = 0.85f + 0.15f * std::sin(time_ * 13.0f + static_cast<float>(i)) * std::sin(time_ * 7.3f);
        float intensity = 0.0f;
        glm::vec3 color(1.0f);
        switch (s.settings.kind) {
        case AnomalyKind::Discharge:
            color = {0.45f, 0.62f, 1.0f};
            intensity = 0.4f + 70.0f * l.flash * l.flash + 8.0f * s.activity;
            light.position = s.center + glm::vec3(0.0f, 1.0f, 0.0f);
            light.range = s.radius * 2.0f + 4.0f + 8.0f * l.flash;
            break;
        case AnomalyKind::Vent: {
            color = {1.0f, 0.42f, 0.12f};
            const float column = s.settings.get("column");
            const bool burning = s.phase == engine::AnomalyField::State::Active;
            intensity = (burning ? 30.0f : 2.5f + 6.0f * s.activity) * wobble + 20.0f * l.flash;
            light.position = s.center + glm::vec3(0.0f, burning ? std::min(column * 0.3f, 2.5f) : 0.4f, 0.0f);
            light.range = burning ? 9.0f + column : s.radius * 2.0f + 3.0f;
            break;
        }
        case AnomalyKind::Acid:
            color = {0.35f, 0.95f, 0.15f};
            intensity = (1.6f + 0.5f * std::sin(time_ * 1.7f + static_cast<float>(i))) * (1.0f + 2.0f * l.flash);
            light.position = s.center + glm::vec3(0.0f, 0.5f, 0.0f);
            light.range = s.radius * 1.6f + 1.5f;
            break;
        case AnomalyKind::Rift:
            color = {0.65f, 0.4f, 1.0f};
            intensity = 1.2f * s.activity + 25.0f * l.flash;
            light.position = s.center + glm::vec3(0.0f, std::min(1.0f, s.height * 0.5f), 0.0f);
            light.range = s.radius * 2.0f + 2.0f;
            break;
        default:
            continue;
        }
        light.color = color * intensity * amount;
        lights.push_back({dist, light});
    }
    std::sort(lights.begin(), lights.end(), [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    for (std::size_t k = 0; k < lights.size() && k < 4; ++k)
        view.lights.push_back(lights[k].light);
}

void GameAnomalies::fillHeat(engine::RenderView& view, const glm::mat4& viewMatrix) const
{
    // Heat haze as a rising column of shimmering quads (depth tested, so things in
    // front stay sharp): faint over a resting vent, tall and strong while it burns;
    // a low shimmer of fumes over acid.
    const glm::vec3 right(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
    const glm::vec3 up(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
    engine::SpriteBatch heat;
    heat.blend = engine::SpriteBlend::Distort;
    heat.lit = false;
    const auto& states = field_.anomalies();
    using engine::AnomalyKind;
    for (std::size_t i = 0; i < states.size() && i < live_.size(); ++i) {
        const auto& s = states[i];
        if (glm::distance(s.center, view.cameraPosition) > 70.0f)
            continue;
        int count = 0;
        float height = 0.0f, size = 0.0f, strength = 0.0f, speed = 1.0f;
        if (s.settings.kind == AnomalyKind::Vent) {
            const bool burning = s.phase == engine::AnomalyField::State::Active;
            height = burning ? s.settings.get("column") * 1.2f : 1.6f + 1.5f * s.activity;
            size = std::max(0.6f, s.radius) * (burning ? 1.3f : 0.8f);
            strength = burning ? 1.0f : 0.25f + 0.5f * s.activity;
            count = burning ? 10 : 4;
            speed = burning ? 4.0f : 0.8f;
        } else if (s.settings.kind == AnomalyKind::Acid) {
            height = 0.9f;
            size = std::max(0.8f, s.radius * 0.45f);
            strength = 0.18f;
            count = 3;
            speed = 0.25f;
        } else {
            continue;
        }
        for (int k = 0; k < count; ++k) {
            // Each quad rises through the column and starts over (staggered).
            const float phase = std::fmod(time_ * speed / height + static_cast<float>(k) / static_cast<float>(count), 1.0f);
            const float swing = std::sin(time_ * 1.3f + static_cast<float>(k) * 2.1f);
            engine::SpriteInstance q;
            q.position = s.center + glm::vec3(swing * s.radius * 0.25f, 0.3f + phase * height, std::cos(time_ + k) * s.radius * 0.2f);
            const float r = size * (0.7f + 0.6f * phase);
            q.axisX = right * r;
            q.axisY = up * r * 1.3f;
            const float edge = std::sin(phase * 3.14159f); // fades in at the bottom, out at the top
            q.color = glm::vec4(0.0f, strength * edge, 0.0f, 1.0f);
            heat.instances.push_back(q);
        }
    }
    if (!heat.instances.empty())
        view.sprites.push_back(std::move(heat));
}

void GameAnomalies::fillGround(engine::RenderView& view) const
{
    // Marks on the ground under the fields (lying sprites from a 2x2 sheet): scorch under
    // discharges and vents, sludge of an acid pool, dead grass of a hotspot, pressed soil
    // under a crusher. Acid also gets moving glossy highlights; vents an ember glow.
    engine::SpriteBatch marks, glows, sheen;
    marks.texture = marksTexture_;
    marks.columns = marks.rows = 2;
    marks.distance = 1e6f; // under every other effect: drawn first
    glows.distance = 1e6f - 1.0f;
    sheen.distance = 1e6f - 2.0f;
    glows.blend = sheen.blend = engine::SpriteBlend::Additive;
    glows.lit = sheen.lit = false;
    sheen.texture = sheenTexture_;
    auto flat = [](glm::vec3 at, float radius, float angle, glm::vec4 color, float frame, float lift = 0.03f) {
        engine::SpriteInstance q;
        q.position = at + glm::vec3(0.0f, lift, 0.0f);
        const float c = std::cos(angle) * radius, s = std::sin(angle) * radius;
        q.axisX = glm::vec3(-s, 0.0f, c); // normal (axisX x axisY) points up: lit from above
        q.axisY = glm::vec3(c, 0.0f, s);
        q.color = color;
        q.frame = frame;
        return q;
    };
    const auto& states = field_.anomalies();
    using engine::AnomalyKind;
    for (std::size_t i = 0; i < states.size() && i < live_.size(); ++i) {
        const auto& s = states[i];
        const Live& l = live_[i];
        if (glm::distance(s.center, view.cameraPosition) > 120.0f)
            continue;
        switch (s.settings.kind) {
        case AnomalyKind::Crusher:
            marks.instances.push_back(flat(s.center, s.radius * 1.05f, l.angle + time_ * 0.05f, glm::vec4(1.0f, 1.0f, 1.0f, 0.85f), 3.0f));
            break;
        case AnomalyKind::Discharge:
            marks.instances.push_back(flat(s.center, s.radius * 0.85f, l.angle, glm::vec4(1.0f, 1.0f, 1.0f, 0.8f), 0.0f));
            glows.instances.push_back(flat(s.center, s.radius * 0.6f, 0.0f, glm::vec4(0.15f, 0.25f, 0.8f, 0.15f + 0.8f * l.flash), 0.0f, 0.05f));
            break;
        case AnomalyKind::Vent:
            marks.instances.push_back(flat(s.center, s.radius * 1.7f, l.angle, glm::vec4(1.0f), 0.0f));
            glows.instances.push_back(flat(s.center, s.radius * 0.55f, 0.0f,
                                           glm::vec4(1.4f, 0.4f, 0.08f, (0.45f + 0.5f * s.activity) * (0.85f + 0.15f * std::sin(time_ * 9.0f))), 0.0f,
                                           0.05f));
            break;
        case AnomalyKind::Acid: {
            marks.instances.push_back(flat(s.center, s.radius * 1.15f, l.angle, glm::vec4(1.0f), 1.0f));
            // Two layers of highlights turning slowly against each other read as a moving, oily surface.
            const float pulse = 0.75f + 0.25f * std::sin(time_ * 0.9f + static_cast<float>(i));
            const glm::vec4 tint(0.55f * pulse, 0.85f * pulse, 0.22f * pulse, 0.24f + 0.3f * l.flash);
            sheen.instances.push_back(flat(s.center, s.radius * 0.95f, l.angle + time_ * 0.07f, tint, 0.0f, 0.06f));
            sheen.instances.push_back(flat(s.center + glm::vec3(0.15f, 0.0f, -0.1f), s.radius * 0.8f, -l.angle - time_ * 0.11f, tint * 0.7f, 0.0f, 0.065f));
            glows.instances.push_back(flat(s.center, s.radius * 0.8f, 0.0f, {0.25f, 0.45f, 0.05f, 0.22f}, 0.0f, 0.05f));
            break;
        }
        case AnomalyKind::Radiation:
            if (s.settings.get("dry") >= 0.5f)
                marks.instances.push_back(flat(s.center, s.radius * 0.9f, l.angle, glm::vec4(1.0f, 1.0f, 1.0f, 0.9f), 2.0f));
            break;
        default:
            break;
        }
    }
    for (engine::SpriteBatch* b : {&marks, &glows, &sheen})
        if (!b->instances.empty())
            view.sprites.push_back(std::move(*b));
}

void GameAnomalies::drawHud(engine::Ui2D& ui, int width, int height) const
{
    (void)width;
    const float H = static_cast<float>(height);
    const float margin = std::round(H * 0.025f);
    const float text = uiSize(H, 0.022f, 12.0f, 26.0f);
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
