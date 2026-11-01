#pragma once
#include <glm/vec3.hpp>
#include <random>
#include <string>
#include <vector>

namespace engine {

class PhysicsWorld;
struct InteriorVolume;

// The Zone's own storm (level setting, editor: Environment > Anomaly storm): every so often
// the sky turns rust-red, a warning (siren, rumble, the ambience ducks) gives the player
// `warningSeconds` to get under a roof, then for `durationSeconds` everyone outdoors takes
// damage while lightning hits all around. NPCs run for cover when warned.
// Level line: anomaly_storm <on> <min> <max> <warning> <duration> <damage> <npc damage> <r g b>
struct AnomalyStormSettings {
    bool enabled = false;
    float minMinutes = 20.0f, maxMinutes = 40.0f; // real minutes between storms
    float warningSeconds = 60.0f;
    float durationSeconds = 90.0f;
    float damage = 6.0f;      // hp per second to the player outdoors at the peak
    float npcDamage = 10.0f;  // hp per second to NPCs outdoors
    glm::vec3 tint{1.0f, 0.55f, 0.42f}; // sky, fog and picture at the peak
    bool operator==(const AnomalyStormSettings&) const = default;
};

// What the storm is doing now, kept in Environment::anomalyStormNow: environmentAt() colours
// the sky by `look`; scripts read it with game.storm() and ask for one with game.start_storm().
struct AnomalyStormNow {
    std::string phase = "calm";
    float secondsLeft = 0.0f;
    float look = 0.0f, danger = 0.0f;
    float request = -1.0f; // >= 0: start a storm with this many seconds of warning (game.start_storm)
};

// Under a roof: inside an "interior" volume, or something solid within 40 m straight above `p`.
bool sheltered(const PhysicsWorld& physics, const std::vector<InteriorVolume>& interiors, glm::vec3 p);

// Runtime of one level: when the next storm comes and how far the current one is.
class AnomalyStorm {
public:
    enum class Phase { Calm, Warning, Storm, Fading };

    explicit AnomalyStorm(unsigned seed = std::random_device{}()) : rng_(seed) {}
    void start(const AnomalyStormSettings& s); // level start: schedules the first storm
    // Advances; returns true when the phase changed this call.
    bool update(const AnomalyStormSettings& s, float dt);
    void trigger(const AnomalyStormSettings& s, float warningSeconds = -1.0f); // now (console `storm`, game.storm())

    Phase phase() const { return phase_; }
    float secondsLeft() const { return timer_; } // in this phase (calm: until the warning)
    // 0..1 how strongly the sky and picture are tinted (rises through the warning, full in the storm).
    float look() const { return look_; }
    // 0..1 how much it hurts to be outdoors (only in the storm phase, with a ramp).
    float danger() const { return danger_; }

    // Saved games.
    int phaseIndex() const { return static_cast<int>(phase_); }
    void restore(int phase, float secondsLeft);
    // Copies the state into `now` (phase, seconds, look, danger) and clears a request it took.
    void publish(AnomalyStormNow& now) const;

private:
    void schedule(const AnomalyStormSettings& s);
    std::mt19937 rng_;
    Phase phase_ = Phase::Calm;
    float timer_ = 0.0f, phaseLength_ = 1.0f;
    float look_ = 0.0f, danger_ = 0.0f;
};

const char* stormPhaseName(AnomalyStorm::Phase p); // "calm", "warning", "storm", "fading"

} // namespace engine
