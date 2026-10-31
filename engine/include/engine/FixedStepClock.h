#pragma once

namespace engine {

// Fixed-timestep accumulator: simulation runs at a constant rate (default
// 60 Hz) independent of the frame rate; rendering interpolates with alpha().
class FixedStepClock {
public:
    explicit FixedStepClock(double stepSeconds = 1.0 / 60.0, int maxStepsPerFrame = 8);

    // Adds the real time of one frame; returns how many simulation steps to run.
    int advance(double frameSeconds);

    // Fraction (0..1) of a step left over, used to blend previous and current state.
    double alpha() const { return accumulator_ / step_; }
    double step() const { return step_; }

private:
    double step_;
    int maxSteps_;
    double accumulator_ = 0.0;
};

} // namespace engine
