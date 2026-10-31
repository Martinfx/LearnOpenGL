#pragma once
// Zone anomalies: dangerous places that pull, burn, shock, dissolve, throw or
// irradiate whatever comes close. They are level entities of type "anomaly";
// their kind and tuning live in the entity's properties (set in the editor's
// Anomaly section). The field is the entity box: radius = size.x / 2, height = size.y.
//
// The simulation here is pure (no audio, particles or rendering): the game feeds
// it the bodies near the anomalies every fixed step and applies the influences
// and events it returns. Tests drive it the same way.
#include "engine/Scene.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace engine {

enum class AnomalyKind : std::uint8_t {
    Crusher,    // gravity trap: pulls toward its core, then crushes and flings
    Discharge,  // electric field: anything entering sets off a lightning strike
    Vent,       // heat vent: erupts in a column of fire on a cycle or when stepped on
    Acid,       // chemical pool: burns and slows while you wade in it
    Rift,       // spatial fold: throws you somewhere else nearby
    Radiation,  // radioactive spot or area: dose ticks up while inside (Geiger counter)
    Count
};

struct AnomalyKindInfo {
    AnomalyKind kind;
    const char* id;          // stored in the level ("crusher")
    const char* name;        // shown in the editor
    const char* description;
    glm::vec3 color;         // editor marker (sRGB)
    glm::vec3 size;          // default field size (m)
    const char* idleEffect;  // .particle relative to assets/, empty = none
    const char* burstEffect; // played when it fires
    const char* loopSound;   // .sound relative to assets/, empty = none
    const char* burstSound;
};

const AnomalyKindInfo& anomalyKindInfo(AnomalyKind kind);
AnomalyKind anomalyKindFromId(const std::string& id); // unknown = Crusher

// One tunable of a kind, edited in the editor and stored as an entity property.
struct AnomalyParam {
    const char* key;
    float defaultValue;
    float min, max, step;
    const char* unit;        // suffix in the editor (" m", " s")
    const char* label;
    const char* tooltip;
    bool toggle = false;     // 0/1 shown as a check box
};
const std::vector<AnomalyParam>& anomalyParams(AnomalyKind kind);

// Kind and parameter values of an anomaly entity (defaults for missing keys).
struct AnomalySettings {
    AnomalyKind kind = AnomalyKind::Crusher;
    std::vector<float> values; // in anomalyParams(kind) order
    std::string link;          // rift: name of the entity it throws you to (empty = random)
    std::string idleEffect, burstEffect, loopSound, burstSound; // relative to assets/

    float get(const char* key) const;
};
AnomalySettings anomalySettings(const Entity& entity);
// Writes the kind and its defaults into a fresh entity (size, color, properties).
void initAnomalyEntity(Entity& entity, AnomalyKind kind);
// Changes the kind, keeping the entity's position; parameters go back to the new kind's defaults.
void setAnomalyKind(Entity& entity, AnomalyKind kind);

// Something an anomaly acts on: the player, a character, a thrown bolt.
struct AnomalyBody {
    glm::vec3 feet{0.0f};
    float height = 1.8f;
    glm::vec3 velocity{0.0f};
    bool small = false;        // bolts and debris: set anomalies off, take no damage
};

// What the anomalies do to one body during a step.
struct AnomalyInfluence {
    glm::vec3 drift{0.0f};     // m/s added to its own motion this step (pull)
    glm::vec3 impulse{0.0f};   // velocity change (a fling), one-off
    float damage = 0.0f;       // hit points this step
    float slow = 0.0f;         // 0..1 of its speed taken away
    float radiation = 0.0f;    // dose rate here (rad/s)
    bool teleport = false;
    glm::vec3 teleportTo{0.0f}; // feet; the game puts it on the ground
};

struct AnomalyEvent {
    enum Type : std::uint8_t {
        Charge,   // started to build up (crusher squeezes, discharge hums)
        Burst,    // fires: crush, lightning strike, fire column, rift jump, acid hiss
        End,      // the burst is over (vent column stops)
    };
    Type type = Burst;
    int anomaly = 0;            // index into AnomalyField::anomalies()
    glm::vec3 position{0.0f};   // where to play the effect
};

class AnomalyField {
public:
    struct State {
        int entity = -1;        // index into scene.entities
        AnomalySettings settings;
        glm::vec3 center{0.0f}; // bottom center of the field
        float radius = 1.0f;
        float height = 2.0f;
        enum Phase : std::uint8_t { Idle, Charging, Active, Cooldown } phase = Idle;
        float timer = 0.0f;     // seconds in this phase
        float cycle = 0.0f;     // vent: seconds since the last eruption
        float idleTimer = 0.0f; // discharge: next idle crackle
        float activity = 0.0f;  // 0..1 smoothed, drives the look (distortion, glow)
    };

    // Collects the scene's anomaly entities (call after loading the level).
    void build(const Scene& scene, std::uint32_t seed = 1234);

    // One fixed step. `bodies` in, one influence per body out; events are appended.
    void step(float dt, const std::vector<AnomalyBody>& bodies, std::vector<AnomalyInfluence>& influences,
              std::vector<AnomalyEvent>& events);

    // Detector: strongest signal 0..1 of an anomaly near `point` (radiation is not
    // detected, the Geiger counter does that) and the distance to its field edge.
    float detectorSignal(glm::vec3 point, float* edgeDistance = nullptr, int* anomaly = nullptr) const;
    // Dose rate (rad/s) at a point.
    float radiationAt(glm::vec3 point) const;

    const std::vector<State>& anomalies() const { return states_; }
    std::vector<State>& anomalies() { return states_; }

    // Field shape helpers (also used by the editor and tests).
    static bool inside(const State& s, glm::vec3 point, float margin = 0.0f);
    static float horizontalDistance(const State& s, glm::vec3 point);

private:
    void stepOne(State& s, int index, float dt, const std::vector<AnomalyBody>& bodies,
                 std::vector<AnomalyInfluence>& influences, std::vector<AnomalyEvent>& events);
    glm::vec3 riftTarget(const State& s, glm::vec3 from);

    const Scene* scene_ = nullptr;
    std::vector<State> states_;
    std::mt19937 rng_;
};

} // namespace engine
