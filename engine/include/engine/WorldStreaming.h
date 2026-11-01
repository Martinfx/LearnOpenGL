#pragma once
#include "engine/Geometry.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace engine {

struct Scene;
struct Model;
class PhysicsWorld;

// World streaming (Scene::streaming): static models are grouped into square cells by
// the centre of their bounds. update() (main thread, once per frame) loads the meshes
// of cells near the viewer on the job system and releases far ones (mesh and
// collision; the renderer frees their GPU data when they are no longer drawn).
// Dynamic props and animated models always stay loaded.
class WorldStreamer {
public:
    WorldStreamer();
    ~WorldStreamer();

    // Groups the scene's static models into cells; models already loaded are tracked
    // as resident. Call after loadScene (and PhysicsWorld::setLevel).
    void reset(Scene& scene);
    // Applies finished loads, starts loads for near cells, releases far ones.
    // physics may be null (editor).
    void update(Scene& scene, PhysicsWorld* physics, glm::vec3 viewer);
    // Loads every cell within the load radius now (blocking): after a teleport or load.
    void loadAround(Scene& scene, PhysicsWorld* physics, glm::vec3 viewer);

    struct CellKey {
        int x = 0, z = 0;
        auto operator<=>(const CellKey&) const = default;
    };
    enum class CellState { Unloaded, Loading, Loaded };
    struct Cell {
        Aabb bounds;                     // of its models (world)
        std::vector<std::size_t> models; // Scene::models indices
        CellState state = CellState::Unloaded;
        bool locked = false;             // editor: never released
        bool wanted = false;             // editor: force loaded / unloaded by hand
        bool manual = false;             // `wanted` overrides the distance
    };
    const std::map<CellKey, Cell>& cells() const { return cells_; }
    Cell* cell(CellKey key);
    CellKey cellOf(glm::vec3 point) const;
    float cellSize() const { return cellSize_; }

    struct Stats {
        int cells = 0, loadedCells = 0, loadingCells = 0;
        int residentModels = 0, deferredModels = 0;
        std::size_t meshBytes = 0;      // vertices + indices of the resident meshes streamed
        int loadsStarted = 0, released = 0; // since reset
        bool overBudget = false;
    };
    Stats stats() const;

private:
    struct Shared; // results handed back from the workers
    std::shared_ptr<Shared> shared_;
    std::map<CellKey, Cell> cells_;
    float cellSize_ = 128.0f;
    // Loaded mesh files: parts shared by every model using the file, with a use count.
    struct Resident {
        std::shared_ptr<Model> model;
        int users = 0;
        std::size_t bytes = 0;
    };
    std::map<std::filesystem::path, Resident> resident_;
    std::set<std::filesystem::path> pending_;
    Stats counters_;
    void applyLoaded(Scene& scene, PhysicsWorld* physics);
    void attach(Scene& scene, PhysicsWorld* physics, std::size_t model);
    void release(Scene& scene, PhysicsWorld* physics, Cell& cell);
    void request(Scene& scene, PhysicsWorld* physics, Cell& cell);
    std::size_t bytes() const;
};

// The cells the game would stream, grouped like WorldStreamer::reset (editor overlay;
// loads nothing). Models without a mesh source or dynamic / animated stay out.
struct StreamingCellInfo {
    int x = 0, z = 0;
    Aabb bounds;     // models' bounds grown to the cell square
    int models = 0;
};
std::vector<StreamingCellInfo> streamingCells(const Scene& scene);

} // namespace engine
