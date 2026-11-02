#pragma once
#include <glm/vec3.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace engine {

struct Scene;

// Walkable-area settings of a level (`navmesh` line, editor: Level > Navigation).
// The agent values describe the characters that walk on it (assets/SCALE.md: 1.8 m).
struct NavSettings {
    bool enabled = true;
    float cellSize = 0.25f;     // m, horizontal voxel size (smaller = more precise, slower)
    float cellHeight = 0.1f;    // m
    float agentRadius = 0.35f;  // m, kept away from walls
    float agentHeight = 1.8f;   // m, needs this much headroom
    float agentClimb = 0.4f;    // m, steps up to this height are walkable
    float maxSlope = 45.0f;     // degrees
    float tileSize = 32.0f;     // m, tiles build in parallel
    float minRegion = 4.0f;     // m², smaller islands are dropped (rooftops, table tops)
};

// Navigation mesh (Recast) and path queries (Detour). build() is slow and runs
// on any thread (tiles in parallel on the job system); queries are cheap and
// may be called from any thread once ready().
class NavMesh {
public:
    NavMesh();
    ~NavMesh();
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    // Builds from the level's collision geometry (boxes, models with collision,
    // terrain, foliage with collision), the same set the player collides with.
    // Finished tiles are kept in the disk cache (<cache>/navmesh, textureCookConfig) under
    // a hash of their triangles and the settings; unchanged tiles load from it next time.
    // `progress` (0..1) and `cancel` may be watched / set from another thread.
    bool build(const Scene& scene, const NavSettings& settings, std::string* error = nullptr,
               std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);
    bool ready() const;

    // Corner points from `from` to `to` along the mesh (both snapped onto it first);
    // empty when either end is off the mesh. A partial path ends as close as it gets.
    std::vector<glm::vec3> findPath(glm::vec3 from, glm::vec3 to) const;
    // Closest point on the mesh within `extents` (half size of the search box).
    std::optional<glm::vec3> nearestPoint(glm::vec3 p, glm::vec3 extents = {2.0f, 4.0f, 2.0f}) const;
    // A random reachable point within `radius` of `center` (patrols).
    std::optional<glm::vec3> randomPointAround(glm::vec3 center, float radius) const;
    // Walks the mesh surface from `from` toward `to`: true if nothing blocks the way.
    bool clearPath(glm::vec3 from, glm::vec3 to) const;

    // Mesh triangles for drawing (3 vertices each, slightly above the surface).
    std::vector<glm::vec3> debugTriangles() const;
    int tileCount() const;
    int cachedTileCount() const; // tiles of the last build read from the disk cache (unchanged since an earlier build)
    int polygonCount() const;
    double buildSeconds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
