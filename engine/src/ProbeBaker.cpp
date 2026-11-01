#include "engine/ProbeBaker.h"

#include "engine/Renderer.h"
#include "engine/Scene.h"

#include <glm/gtc/packing.hpp>

#include <chrono>

namespace engine {

ProbeBaker::ProbeBaker(const Scene& scene, int bounces, int irradianceSize)
    : irradianceSize_(std::max(4, irradianceSize)), bounces_(std::max(1, bounces))
{
    set_.reflections = reflectionProbesToBake(scene);
    set_.volumes = irradianceVolumesToBake(scene);
    total_ = static_cast<int>(set_.reflections.size());
    for (IrradianceVolume& v : set_.volumes) {
        const int points = v.dims.x * v.dims.y * v.dims.z;
        v.sh.assign(3u * static_cast<std::size_t>(points), glm::vec4(0.0f));
        total_ += points;
    }
    if (total_ == 0)
        pass_ = bounces_; // nothing to bake
}

void ProbeBaker::step(Renderer& renderer, const Scene& scene, double budgetMs, float time)
{
    const auto start = std::chrono::steady_clock::now();
    std::vector<float> faces;
    renderer.setCaptureProbes(previous_);
    do {
        if (finished())
            break;
        faces.clear();
        if (reflection_ < set_.reflections.size()) {
            ReflectionProbe& p = set_.reflections[reflection_++];
            renderer.captureCube(scene, p.shape.center, p.size, faces, time);
            p.faces.resize(faces.size() / 3);
            for (std::size_t i = 0; i < p.faces.size(); ++i)
                p.faces[i] = glm::packF3x9_E1x5(glm::vec3(faces[i * 3], faces[i * 3 + 1], faces[i * 3 + 2]));
        } else if (volume_ < set_.volumes.size()) {
            IrradianceVolume& v = set_.volumes[volume_];
            const glm::ivec3 cell(point_ % v.dims.x, (point_ / v.dims.x) % v.dims.y, point_ / (v.dims.x * v.dims.y));
            renderer.captureCube(scene, irradiancePoint(v, cell), irradianceSize_, faces, time);
            projectIrradiance(faces, irradianceSize_, &v.sh[3u * static_cast<std::size_t>(point_)]);
            if (++point_ >= v.dims.x * v.dims.y * v.dims.z) {
                point_ = 0;
                ++volume_;
            }
        }
        if (++done_ >= total_) { // a bounce finished: the next one is lit by it
            previous_ = std::make_shared<ProbeSet>(set_);
            renderer.setCaptureProbes(previous_);
            done_ = 0;
            reflection_ = volume_ = 0;
            point_ = 0;
            ++pass_;
        }
    } while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() < budgetMs);
    if (finished())
        stop(renderer);
}

void ProbeBaker::stop(Renderer& renderer)
{
    renderer.setCaptureProbes(nullptr);
}

} // namespace engine
