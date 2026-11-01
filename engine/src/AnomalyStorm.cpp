#include "engine/AnomalyStorm.h"

#include "engine/EntityIO.h"
#include "engine/Physics.h"

#include <algorithm>

namespace engine {

namespace {
constexpr float kFadeSeconds = 25.0f;
}

const char* stormPhaseName(AnomalyStorm::Phase p)
{
    switch (p) {
    case AnomalyStorm::Phase::Warning: return "warning";
    case AnomalyStorm::Phase::Storm: return "storm";
    case AnomalyStorm::Phase::Fading: return "fading";
    default: return "calm";
    }
}

void AnomalyStorm::schedule(const AnomalyStormSettings& s)
{
    const float lo = std::max(0.01f, std::min(s.minMinutes, s.maxMinutes)), hi = std::max(lo, std::max(s.minMinutes, s.maxMinutes));
    phase_ = Phase::Calm;
    timer_ = phaseLength_ = std::uniform_real_distribution<float>(lo, hi)(rng_) * 60.0f;
}

void AnomalyStorm::start(const AnomalyStormSettings& s)
{
    schedule(s);
    look_ = danger_ = 0.0f;
}

void AnomalyStorm::trigger(const AnomalyStormSettings& s, float warningSeconds)
{
    phase_ = Phase::Warning;
    timer_ = phaseLength_ = std::max(0.0f, warningSeconds >= 0.0f ? warningSeconds : s.warningSeconds);
}

void AnomalyStorm::restore(int phase, float secondsLeft)
{
    phase_ = static_cast<Phase>(std::clamp(phase, 0, 3));
    timer_ = std::max(0.0f, secondsLeft);
    phaseLength_ = std::max(timer_, 1.0f);
}

bool sheltered(const PhysicsWorld& physics, const std::vector<InteriorVolume>& interiors, glm::vec3 p)
{
    if (interiorWeight(interiors, p) > 0.5f)
        return true;
    return physics.raycast(p + glm::vec3(0.0f, 0.3f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 40.0f).has_value();
}

void AnomalyStorm::publish(AnomalyStormNow& now) const
{
    now.phase = stormPhaseName(phase_);
    now.secondsLeft = timer_;
    now.look = look_;
    now.danger = danger_;
}

bool AnomalyStorm::update(const AnomalyStormSettings& s, float dt)
{
    const Phase before = phase_;
    if (phase_ == Phase::Calm && !s.enabled) {
        look_ = danger_ = 0.0f;
        return false; // storms switched off (a forced one still runs its course)
    }
    timer_ -= dt;
    if (timer_ <= 0.0f) {
        switch (phase_) {
        case Phase::Calm: trigger(s); break;
        case Phase::Warning:
            phase_ = Phase::Storm;
            timer_ = phaseLength_ = std::max(1.0f, s.durationSeconds);
            break;
        case Phase::Storm:
            phase_ = Phase::Fading;
            timer_ = phaseLength_ = kFadeSeconds;
            break;
        case Phase::Fading: schedule(s); break;
        }
    }
    const float t = phaseLength_ > 0.0f ? std::clamp(1.0f - timer_ / phaseLength_, 0.0f, 1.0f) : 1.0f; // 0..1 through the phase
    switch (phase_) {
    case Phase::Calm: look_ = danger_ = 0.0f; break;
    case Phase::Warning: look_ = 0.15f + 0.6f * t; danger_ = 0.0f; break;
    case Phase::Storm: look_ = std::min(1.0f, 0.75f + t * 4.0f); danger_ = std::min(1.0f, t * 6.0f); break;
    case Phase::Fading: look_ = 1.0f - t; danger_ = 0.0f; break;
    }
    return phase_ != before;
}

} // namespace engine
