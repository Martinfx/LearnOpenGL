#include "engine/WorldStreaming.h"

#include "engine/JobSystem.h"
#include "engine/Log.h"
#include "engine/Physics.h"
#include "engine/Scene.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>
#include <utility>

namespace engine {

struct WorldStreamer::Shared {
    std::mutex mutex;
    std::vector<std::pair<std::filesystem::path, std::shared_ptr<Model>>> done; // null model = failed
};

namespace {
std::size_t meshBytes(const Model& m)
{
    std::size_t b = 0;
    for (const MeshPart& p : m.parts)
        b += p.positions.size() * (sizeof(glm::vec3) * 2) + p.texcoords.size() * sizeof(glm::vec2) +
             p.indices.size() * sizeof(std::uint32_t);
    return b;
}

bool streamable(const Model& m)
{
    return !m.dynamic && !m.skeleton && !m.sourcePath.empty();
}

float distanceXZ(const Aabb& box, glm::vec3 p)
{
    const glm::vec2 nearest = glm::clamp(glm::vec2(p.x, p.z), glm::vec2(box.min.x, box.min.z), glm::vec2(box.max.x, box.max.z));
    return glm::distance(nearest, glm::vec2(p.x, p.z));
}
} // namespace

WorldStreamer::WorldStreamer() : shared_(std::make_shared<Shared>()) {}
WorldStreamer::~WorldStreamer() = default;

WorldStreamer::CellKey WorldStreamer::cellOf(glm::vec3 p) const
{
    return {static_cast<int>(std::floor(p.x / cellSize_)), static_cast<int>(std::floor(p.z / cellSize_))};
}

WorldStreamer::Cell* WorldStreamer::cell(CellKey key)
{
    const auto it = cells_.find(key);
    return it == cells_.end() ? nullptr : &it->second;
}

void WorldStreamer::reset(Scene& scene)
{
    cells_.clear();
    resident_.clear();
    pending_.clear();
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->done.clear();
    }
    shared_ = std::make_shared<Shared>(); // results of loads still running go to the old one
    counters_ = Stats{};
    cellSize_ = std::max(scene.streaming.cellSize, 8.0f);
    for (std::size_t i = 0; i < scene.models.size(); ++i) {
        Model& m = scene.models[i];
        if (!streamable(m))
            continue;
        const Aabb b = m.bounds();
        Cell& c = cells_[cellOf((b.min + b.max) * 0.5f)];
        if (c.models.empty())
            c.bounds = b;
        c.bounds.min = glm::min(c.bounds.min, b.min);
        c.bounds.max = glm::max(c.bounds.max, b.max);
        c.models.push_back(i);
        if (!m.deferred && !m.parts.empty()) {
            Resident& r = resident_[m.sourcePath];
            if (!r.model) { // what loadScene read: shared by the copies already
                r.model = std::make_shared<Model>(m);
                r.bytes = meshBytes(m);
            }
            ++r.users;
        }
    }
    for (auto& [key, c] : cells_) {
        // The cell's square, so distances do not jump with what stands in it.
        c.bounds.min.x = std::min(c.bounds.min.x, key.x * cellSize_);
        c.bounds.min.z = std::min(c.bounds.min.z, key.z * cellSize_);
        c.bounds.max.x = std::max(c.bounds.max.x, (key.x + 1) * cellSize_);
        c.bounds.max.z = std::max(c.bounds.max.z, (key.z + 1) * cellSize_);
        const bool all = std::all_of(c.models.begin(), c.models.end(), [&](std::size_t i) { return !scene.models[i].deferred; });
        c.state = all ? CellState::Loaded : CellState::Unloaded;
    }
}

std::size_t WorldStreamer::bytes() const
{
    std::size_t total = 0;
    for (const auto& [path, r] : resident_)
        total += r.bytes;
    return total;
}

void WorldStreamer::request(Scene& scene, PhysicsWorld* physics, Cell& c)
{
    c.state = CellState::Loading;
    for (std::size_t i : c.models) {
        const Model& m = scene.models[i];
        if (!m.deferred)
            continue;
        if (resident_.count(m.sourcePath)) { // another cell has the file: share it now
            attach(scene, physics, i);
            continue;
        }
        if (pending_.count(m.sourcePath))
            continue;
        pending_.insert(m.sourcePath);
        ++counters_.loadsStarted;
        std::weak_ptr<Shared> shared = shared_;
        const std::filesystem::path path = m.sourcePath;
        JobSystem::global().run([shared, path] {
            auto model = std::make_shared<Model>();
            std::string error;
            if (!loadModelFile(*model, path, &error)) {
                LOG_WARN("streaming", "%s\n", error.c_str());
                model.reset();
            }
            if (auto s = shared.lock()) {
                std::lock_guard<std::mutex> lock(s->mutex);
                s->done.emplace_back(path, std::move(model));
            }
        });
    }
}

void WorldStreamer::attach(Scene& scene, PhysicsWorld* physics, std::size_t index)
{
    Model& m = scene.models[index];
    const auto it = resident_.find(m.sourcePath);
    if (!m.deferred || it == resident_.end() || !it->second.model)
        return;
    const Model& source = *it->second.model;
    m.parts = source.parts; // shared, no copy
    m.localBounds = source.localBounds;
    m.deferred = false;
    ++it->second.users;
    if (physics)
        physics->addStaticModel(scene, index);
}

void WorldStreamer::applyLoaded(Scene& scene, PhysicsWorld* physics)
{
    std::vector<std::pair<std::filesystem::path, std::shared_ptr<Model>>> done;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        done.swap(shared_->done);
    }
    for (auto& [path, model] : done) {
        pending_.erase(path);
        if (!model)
            continue;
        Resident& r = resident_[path];
        r.bytes = meshBytes(*model);
        r.model = std::move(model);
    }
    // Cells waiting for these files: attach every model whose file is in.
    for (auto& [key, c] : cells_) {
        if (c.state != CellState::Loading)
            continue;
        bool complete = true;
        for (std::size_t i : c.models) {
            Model& m = scene.models[i];
            if (!m.deferred)
                continue;
            if (resident_.count(m.sourcePath))
                attach(scene, physics, i);
            else if (pending_.count(m.sourcePath))
                complete = false;
            // else: failed to load, the cell does not wait for it
        }
        if (complete)
            c.state = CellState::Loaded;
    }
}

void WorldStreamer::release(Scene& scene, PhysicsWorld* physics, Cell& c)
{
    for (std::size_t i : c.models) {
        Model& m = scene.models[i];
        if (m.deferred || !streamable(m))
            continue;
        if (physics)
            physics->removeStaticModel(i);
        m.parts.clear();
        m.deferred = true;
        if (const auto it = resident_.find(m.sourcePath); it != resident_.end() && --it->second.users <= 0)
            resident_.erase(it); // last user: the mesh is freed (GPU copy once it is not drawn)
    }
    c.state = CellState::Unloaded;
    ++counters_.released;
}

void WorldStreamer::update(Scene& scene, PhysicsWorld* physics, glm::vec3 viewer)
{
    const StreamingSettings& st = scene.streaming;
    applyLoaded(scene, physics);
    if (!st.enabled && std::none_of(cells_.begin(), cells_.end(), [](const auto& kv) { return kv.second.manual; }))
        return;
    const float load = st.enabled ? st.loadRadius : 1e30f;
    const float unload = st.enabled ? std::max(st.unloadRadius, st.loadRadius + cellSize_ * 0.25f) : 1e30f;
    const std::size_t budget = static_cast<std::size_t>(std::max(st.budgetMB, 16)) * 1024u * 1024u;
    // Nearest first: when the budget runs out, the far ones wait.
    std::vector<std::pair<float, Cell*>> order;
    for (auto& [key, c] : cells_)
        order.emplace_back(distanceXZ(c.bounds, viewer), &c);
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::size_t used = bytes();
    counters_.overBudget = false;
    for (auto& [d, c] : order) {
        const bool want = c->manual ? c->wanted : d <= load;
        const bool drop = c->manual ? !c->wanted : d > unload;
        if (want && c->state == CellState::Unloaded) {
            if (used > budget && d > cellSize_) {
                counters_.overBudget = true;
                continue;
            }
            request(scene, physics, *c);
        } else if (drop && c->state != CellState::Unloaded && !c->locked) {
            release(scene, physics, *c);
            used = bytes();
        }
    }
    // Over budget: release the farthest loaded cells outside the load radius first.
    for (auto it = order.rbegin(); it != order.rend() && bytes() > budget; ++it)
        if (it->second->state == CellState::Loaded && !it->second->locked && !it->second->manual && it->first > cellSize_) {
            release(scene, physics, *it->second);
            counters_.overBudget = true;
        }
    applyLoaded(scene, physics); // files other cells already had
}

void WorldStreamer::loadAround(Scene& scene, PhysicsWorld* physics, glm::vec3 viewer)
{
    update(scene, physics, viewer);
    // Wait for the workers (loading screen, teleports); each finished file is applied as it comes.
    while (!pending_.empty()) {
        if (!JobSystem::global().runOne())
            std::this_thread::yield();
        applyLoaded(scene, physics);
    }
    applyLoaded(scene, physics);
}

WorldStreamer::Stats WorldStreamer::stats() const
{
    Stats s = counters_;
    s.cells = static_cast<int>(cells_.size());
    for (const auto& [key, c] : cells_) {
        s.loadedCells += c.state == CellState::Loaded;
        s.loadingCells += c.state == CellState::Loading;
    }
    int streamed = 0;
    for (const auto& [key, c] : cells_)
        streamed += static_cast<int>(c.models.size());
    for (const auto& [path, r] : resident_)
        s.residentModels += r.users;
    s.deferredModels = std::max(0, streamed - s.residentModels);
    s.meshBytes = bytes();
    return s;
}

std::vector<StreamingCellInfo> streamingCells(const Scene& scene)
{
    const float cs = std::max(scene.streaming.cellSize, 8.0f);
    std::map<std::pair<int, int>, StreamingCellInfo> cells;
    for (const Model& m : scene.models) {
        if (!streamable(m))
            continue;
        const Aabb b = m.bounds();
        const glm::vec3 c = (b.min + b.max) * 0.5f;
        const int x = static_cast<int>(std::floor(c.x / cs)), z = static_cast<int>(std::floor(c.z / cs));
        StreamingCellInfo& info = cells[{x, z}];
        if (info.models == 0) {
            info.x = x;
            info.z = z;
            info.bounds = b;
        }
        info.bounds.min = glm::min(info.bounds.min, b.min);
        info.bounds.max = glm::max(info.bounds.max, b.max);
        ++info.models;
    }
    std::vector<StreamingCellInfo> out;
    out.reserve(cells.size());
    for (auto& [key, info] : cells) {
        info.bounds.min.x = std::min(info.bounds.min.x, info.x * cs);
        info.bounds.min.z = std::min(info.bounds.min.z, info.z * cs);
        info.bounds.max.x = std::max(info.bounds.max.x, (info.x + 1) * cs);
        info.bounds.max.z = std::max(info.bounds.max.z, (info.z + 1) * cs);
        out.push_back(info);
    }
    return out;
}

} // namespace engine

