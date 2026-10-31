#include "engine/FixedStepClock.h"
#include <algorithm>

namespace engine {

FixedStepClock::FixedStepClock(double stepSeconds, int maxStepsPerFrame)
    : step_(stepSeconds), maxSteps_(maxStepsPerFrame) {}

int FixedStepClock::advance(double frameSeconds)
{
    // Clamp huge frames (debugger break, window drag) to avoid a spiral of death.
    accumulator_ += std::clamp(frameSeconds, 0.0, step_ * maxSteps_);
    int steps = 0;
    while (accumulator_ >= step_ && steps < maxSteps_) {
        accumulator_ -= step_;
        ++steps;
    }
    return steps;
}

} // namespace engine
