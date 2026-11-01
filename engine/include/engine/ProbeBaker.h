#pragma once
#include "engine/Probes.h"

#include <memory>

namespace engine {

class Renderer;
struct Scene;

// Bakes the level's reflection probes and irradiance volumes a few captures at a time
// (the editor runs step() from a timer so the window stays responsive; the GL context
// must be current). Each capture renders the scene 6 times around a point.
class ProbeBaker {
public:
    // bounces: 1 = light straight from the sun, sky and lamps; 2+ = captured again lit by the
    // previous bounce's probes (light bouncing around rooms).
    explicit ProbeBaker(const Scene& scene, int bounces = 2, int irradianceSize = 16);

    int total() const { return total_ * bounces_; }
    int done() const { return done_ + pass_ * total_; }
    bool finished() const { return pass_ >= bounces_; }
    // Captures until `budgetMs` has passed (at least one).
    void step(Renderer& renderer, const Scene& scene, double budgetMs, float time = 0.0f);
    void stop(Renderer& renderer); // after cancelling: the renderer goes back to the level's probes
    const ProbeSet& result() const { return set_; }

private:
    ProbeSet set_;
    std::shared_ptr<ProbeSet> previous_; // the last finished bounce
    int irradianceSize_, bounces_;
    int total_ = 0, done_ = 0, pass_ = 0;
    std::size_t reflection_ = 0, volume_ = 0;
    int point_ = 0;
};

} // namespace engine
