#include "engine/Demo.h"
#include "engine/NavMesh.h"

#include "engine/JobSystem.h"
#include "engine/Scene.h"
#include "engine/Terrain.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>

namespace engine {

namespace {

constexpr unsigned short kWalkable = 1; // poly flag
constexpr int kMaxPathPolys = 512;
constexpr int kMaxCorners = 128;

// Collision geometry of the level, as instances that are expanded into
// triangles per tile (only the instances overlapping that tile).
struct GeoInstance {
    const MeshPart* part = nullptr; // null = unit cube (a box)
    glm::mat4 transform{1.0f};
    glm::vec3 min{0.0f}, max{0.0f}; // world bounds
};

struct TileGeometry {
    std::vector<float> verts;
    std::vector<int> tris;

    void add(glm::vec3 a, glm::vec3 b, glm::vec3 c)
    {
        const int base = static_cast<int>(verts.size() / 3);
        for (const glm::vec3& v : {a, b, c}) {
            verts.push_back(v.x);
            verts.push_back(v.y);
            verts.push_back(v.z);
        }
        tris.push_back(base);
        tris.push_back(base + 1);
        tris.push_back(base + 2);
    }
};

void boundsOf(const glm::mat4& m, glm::vec3 lo, glm::vec3 hi, glm::vec3& outMin, glm::vec3& outMax)
{
    outMin = glm::vec3(1e30f);
    outMax = glm::vec3(-1e30f);
    for (int c = 0; c < 8; ++c) {
        const glm::vec3 p = glm::vec3(m * glm::vec4((c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z, 1.0f));
        outMin = glm::min(outMin, p);
        outMax = glm::max(outMax, p);
    }
}

void partBounds(const MeshPart& part, glm::vec3& lo, glm::vec3& hi)
{
    lo = glm::vec3(1e30f);
    hi = glm::vec3(-1e30f);
    for (const glm::vec3& p : part.positions) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
}

// Gathers what the player collides with (see PhysicsWorld::setLevel).
std::vector<GeoInstance> gatherInstances(const Scene& scene)
{
    std::vector<GeoInstance> out;
    for (const Box& b : scene.boxes) {
        GeoInstance g;
        g.transform = b.transform(); // unit cube -> world
        boundsOf(g.transform, glm::vec3(-0.5f), glm::vec3(0.5f), g.min, g.max);
        out.push_back(g);
    }
    auto addModel = [&](const Model& m, const glm::mat4& transform) {
        for (const MeshPart& part : m.parts) {
            if (part.indices.empty())
                continue;
            GeoInstance g;
            g.part = &part;
            g.transform = transform;
            glm::vec3 lo, hi;
            partBounds(part, lo, hi);
            boundsOf(transform, lo, hi, g.min, g.max);
            out.push_back(g);
        }
    };
    for (const Model& m : scene.models)
        if (m.collision)
            addModel(m, m.transform());
    if (scene.terrain && scene.terrain->valid()) {
        const Terrain& t = *scene.terrain;
        for (const FoliageInstance& f : t.foliage) {
            if (f.type >= t.foliageTypes.size())
                continue;
            const FoliageType& type = t.foliageTypes[f.type];
            if (!type.collision || !type.model)
                continue;
            // Same rotation as the physics body (PhysicsWorld::setLevel).
            glm::mat4 m = glm::translate(glm::mat4(1.0f), f.position);
            m = glm::rotate(m, f.tiltX, glm::vec3(1, 0, 0));
            m = glm::rotate(m, f.tiltZ, glm::vec3(0, 0, 1));
            m = glm::rotate(m, glm::radians(f.yawDegrees), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(f.scale > 0.0f ? f.scale : 1.0f));
            addModel(*type.model, m);
        }
    }
    return out;
}

void emitInstance(const GeoInstance& g, glm::vec3 lo, glm::vec3 hi, TileGeometry& geo)
{
    auto inside = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        const glm::vec3 tmin = glm::min(a, glm::min(b, c)), tmax = glm::max(a, glm::max(b, c));
        return tmax.x >= lo.x && tmin.x <= hi.x && tmax.z >= lo.z && tmin.z <= hi.z;
    };
    if (!g.part) {
        static const glm::vec3 corners[8] = {{-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f},
                                             {-.5f, -.5f, .5f},  {.5f, -.5f, .5f},  {.5f, .5f, .5f},  {-.5f, .5f, .5f}};
        static const int faces[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                                         {3, 6, 2}, {3, 7, 6}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
        glm::vec3 w[8];
        for (int i = 0; i < 8; ++i)
            w[i] = glm::vec3(g.transform * glm::vec4(corners[i], 1.0f));
        for (const auto& f : faces)
            geo.add(w[f[0]], w[f[1]], w[f[2]]);
        return;
    }
    const MeshPart& part = *g.part;
    // Winding does not matter to Recast's slope test except for the normal sign:
    // mirrored transforms flip it, so the triangle order follows the determinant.
    const bool flip = glm::determinant(glm::mat3(g.transform)) < 0.0f;
    for (std::size_t i = 0; i + 2 < part.indices.size(); i += 3) {
        const std::uint32_t ia = part.indices[i], ib = part.indices[i + 1], ic = part.indices[i + 2];
        if (ia >= part.positions.size() || ib >= part.positions.size() || ic >= part.positions.size())
            continue;
        const glm::vec3 a = glm::vec3(g.transform * glm::vec4(part.positions[ia], 1.0f));
        const glm::vec3 b = glm::vec3(g.transform * glm::vec4(part.positions[ib], 1.0f));
        const glm::vec3 c = glm::vec3(g.transform * glm::vec4(part.positions[ic], 1.0f));
        if (!inside(a, b, c))
            continue;
        if (flip)
            geo.add(a, c, b);
        else
            geo.add(a, b, c);
    }
}

void emitTerrain(const Terrain& t, glm::vec3 lo, glm::vec3 hi, TileGeometry& geo)
{
    const float d = t.spacing();
    const int n = t.resolution;
    const int x0 = std::clamp(static_cast<int>(std::floor((lo.x - t.origin.x) / d)), 0, n - 1);
    const int x1 = std::clamp(static_cast<int>(std::ceil((hi.x - t.origin.x) / d)), 0, n - 1);
    const int z0 = std::clamp(static_cast<int>(std::floor((lo.z - t.origin.y) / d)), 0, n - 1);
    const int z1 = std::clamp(static_cast<int>(std::ceil((hi.z - t.origin.y) / d)), 0, n - 1);
    auto at = [&](int x, int z) {
        return glm::vec3(t.origin.x + x * d, t.heights[static_cast<std::size_t>(z) * n + x], t.origin.y + z * d);
    };
    for (int z = z0; z < z1; ++z)
        for (int x = x0; x < x1; ++x) {
            // Counter-clockwise seen from above (+Y normal), Recast's walkable side.
            geo.add(at(x, z), at(x, z + 1), at(x + 1, z));
            geo.add(at(x + 1, z), at(x, z + 1), at(x + 1, z + 1));
        }
}

float frand()
{
    thread_local std::mt19937 rng{seedFor("nav")};
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

struct RecastDeleter {
    void operator()(rcHeightfield* p) const { rcFreeHeightField(p); }
    void operator()(rcCompactHeightfield* p) const { rcFreeCompactHeightfield(p); }
    void operator()(rcContourSet* p) const { rcFreeContourSet(p); }
    void operator()(rcPolyMesh* p) const { rcFreePolyMesh(p); }
    void operator()(rcPolyMeshDetail* p) const { rcFreePolyMeshDetail(p); }
};

} // namespace

struct NavMesh::Impl {
    dtNavMesh* mesh = nullptr;
    dtNavMeshQuery* query = nullptr;
    dtQueryFilter filter;
    mutable std::mutex mutex; // dtNavMeshQuery keeps per-query state
    double seconds = 0.0;
    int tiles = 0;
    int polys = 0;

    Impl() { filter.setIncludeFlags(kWalkable); }
    ~Impl() { clear(); }
    void clear()
    {
        dtFreeNavMeshQuery(query);
        dtFreeNavMesh(mesh);
        query = nullptr;
        mesh = nullptr;
        tiles = polys = 0;
    }

    dtPolyRef nearest(glm::vec3 p, glm::vec3 extents, float* out) const
    {
        dtPolyRef ref = 0;
        const float half[3] = {extents.x, extents.y, extents.z};
        query->findNearestPoly(&p.x, half, &filter, &ref, out);
        return ref;
    }
};

NavMesh::NavMesh() : impl_(std::make_unique<Impl>()) {}
NavMesh::~NavMesh() = default;

bool NavMesh::ready() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->query != nullptr;
}

bool NavMesh::build(const Scene& scene, const NavSettings& s, std::string* error, std::atomic<float>* progress,
                    const std::atomic<bool>* cancel)
{
    const auto start = std::chrono::steady_clock::now();
    auto fail = [&](const std::string& message) {
        if (error)
            *error = "Navmesh: " + message;
        return false;
    };
    const std::vector<GeoInstance> instances = gatherInstances(scene);
    const Terrain* terrain = scene.terrain && scene.terrain->valid() ? scene.terrain.get() : nullptr;

    // Level bounds in XZ (and Y for the heightfield).
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const GeoInstance& g : instances) {
        lo = glm::min(lo, g.min);
        hi = glm::max(hi, g.max);
    }
    if (terrain) {
        const auto [mn, mx] = std::minmax_element(terrain->heights.begin(), terrain->heights.end());
        lo = glm::min(lo, glm::vec3(terrain->origin.x, *mn, terrain->origin.y));
        hi = glm::max(hi, glm::vec3(terrain->origin.x + terrain->size, *mx, terrain->origin.y + terrain->size));
    }
    if (lo.x > hi.x)
        return fail("the level has no collision geometry");

    const float cs = std::max(0.05f, s.cellSize), ch = std::max(0.02f, s.cellHeight);
    const int tileCells = std::max(16, static_cast<int>(s.tileSize / cs));
    const float tileWorld = tileCells * cs;
    const int tilesX = std::max(1, static_cast<int>(std::ceil((hi.x - lo.x) / tileWorld)));
    const int tilesZ = std::max(1, static_cast<int>(std::ceil((hi.z - lo.z) / tileWorld)));
    // 32-bit poly refs: tile bits + poly bits = 22 (salt takes the rest).
    int tileBits = 1;
    while ((1 << tileBits) < tilesX * tilesZ)
        ++tileBits;
    if (tileBits > 14)
        return fail("level too large for the tile size (" + std::to_string(tilesX * tilesZ) + " tiles), raise the tile size");
    const int polyBits = 22 - tileBits;

    dtNavMesh* mesh = dtAllocNavMesh();
    dtNavMeshParams params{};
    params.orig[0] = lo.x;
    params.orig[1] = lo.y;
    params.orig[2] = lo.z;
    params.tileWidth = tileWorld;
    params.tileHeight = tileWorld;
    params.maxTiles = 1 << tileBits;
    params.maxPolys = 1 << polyBits;
    if (!mesh || dtStatusFailed(mesh->init(&params))) {
        dtFreeNavMesh(mesh);
        return fail("could not create the tiled mesh");
    }

    rcConfig cfg{};
    cfg.cs = cs;
    cfg.ch = ch;
    cfg.walkableSlopeAngle = std::clamp(s.maxSlope, 0.0f, 89.0f);
    cfg.walkableHeight = static_cast<int>(std::ceil(s.agentHeight / ch));
    cfg.walkableClimb = static_cast<int>(std::floor(s.agentClimb / ch));
    cfg.walkableRadius = static_cast<int>(std::ceil(s.agentRadius / cs));
    cfg.maxEdgeLen = static_cast<int>(12.0f / cs);
    cfg.maxSimplificationError = 1.3f;
    cfg.minRegionArea = static_cast<int>(s.minRegion / (cs * cs));
    cfg.mergeRegionArea = static_cast<int>(20.0f / (cs * cs));
    cfg.maxVertsPerPoly = 6;
    cfg.tileSize = tileCells;
    cfg.borderSize = cfg.walkableRadius + 3;
    cfg.width = cfg.tileSize + cfg.borderSize * 2;
    cfg.height = cfg.tileSize + cfg.borderSize * 2;
    cfg.detailSampleDist = cs * 6.0f;
    cfg.detailSampleMaxError = ch;

    std::mutex addMutex;
    std::atomic<int> done{0}, polys{0}, tilesBuilt{0};
    std::atomic<bool> failed{false};
    const int total = tilesX * tilesZ;

    auto buildTile = [&](int tx, int tz) {
        if ((cancel && *cancel) || failed)
            return;
        rcConfig tc = cfg;
        tc.bmin[0] = lo.x + tx * tileWorld - cfg.borderSize * cs;
        tc.bmin[1] = lo.y - 1.0f;
        tc.bmin[2] = lo.z + tz * tileWorld - cfg.borderSize * cs;
        tc.bmax[0] = lo.x + (tx + 1) * tileWorld + cfg.borderSize * cs;
        tc.bmax[1] = hi.y + 1.0f;
        tc.bmax[2] = lo.z + (tz + 1) * tileWorld + cfg.borderSize * cs;
        const glm::vec3 tmin(tc.bmin[0], tc.bmin[1], tc.bmin[2]), tmax(tc.bmax[0], tc.bmax[1], tc.bmax[2]);

        TileGeometry geo;
        for (const GeoInstance& g : instances)
            if (g.max.x >= tmin.x && g.min.x <= tmax.x && g.max.z >= tmin.z && g.min.z <= tmax.z)
                emitInstance(g, tmin, tmax, geo);
        if (terrain)
            emitTerrain(*terrain, tmin, tmax, geo);
        if (geo.tris.empty())
            return;

        rcContext ctx(false);
        const int nverts = static_cast<int>(geo.verts.size() / 3), ntris = static_cast<int>(geo.tris.size() / 3);
        std::unique_ptr<rcHeightfield, RecastDeleter> solid(rcAllocHeightfield());
        if (!solid || !rcCreateHeightfield(&ctx, *solid, tc.width, tc.height, tc.bmin, tc.bmax, tc.cs, tc.ch))
            return;
        std::vector<unsigned char> areas(static_cast<std::size_t>(ntris), RC_NULL_AREA);
        rcMarkWalkableTriangles(&ctx, tc.walkableSlopeAngle, geo.verts.data(), nverts, geo.tris.data(), ntris, areas.data());
        if (!rcRasterizeTriangles(&ctx, geo.verts.data(), nverts, geo.tris.data(), areas.data(), ntris, *solid, tc.walkableClimb))
            return;
        rcFilterLowHangingWalkableObstacles(&ctx, tc.walkableClimb, *solid);
        rcFilterLedgeSpans(&ctx, tc.walkableHeight, tc.walkableClimb, *solid);
        rcFilterWalkableLowHeightSpans(&ctx, tc.walkableHeight, *solid);
        std::unique_ptr<rcCompactHeightfield, RecastDeleter> chf(rcAllocCompactHeightfield());
        if (!chf || !rcBuildCompactHeightfield(&ctx, tc.walkableHeight, tc.walkableClimb, *solid, *chf))
            return;
        solid.reset();
        if (!rcErodeWalkableArea(&ctx, tc.walkableRadius, *chf) || !rcBuildDistanceField(&ctx, *chf) ||
            !rcBuildRegions(&ctx, *chf, tc.borderSize, tc.minRegionArea, tc.mergeRegionArea))
            return;
        std::unique_ptr<rcContourSet, RecastDeleter> cset(rcAllocContourSet());
        if (!cset || !rcBuildContours(&ctx, *chf, tc.maxSimplificationError, tc.maxEdgeLen, *cset) || cset->nconts == 0)
            return;
        std::unique_ptr<rcPolyMesh, RecastDeleter> pmesh(rcAllocPolyMesh());
        if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, tc.maxVertsPerPoly, *pmesh) || pmesh->npolys == 0)
            return;
        std::unique_ptr<rcPolyMeshDetail, RecastDeleter> dmesh(rcAllocPolyMeshDetail());
        if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, tc.detailSampleDist, tc.detailSampleMaxError, *dmesh))
            return;
        if (pmesh->npolys > (1 << polyBits)) {
            failed = true;
            return;
        }
        for (int i = 0; i < pmesh->npolys; ++i)
            pmesh->flags[i] = pmesh->areas[i] == RC_WALKABLE_AREA ? kWalkable : 0;

        dtNavMeshCreateParams p{};
        p.verts = pmesh->verts;
        p.vertCount = pmesh->nverts;
        p.polys = pmesh->polys;
        p.polyAreas = pmesh->areas;
        p.polyFlags = pmesh->flags;
        p.polyCount = pmesh->npolys;
        p.nvp = pmesh->nvp;
        p.detailMeshes = dmesh->meshes;
        p.detailVerts = dmesh->verts;
        p.detailVertsCount = dmesh->nverts;
        p.detailTris = dmesh->tris;
        p.detailTriCount = dmesh->ntris;
        p.walkableHeight = s.agentHeight;
        p.walkableRadius = s.agentRadius;
        p.walkableClimb = s.agentClimb;
        p.tileX = tx;
        p.tileY = tz;
        p.tileLayer = 0;
        std::copy(pmesh->bmin, pmesh->bmin + 3, p.bmin);
        std::copy(pmesh->bmax, pmesh->bmax + 3, p.bmax);
        p.cs = tc.cs;
        p.ch = tc.ch;
        p.buildBvTree = true;
        unsigned char* data = nullptr;
        int size = 0;
        if (!dtCreateNavMeshData(&p, &data, &size))
            return;
        std::lock_guard lock(addMutex);
        if (dtStatusFailed(mesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr))) {
            dtFree(data);
            return;
        }
        polys += pmesh->npolys;
        ++tilesBuilt;
    };

    JobSystem::global().parallelFor(static_cast<std::size_t>(total), 1, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            buildTile(static_cast<int>(i % static_cast<std::size_t>(tilesX)), static_cast<int>(i / static_cast<std::size_t>(tilesX)));
            const int d = ++done;
            if (progress)
                *progress = static_cast<float>(d) / static_cast<float>(total);
        }
    });
    if ((cancel && *cancel) || failed) {
        dtFreeNavMesh(mesh);
        return fail(failed ? "a tile has too many polygons, raise the cell size or lower the tile size" : "cancelled");
    }
    dtNavMeshQuery* query = dtAllocNavMeshQuery();
    if (!query || dtStatusFailed(query->init(mesh, 4096))) {
        dtFreeNavMeshQuery(query);
        dtFreeNavMesh(mesh);
        return fail("could not create the query");
    }
    std::lock_guard lock(impl_->mutex);
    impl_->clear();
    impl_->mesh = mesh;
    impl_->query = query;
    impl_->tiles = tilesBuilt;
    impl_->polys = polys;
    impl_->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return true;
}

std::vector<glm::vec3> NavMesh::findPath(glm::vec3 from, glm::vec3 to) const
{
    std::lock_guard lock(impl_->mutex);
    std::vector<glm::vec3> out;
    if (!impl_->query)
        return out;
    glm::vec3 a, b;
    const dtPolyRef startRef = impl_->nearest(from, {2.0f, 4.0f, 2.0f}, &a.x);
    const dtPolyRef endRef = impl_->nearest(to, {2.0f, 4.0f, 2.0f}, &b.x);
    if (!startRef || !endRef)
        return out;
    std::array<dtPolyRef, kMaxPathPolys> path{};
    int count = 0;
    impl_->query->findPath(startRef, endRef, &a.x, &b.x, &impl_->filter, path.data(), &count, kMaxPathPolys);
    if (count == 0)
        return out;
    if (path[static_cast<std::size_t>(count - 1)] != endRef) // partial: end as close as we got
        impl_->query->closestPointOnPoly(path[static_cast<std::size_t>(count - 1)], &b.x, &b.x, nullptr);
    std::array<float, kMaxCorners * 3> corners{};
    int n = 0;
    impl_->query->findStraightPath(&a.x, &b.x, path.data(), count, corners.data(), nullptr, nullptr, &n, kMaxCorners);
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        out.emplace_back(corners[static_cast<std::size_t>(i) * 3], corners[static_cast<std::size_t>(i) * 3 + 1],
                         corners[static_cast<std::size_t>(i) * 3 + 2]);
    return out;
}

std::optional<glm::vec3> NavMesh::nearestPoint(glm::vec3 p, glm::vec3 extents) const
{
    std::lock_guard lock(impl_->mutex);
    if (!impl_->query)
        return std::nullopt;
    glm::vec3 out;
    if (!impl_->nearest(p, extents, &out.x))
        return std::nullopt;
    return out;
}

std::optional<glm::vec3> NavMesh::randomPointAround(glm::vec3 center, float radius) const
{
    std::lock_guard lock(impl_->mutex);
    if (!impl_->query)
        return std::nullopt;
    glm::vec3 c;
    const dtPolyRef ref = impl_->nearest(center, {2.0f, 4.0f, 2.0f}, &c.x);
    if (!ref)
        return std::nullopt;
    dtPolyRef outRef = 0;
    glm::vec3 out;
    if (dtStatusFailed(impl_->query->findRandomPointAroundCircle(ref, &c.x, radius, &impl_->filter, frand, &outRef, &out.x)) ||
        !outRef)
        return std::nullopt;
    return out;
}

bool NavMesh::clearPath(glm::vec3 from, glm::vec3 to) const
{
    std::lock_guard lock(impl_->mutex);
    if (!impl_->query)
        return false;
    glm::vec3 a;
    const dtPolyRef ref = impl_->nearest(from, {1.0f, 2.0f, 1.0f}, &a.x);
    if (!ref)
        return false;
    float t = 0.0f;
    float normal[3];
    std::array<dtPolyRef, 64> polys{};
    int n = 0;
    impl_->query->raycast(ref, &a.x, &to.x, &impl_->filter, &t, normal, polys.data(), &n, static_cast<int>(polys.size()));
    return t >= 1.0f; // FLT_MAX when the ray reaches the end
}

std::vector<glm::vec3> NavMesh::debugTriangles() const
{
    std::lock_guard lock(impl_->mutex);
    std::vector<glm::vec3> out;
    const dtNavMesh* mesh = impl_->mesh;
    if (!mesh)
        return out;
    for (int i = 0; i < mesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = mesh->getTile(i);
        if (!tile || !tile->header)
            continue;
        for (int p = 0; p < tile->header->polyCount; ++p) {
            const dtPoly& poly = tile->polys[p];
            if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
                continue;
            const dtPolyDetail& pd = tile->detailMeshes[p];
            for (int j = 0; j < pd.triCount; ++j) {
                const unsigned char* t = &tile->detailTris[(pd.triBase + static_cast<unsigned>(j)) * 4];
                for (int k = 0; k < 3; ++k) {
                    const float* v = t[k] < poly.vertCount ? &tile->verts[poly.verts[t[k]] * 3]
                                                           : &tile->detailVerts[(pd.vertBase + t[k] - poly.vertCount) * 3];
                    out.emplace_back(v[0], v[1] + 0.05f, v[2]);
                }
            }
        }
    }
    return out;
}

int NavMesh::tileCount() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->tiles;
}

int NavMesh::polygonCount() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->polys;
}

double NavMesh::buildSeconds() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->seconds;
}

} // namespace engine
