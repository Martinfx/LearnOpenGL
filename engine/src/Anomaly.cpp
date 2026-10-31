#include "engine/Anomaly.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace engine {

namespace {

const std::array<AnomalyKindInfo, static_cast<std::size_t>(AnomalyKind::Count)> kKinds = {{
    {AnomalyKind::Crusher, "crusher", "Crusher (gravity trap)",
     "Pulls everything toward its core. When something reaches the core it squeezes for a moment, then crushes and flings it away.",
     {0.55f, 0.5f, 0.75f}, {5.0f, 3.0f, 5.0f}, "particles/anomaly_crusher_idle.particle", "particles/anomaly_crusher_burst.particle",
     "sounds/anomalies/crusher_loop.sound", "sounds/anomalies/crusher_burst.sound"},
    {AnomalyKind::Discharge, "discharge", "Discharge (electric field)",
     "A charged patch of air. Anything that enters sets off a lightning strike after a short delay; then it recharges.",
     {0.35f, 0.6f, 1.0f}, {3.5f, 3.0f, 3.5f}, "particles/anomaly_discharge_idle.particle", "particles/anomaly_discharge_burst.particle",
     "sounds/anomalies/discharge_loop.sound", "sounds/anomalies/discharge_burst.sound"},
    {AnomalyKind::Vent, "vent", "Vent (heat and fire)",
     "A crack in the ground that shimmers with heat and erupts in a column of fire, on its own cycle and when stepped on.",
     {1.0f, 0.45f, 0.15f}, {2.0f, 5.0f, 2.0f}, "particles/anomaly_vent_idle.particle", "particles/anomaly_vent_burst.particle",
     "sounds/anomalies/vent_loop.sound", "sounds/anomalies/vent_burst.sound"},
    {AnomalyKind::Acid, "acid", "Acid pool (chemical)",
     "A shallow pool of corrosive sludge under a green haze. Burns and slows while you wade in it; dissolves bolts.",
     {0.45f, 0.85f, 0.2f}, {6.0f, 1.0f, 6.0f}, "particles/anomaly_acid_idle.particle", "particles/anomaly_acid_burst.particle",
     "sounds/anomalies/acid_loop.sound", "sounds/anomalies/acid_burst.sound"},
    {AnomalyKind::Rift, "rift", "Rift (spatial fold)",
     "A barely visible fold in space. Whatever steps in is thrown out somewhere else: to the linked entity or a random spot nearby.",
     {0.75f, 0.4f, 0.95f}, {2.5f, 2.5f, 2.5f}, "particles/anomaly_rift_idle.particle", "particles/anomaly_rift_burst.particle",
     "sounds/anomalies/rift_loop.sound", "sounds/anomalies/rift_burst.sound"},
    {AnomalyKind::Radiation, "radiation", "Radiation",
     "Invisible contamination. A hotspot is strongest in its middle; an area is even inside its box. Heard on the Geiger counter.",
     {0.9f, 0.85f, 0.2f}, {8.0f, 4.0f, 8.0f}, "", "", "", ""},
}};

using P = AnomalyParam;

const std::vector<AnomalyParam>& paramsFor(AnomalyKind kind)
{
    static const std::array<std::vector<AnomalyParam>, static_cast<std::size_t>(AnomalyKind::Count)> table = {{
        {   // Crusher
            P{"pull", 5.0f, 0.0f, 30.0f, 0.5f, " m/s", "Pull", "Speed it drags things toward the core (strongest near the core)"},
            P{"core", 0.7f, 0.1f, 5.0f, 0.1f, " m", "Core radius", "Reaching this close to the middle sets it off"},
            P{"charge", 0.8f, 0.0f, 5.0f, 0.1f, " s", "Squeeze time", "Seconds between reaching the core and the crush (time to escape)"},
            P{"damage", 70.0f, 0.0f, 1000.0f, 5.0f, "", "Crush damage", "Hit points in the core; the rest of the field takes a third"},
            P{"throw", 10.0f, 0.0f, 40.0f, 0.5f, " m/s", "Fling speed", "How hard the crush throws things out"},
            P{"cooldown", 3.0f, 0.0f, 60.0f, 0.5f, " s", "Cooldown", "Seconds before it can crush again"},
            P{"detect", 6.0f, 0.0f, 50.0f, 0.5f, " m", "Detector range", "Distance from the field edge where the detector starts beeping"},
        },
        {   // Discharge
            P{"damage", 35.0f, 0.0f, 1000.0f, 5.0f, "", "Strike damage", "Hit points per lightning strike"},
            P{"delay", 0.15f, 0.0f, 3.0f, 0.05f, " s", "Delay", "Seconds from entering to the strike"},
            P{"cooldown", 2.5f, 0.0f, 60.0f, 0.5f, " s", "Recharge", "Seconds before the next strike"},
            P{"idle", 4.0f, 0.0f, 60.0f, 0.5f, " s", "Idle crackle", "Average seconds between small idle sparks (0 = none)"},
            P{"detect", 5.0f, 0.0f, 50.0f, 0.5f, " m", "Detector range", "Distance from the field edge where the detector starts beeping"},
        },
        {   // Vent
            P{"damage", 40.0f, 0.0f, 1000.0f, 5.0f, " /s", "Fire damage", "Hit points per second inside the column"},
            P{"period", 8.0f, 0.0f, 120.0f, 0.5f, " s", "Cycle", "Seconds between eruptions on its own (0 = only when stepped on)"},
            P{"burst", 1.8f, 0.1f, 20.0f, 0.1f, " s", "Eruption", "How long the column burns"},
            P{"column", 5.0f, 0.5f, 30.0f, 0.5f, " m", "Column height", "Fire reaches this high"},
            P{"lift", 3.0f, 0.0f, 20.0f, 0.5f, " m/s", "Updraft", "Pushes things up while it burns"},
            P{"on_step", 1.0f, 0.0f, 1.0f, 1.0f, "", "Erupts when entered", "Something in the field sets it off", true},
            P{"detect", 5.0f, 0.0f, 50.0f, 0.5f, " m", "Detector range", "Distance from the field edge where the detector starts beeping"},
        },
        {   // Acid
            P{"damage", 8.0f, 0.0f, 1000.0f, 1.0f, " /s", "Burn damage", "Hit points per second while in the pool"},
            P{"slow", 0.45f, 0.0f, 0.95f, 0.05f, "", "Slow down", "Part of the walking speed the sludge takes away"},
            P{"depth", 0.6f, 0.05f, 5.0f, 0.05f, " m", "Depth", "Feet below this height above the entity are in the pool"},
            P{"detect", 4.0f, 0.0f, 50.0f, 0.5f, " m", "Detector range", "Distance from the field edge where the detector starts beeping"},
        },
        {   // Rift
            P{"jump", 10.0f, 1.0f, 200.0f, 1.0f, " m", "Throw distance", "How far it throws without a link"},
            P{"damage", 5.0f, 0.0f, 1000.0f, 1.0f, "", "Damage", "Hit points per throw"},
            P{"cooldown", 4.0f, 0.0f, 60.0f, 0.5f, " s", "Cooldown", "Seconds before it throws again"},
            P{"detect", 4.0f, 0.0f, 50.0f, 0.5f, " m", "Detector range", "Distance from the field edge where the detector starts beeping"},
        },
        {   // Radiation
            P{"intensity", 25.0f, 0.0f, 1000.0f, 1.0f, " rad/s", "Intensity", "Dose rate in the middle of a hotspot, or anywhere in an area"},
            P{"area", 0.0f, 0.0f, 1.0f, 1.0f, "", "Even area (box)", "Off: hotspot fading out to its radius. On: the whole box, soft edges", true},
            P{"fade", 2.0f, 0.0f, 50.0f, 0.5f, " m", "Edge fade", "Areas: metres over which the dose fades out around the box"},
        },
    }};
    return table[static_cast<std::size_t>(kind)];
}

float propFloat(const Entity& e, const char* key, float fallback)
{
    const auto it = e.properties.find(key);
    if (it == e.properties.end() || it->second.empty())
        return fallback;
    char* end = nullptr;
    const float v = std::strtof(it->second.c_str(), &end);
    return end == it->second.c_str() ? fallback : v;
}

std::string propString(const Entity& e, const char* key, const char* fallback)
{
    const auto it = e.properties.find(key);
    return it == e.properties.end() ? std::string(fallback) : it->second;
}

float approach(float value, float target, float rate, float dt)
{
    return value + (target - value) * std::min(1.0f, rate * dt);
}

} // namespace

const AnomalyKindInfo& anomalyKindInfo(AnomalyKind kind)
{
    return kKinds[std::min<std::size_t>(static_cast<std::size_t>(kind), kKinds.size() - 1)];
}

AnomalyKind anomalyKindFromId(const std::string& id)
{
    for (const AnomalyKindInfo& k : kKinds)
        if (id == k.id)
            return k.kind;
    return AnomalyKind::Crusher;
}

const std::vector<AnomalyParam>& anomalyParams(AnomalyKind kind) { return paramsFor(kind); }

float AnomalySettings::get(const char* key) const
{
    const auto& params = paramsFor(kind);
    for (std::size_t i = 0; i < params.size(); ++i)
        if (std::string_view(params[i].key) == key)
            return i < values.size() ? values[i] : params[i].defaultValue;
    return 0.0f;
}

AnomalySettings anomalySettings(const Entity& entity)
{
    AnomalySettings s;
    s.kind = anomalyKindFromId(propString(entity, "kind", "crusher"));
    for (const AnomalyParam& p : paramsFor(s.kind))
        s.values.push_back(std::clamp(propFloat(entity, p.key, p.defaultValue), p.min, p.max));
    const AnomalyKindInfo& info = anomalyKindInfo(s.kind);
    s.link = propString(entity, "link", "");
    s.idleEffect = propString(entity, "effect", info.idleEffect);
    s.burstEffect = propString(entity, "burst_effect", info.burstEffect);
    s.loopSound = propString(entity, "sound", info.loopSound);
    s.burstSound = propString(entity, "burst_sound", info.burstSound);
    return s;
}

void setAnomalyKind(Entity& entity, AnomalyKind kind)
{
    const AnomalyKindInfo& info = anomalyKindInfo(kind);
    entity.properties.clear();
    entity.properties["kind"] = info.id;
    entity.size = info.size;
    entity.color = info.color;
}

void initAnomalyEntity(Entity& entity, AnomalyKind kind)
{
    entity.type = "anomaly";
    entity.scriptPath.clear();
    entity.modelPath.clear();
    setAnomalyKind(entity, kind);
}

// ------------------------------------------------------------------ field

float AnomalyField::horizontalDistance(const State& s, glm::vec3 p)
{
    return glm::length(glm::vec2(p.x - s.center.x, p.z - s.center.z));
}

bool AnomalyField::inside(const State& s, glm::vec3 p, float margin)
{
    return horizontalDistance(s, p) < s.radius + margin && p.y > s.center.y - 0.5f - margin &&
           p.y < s.center.y + s.height + margin;
}

void AnomalyField::build(const Scene& scene, std::uint32_t seed)
{
    scene_ = &scene;
    states_.clear();
    rng_.seed(seed);
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const Entity& e = scene.entities[i];
        if (e.type != "anomaly")
            continue;
        State s;
        s.entity = static_cast<int>(i);
        s.settings = anomalySettings(e);
        s.center = e.position;
        s.radius = std::max(0.1f, std::max(e.size.x, e.size.z) * 0.5f);
        s.height = std::max(0.1f, e.size.y);
        // Vents start at different points of their cycle so a row of them does not fire together.
        s.cycle = std::uniform_real_distribution<float>(0.0f, std::max(0.0f, s.settings.get("period")))(rng_);
        s.idleTimer = std::uniform_real_distribution<float>(0.5f, 4.0f)(rng_);
        states_.push_back(std::move(s));
    }
}

float AnomalyField::radiationAt(glm::vec3 p) const
{
    float total = 0.0f;
    for (const State& s : states_) {
        if (s.settings.kind != AnomalyKind::Radiation || !scene_ || s.entity >= static_cast<int>(scene_->entities.size()))
            continue;
        const float intensity = s.settings.get("intensity");
        if (s.settings.get("area") >= 0.5f) {
            // Even inside the box, fading out over `fade` metres around it.
            const Entity& e = scene_->entities[static_cast<std::size_t>(s.entity)];
            const Aabb box = e.bounds();
            const glm::vec3 outside = glm::max(glm::max(box.min - p, p - box.max), glm::vec3(0.0f));
            const float d = glm::length(outside);
            const float fade = s.settings.get("fade");
            const float f = fade > 0.0f ? 1.0f - std::clamp(d / fade, 0.0f, 1.0f) : (d <= 0.0f ? 1.0f : 0.0f);
            total += intensity * f * f;
        } else {
            // Hotspot: strongest in the middle of the field, gone at its radius.
            const glm::vec3 mid = s.center + glm::vec3(0.0f, std::min(s.height * 0.5f, 1.0f), 0.0f);
            const float d = glm::length(p - mid);
            const float f = 1.0f - std::clamp(d / s.radius, 0.0f, 1.0f);
            total += intensity * f * f;
        }
    }
    return total;
}

float AnomalyField::detectorSignal(glm::vec3 point, float* edgeDistance, int* anomaly) const
{
    float best = 0.0f, bestEdge = 1e9f;
    int bestIndex = -1;
    for (std::size_t i = 0; i < states_.size(); ++i) {
        const State& s = states_[i];
        if (s.settings.kind == AnomalyKind::Radiation)
            continue;
        const float range = s.settings.get("detect");
        if (range <= 0.0f)
            continue;
        const float vertical = point.y < s.center.y - 2.0f ? s.center.y - 2.0f - point.y
                             : point.y > s.center.y + s.height + 2.0f ? point.y - s.center.y - s.height - 2.0f : 0.0f;
        const float edge = std::max(0.0f, horizontalDistance(s, point) - s.radius) + vertical;
        if (edge >= range)
            continue;
        const float signal = 1.0f - edge / range;
        if (signal > best) {
            best = signal;
            bestEdge = edge;
            bestIndex = static_cast<int>(i);
        }
    }
    if (edgeDistance)
        *edgeDistance = bestEdge;
    if (anomaly)
        *anomaly = bestIndex;
    return best;
}

glm::vec3 AnomalyField::riftTarget(const State& s, glm::vec3 from)
{
    const float angle = std::uniform_real_distribution<float>(0.0f, 6.2831853f)(rng_);
    const glm::vec3 dir(std::cos(angle), 0.0f, std::sin(angle));
    if (!s.settings.link.empty() && scene_) {
        for (std::size_t i = 0; i < scene_->entities.size(); ++i) {
            const Entity& e = scene_->entities[i];
            if (e.name != s.settings.link)
                continue;
            float clear = 0.5f;
            for (State& other : states_)
                if (other.entity == static_cast<int>(i)) {
                    clear = other.radius + 1.0f;
                    // The exit does not throw you straight back.
                    other.phase = State::Cooldown;
                    other.timer = 0.0f;
                }
            return e.position + dir * clear + glm::vec3(0.0f, 0.3f, 0.0f);
        }
    }
    const float jump = std::max(s.settings.get("jump"), s.radius + 1.5f);
    glm::vec3 target = from + dir * jump;
    if (horizontalDistance(s, target) < s.radius + 1.0f) // never back into the fold
        target = s.center + dir * (s.radius + 1.5f);
    target.y = from.y + 0.3f;
    return target;
}

void AnomalyField::step(float dt, const std::vector<AnomalyBody>& bodies, std::vector<AnomalyInfluence>& influences,
                        std::vector<AnomalyEvent>& events)
{
    influences.assign(bodies.size(), AnomalyInfluence{});
    for (std::size_t i = 0; i < states_.size(); ++i)
        stepOne(states_[i], static_cast<int>(i), dt, bodies, influences, events);
    for (std::size_t b = 0; b < bodies.size(); ++b)
        influences[b].radiation = radiationAt(bodies[b].feet + glm::vec3(0.0f, std::min(1.0f, bodies[b].height * 0.5f), 0.0f));
}

void AnomalyField::stepOne(State& s, int index, float dt, const std::vector<AnomalyBody>& bodies,
                           std::vector<AnomalyInfluence>& out, std::vector<AnomalyEvent>& events)
{
    const AnomalySettings& cfg = s.settings;
    s.timer += dt;
    auto enter = [&](State::Phase phase) {
        s.phase = phase;
        s.timer = 0.0f;
    };
    auto emit = [&](AnomalyEvent::Type type, glm::vec3 where) { events.push_back({type, index, where}); };
    const glm::vec3 middle = s.center + glm::vec3(0.0f, std::min(1.0f, s.height * 0.5f), 0.0f);
    // A body is in the field when its middle is (small things: their position).
    auto bodyPoint = [](const AnomalyBody& b) { return b.feet + glm::vec3(0.0f, b.small ? 0.0f : std::min(0.9f, b.height * 0.5f), 0.0f); };
    auto anyInside = [&](float radius) {
        for (const AnomalyBody& b : bodies) {
            const glm::vec3 p = bodyPoint(b);
            if (horizontalDistance(s, p) < radius && inside(s, p))
                return true;
        }
        return false;
    };
    float targetActivity = 0.0f;

    switch (cfg.kind) {
    case AnomalyKind::Crusher: {
        const float pull = cfg.get("pull"), core = cfg.get("core");
        const float strength = s.phase == State::Cooldown ? 0.3f : 1.0f;
        for (std::size_t b = 0; b < bodies.size(); ++b) {
            const glm::vec3 p = bodyPoint(bodies[b]);
            if (!inside(s, p))
                continue;
            const float d = horizontalDistance(s, p);
            glm::vec3 to(s.center.x - p.x, 0.0f, s.center.z - p.z);
            if (d > 0.05f) {
                const float f = 1.0f - d / s.radius;
                // Fades out right at the middle so things settle instead of jittering across it.
                out[b].drift += to / d * pull * strength * (0.35f + 0.65f * f) * std::min(1.0f, d / 0.4f);
            }
            if (s.phase == State::Charging)
                out[b].drift.y += 1.2f; // lifted off the ground while it squeezes
        }
        if (s.phase == State::Idle && anyInside(core)) {
            enter(State::Charging);
            emit(AnomalyEvent::Charge, middle);
        } else if (s.phase == State::Charging && s.timer >= cfg.get("charge")) {
            const float damage = cfg.get("damage"), fling = cfg.get("throw");
            for (std::size_t b = 0; b < bodies.size(); ++b) {
                const glm::vec3 p = bodyPoint(bodies[b]);
                if (!inside(s, p))
                    continue;
                const float d = horizontalDistance(s, p);
                const float f = 1.0f - d / s.radius;
                out[b].damage += bodies[b].small ? 0.0f : damage * (d < core * 1.6f ? 1.0f : 0.33f);
                glm::vec3 away(p.x - s.center.x, 0.0f, p.z - s.center.z);
                const float len = glm::length(away);
                if (len < 0.05f) { // dead center: a random direction
                    const float a = std::uniform_real_distribution<float>(0.0f, 6.2831853f)(rng_);
                    away = glm::vec3(std::cos(a), 0.0f, std::sin(a));
                } else {
                    away /= len;
                }
                out[b].impulse += away * fling * (0.5f + 0.5f * f) + glm::vec3(0.0f, fling * 0.6f, 0.0f);
            }
            emit(AnomalyEvent::Burst, middle);
            enter(State::Active);
        } else if (s.phase == State::Active && s.timer >= 0.3f) {
            enter(State::Cooldown);
        } else if (s.phase == State::Cooldown && s.timer >= cfg.get("cooldown")) {
            enter(State::Idle);
        }
        targetActivity = s.phase == State::Idle ? 0.45f : s.phase == State::Cooldown ? 0.2f : 1.0f;
        break;
    }
    case AnomalyKind::Discharge: {
        if (s.phase == State::Idle) {
            if (anyInside(s.radius)) {
                enter(State::Charging);
                emit(AnomalyEvent::Charge, middle);
            } else if (const float idle = cfg.get("idle"); idle > 0.0f) {
                s.idleTimer -= dt;
                if (s.idleTimer <= 0.0f) {
                    s.idleTimer = std::uniform_real_distribution<float>(0.4f, 1.6f)(rng_) * idle;
                    emit(AnomalyEvent::Charge, middle + glm::vec3(std::uniform_real_distribution<float>(-0.5f, 0.5f)(rng_) * s.radius,
                                                                  0.0f,
                                                                  std::uniform_real_distribution<float>(-0.5f, 0.5f)(rng_) * s.radius));
                }
            }
        } else if (s.phase == State::Charging && s.timer >= cfg.get("delay")) {
            const float damage = cfg.get("damage");
            glm::vec3 strike = middle;
            for (std::size_t b = 0; b < bodies.size(); ++b) {
                const glm::vec3 p = bodyPoint(bodies[b]);
                if (!inside(s, p))
                    continue;
                strike = p;
                out[b].damage += bodies[b].small ? 0.0f : damage;
                glm::vec3 away(p.x - s.center.x, 0.0f, p.z - s.center.z);
                if (glm::dot(away, away) > 1e-4f)
                    out[b].impulse += glm::normalize(away) * 3.0f + glm::vec3(0.0f, 1.5f, 0.0f);
            }
            emit(AnomalyEvent::Burst, strike);
            enter(State::Active);
        } else if (s.phase == State::Active && s.timer >= 0.25f) {
            enter(State::Cooldown);
        } else if (s.phase == State::Cooldown && s.timer >= cfg.get("cooldown")) {
            enter(State::Idle);
        }
        targetActivity = s.phase == State::Active ? 1.0f : s.phase == State::Charging ? 0.6f : 0.06f;
        break;
    }
    case AnomalyKind::Vent: {
        s.cycle += dt;
        const float period = cfg.get("period"), column = cfg.get("column");
        if (s.phase == State::Idle) {
            if ((period > 0.0f && s.cycle >= period) || (cfg.get("on_step") >= 0.5f && anyInside(s.radius))) {
                enter(State::Charging);
                emit(AnomalyEvent::Charge, s.center);
            }
        } else if (s.phase == State::Charging && s.timer >= 0.5f) {
            enter(State::Active);
            s.cycle = 0.0f;
            emit(AnomalyEvent::Burst, s.center);
        } else if (s.phase == State::Active) {
            const float damage = cfg.get("damage"), lift = cfg.get("lift");
            for (std::size_t b = 0; b < bodies.size(); ++b) {
                const glm::vec3 p = bodyPoint(bodies[b]);
                if (horizontalDistance(s, p) >= s.radius || p.y < s.center.y - 0.5f || bodies[b].feet.y > s.center.y + column)
                    continue;
                out[b].damage += bodies[b].small ? 0.0f : damage * dt;
                out[b].drift.y += lift;
            }
            if (s.timer >= cfg.get("burst")) {
                enter(State::Cooldown);
                emit(AnomalyEvent::End, s.center);
            }
        } else if (s.phase == State::Cooldown && s.timer >= 1.5f) {
            enter(State::Idle);
        }
        targetActivity = s.phase == State::Active ? 1.0f : s.phase == State::Charging ? 0.6f : 0.3f;
        break;
    }
    case AnomalyKind::Acid: {
        const float damage = cfg.get("damage"), slow = cfg.get("slow"), depth = cfg.get("depth");
        bool hiss = false;
        for (std::size_t b = 0; b < bodies.size(); ++b) {
            const AnomalyBody& body = bodies[b];
            if (horizontalDistance(s, body.feet) >= s.radius || body.feet.y > s.center.y + depth || body.feet.y < s.center.y - 1.0f)
                continue;
            out[b].damage += damage * dt; // bolts "take" it too: the game dissolves them
            out[b].slow = std::max(out[b].slow, slow);
            hiss = hiss || body.small || glm::dot(body.velocity, body.velocity) > 1.0f;
        }
        if (s.phase == State::Cooldown && s.timer >= 0.5f)
            enter(State::Idle);
        if (hiss && s.phase == State::Idle) { // stirred up: hisses and splashes, at most twice a second
            emit(AnomalyEvent::Burst, s.center);
            enter(State::Cooldown);
        }
        targetActivity = 0.12f;
        break;
    }
    case AnomalyKind::Rift: {
        if (s.phase == State::Idle) {
            bool thrown = false;
            for (std::size_t b = 0; b < bodies.size(); ++b) {
                const glm::vec3 p = bodyPoint(bodies[b]);
                if (!inside(s, p))
                    continue;
                out[b].teleport = true;
                out[b].teleportTo = riftTarget(s, bodies[b].feet);
                out[b].damage += bodies[b].small ? 0.0f : cfg.get("damage");
                emit(AnomalyEvent::Burst, out[b].teleportTo + glm::vec3(0.0f, 1.0f, 0.0f));
                thrown = true;
            }
            if (thrown) {
                emit(AnomalyEvent::Burst, middle);
                enter(State::Cooldown);
            }
        } else if (s.phase == State::Cooldown && s.timer >= cfg.get("cooldown")) {
            enter(State::Idle);
        }
        targetActivity = s.phase == State::Cooldown ? 0.25f + 0.75f * std::max(0.0f, 1.0f - s.timer * 2.0f) : 0.55f;
        break;
    }
    case AnomalyKind::Radiation:
    case AnomalyKind::Count:
        break;
    }
    s.activity = approach(s.activity, targetActivity, 6.0f, dt);
}

} // namespace engine
