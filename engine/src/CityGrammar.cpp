#include "engine/CityGrammar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace engine::city {

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- materials (zone texture sets)
struct MatDef {
    const char* name;
    const char* tex;     // <tex>_albedo.jpg/_normal.jpg/_orm.jpg, nullptr = plain colour
    float tile;          // metres per texture repeat
    float r = 1.0f, g = 1.0f, b = 1.0f;
    float rough = 1.0f, metal = 1.0f;
    float alpha = 1.0f;  // < 1: blended (glass)
    bool decal = false;  // RGBA png over the wall (grime)
};

const MatDef kMats[] = {
    {"brick_red", "brick_red", 1.4f},
    {"brick", "brick", 2.0f},
    {"st_brick_red", "st_brick_red", 1.0f},
    {"st_brick_dark", "st_brick_dark", 1.05f},
    {"st_brick_worn", "st_brick_worn", 2.0f},
    {"plaster", "plaster", 2.5f},
    {"plaster_yellow", "plaster_yellow", 2.0f},
    {"plaster_peel", "plaster_peel", 1.8f},
    {"plaster_green", "plaster_green", 2.5f},
    {"plaster_blue", "plaster_blue", 2.5f},
    {"plaster_mossy", "plaster_mossy", 1.8f},
    {"st_plaster_beige", "st_plaster_beige", 3.0f},
    {"st_plaster_crack", "st_plaster_crack", 1.8f},
    {"st_plaster_brick", "st_plaster_brick", 1.5f},
    {"silicate", "silicate", 2.0f},
    {"silicate_dirty", "silicate_dirty", 1.5f},
    {"panel", "panel", 2.1f},
    {"concrete", "concrete", 3.0f},
    {"concrete_moss", "concrete_moss", 4.0f},
    {"st_concrete_crack", "st_concrete_crack", 2.0f},
    {"interior_dark", "concrete_dark", 3.0f, 0.25f, 0.25f, 0.25f},
    {"frame_concrete", "concrete_dark", 2.5f, 0.82f, 0.82f, 0.82f}, // weathered grey frame of a skeleton
    {"roof_tar", "roof_tar", 4.0f},
    {"rust_sheet", "rust_sheet", 2.0f},
    {"rust", "rust", 1.5f},
    {"rust_dark", "rust_dark", 1.5f},
    {"wood", "wood", 1.5f},
    {"st_metal_paint", "st_metal_paint", 2.2f},
    {"glass", nullptr, 1.0f, 0.05f, 0.06f, 0.06f, 0.08f, 0.0f, 0.35f},
    {"grime", "grime", 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, true},
    {"shop_signs", "shop_signs", 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, true}, // zonegen/shop_signs.py: 2 x 8 painted boards
};

const MatDef& mat(const std::string& name)
{
    for (const auto& m : kMats)
        if (name == m.name)
            return m;
    return kMats[0];
}

// ---------------------------------------------------------------- rng (same numbers everywhere)
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
    std::uint64_t next()
    {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float uniform() { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * uniform(); }
    int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
    bool chance(float p) { return uniform() < p; }
};

// ---------------------------------------------------------------- mesh
struct Prim {
    std::vector<glm::vec3> p, n;
    std::vector<glm::vec2> uv;
    std::vector<std::uint32_t> idx;
};

class Mesh {
public:
    std::map<std::string, Prim> prims;

    // A planar quad a b c d (in order around it) facing `nrm`.
    void quad(const std::string& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 nrm, glm::vec2 ua, glm::vec2 ub,
              glm::vec2 uc, glm::vec2 ud)
    {
        Prim& P = prims[m];
        const auto base = static_cast<std::uint32_t>(P.p.size());
        const bool flip = glm::dot(glm::cross(b - a, c - a), nrm) < 0.0f;
        P.p.insert(P.p.end(), {a, b, c, d});
        P.n.insert(P.n.end(), {nrm, nrm, nrm, nrm});
        P.uv.insert(P.uv.end(), {ua, ub, uc, ud});
        if (!flip)
            P.idx.insert(P.idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        else
            P.idx.insert(P.idx.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }

    // Horizontal polygon at height y facing up (or down), UVs from world x/z.
    void flat(const std::string& m, const Poly& poly, float y, bool up)
    {
        if (poly.size() < 3)
            return;
        const float tile = mat(m).tile;
        Prim& P = prims[m];
        const auto base = static_cast<std::uint32_t>(P.p.size());
        for (const auto& q : poly) {
            P.p.push_back({q.x, y, q.y});
            P.n.push_back({0.0f, up ? 1.0f : -1.0f, 0.0f});
            P.uv.push_back({q.x / tile, q.y / tile});
        }
        const auto tris = triangulate(poly);
        for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
            const glm::vec3 a = P.p[base + tris[i]], b = P.p[base + tris[i + 1]], c = P.p[base + tris[i + 2]];
            const bool faceUp = glm::cross(b - a, c - a).y > 0.0f;
            if (faceUp == up)
                P.idx.insert(P.idx.end(), {base + tris[i], base + tris[i + 1], base + tris[i + 2]});
            else
                P.idx.insert(P.idx.end(), {base + tris[i], base + tris[i + 2], base + tris[i + 1]});
        }
    }

    int triangles() const
    {
        std::size_t n = 0;
        for (const auto& [k, p] : prims)
            n += p.idx.size() / 3;
        return static_cast<int>(n);
    }
};

// A facade plane: s along the edge, y up, d into the building.
struct Face {
    glm::vec3 o, t, n; // origin (edge start at y = 0), along, outward
    float len = 0.0f;
    glm::vec3 at(float s, float y, float d) const { return o + t * s + glm::vec3(0.0f, y, 0.0f) - n * d; }
};

void bar(Mesh& mesh, const std::string& m, glm::vec3 a, glm::vec3 b, float w, float h);

class Builder {
public:
    Builder(const BuildingSpec& b, int lod) : b_(b), st_(b.style), lod_(lod), rng_(b.seed), open_(b.ruin || (b.interior && lod == 0)) {}

    Mesh mesh;
    int windows = 0;
    std::vector<BalconySpot> balconies_;
    std::vector<LadderSpot> ladders_;

    void run()
    {
        const int F = std::max(1, b_.floors);
        top_ = floorY(F);
        const std::size_t n = b_.footprint.size();
        if (n < 3)
            return;
        // Ruins: one corner of the house came down; storeys left per spot.
        if (b_.ruin) {
            const float a = rng_.range(0.0f, 6.2831853f);
            ruinDir_ = {std::cos(a), std::sin(a)};
            glm::vec2 lo{1e9f}, hi{-1e9f};
            for (const auto& p : b_.footprint) {
                lo = glm::min(lo, p);
                hi = glm::max(hi, p);
            }
            ruinR_ = std::max(4.0f, glm::length(hi - lo) * 0.5f);
            ruinLoss_ = std::max(1, static_cast<int>(static_cast<float>(F) * rng_.range(0.35f, 0.7f)));
        }
        loggiaCols_ = st_.loggias;
        for (std::size_t i = 0; i < n; ++i)
            facade(static_cast<int>(i));
        roof();
        if (b_.ruin && lod_ == 0)
            ruinSlabs();
        else if (open_)
            interior();
    }

private:
    float floorY(int f) const { return f <= 0 ? 0.0f : st_.groundStorey + static_cast<float>(f - 1) * st_.storey; }

    int floorsAt(glm::vec2 p) const
    {
        const int F = std::max(1, b_.floors);
        if (!b_.ruin)
            return F;
        const float k = std::clamp((glm::dot(p, ruinDir_) / ruinR_ + 0.2f) / 1.0f, 0.0f, 1.0f);
        const float jag = static_cast<float>((static_cast<int>(std::floor(p.x * 0.37f + p.y * 0.53f)) * 2654435761u >> 28) & 1u);
        return std::max(1, F - static_cast<int>(std::lround(static_cast<float>(ruinLoss_) * k + jag * k)));
    }

    // ---- wall pieces
    void wall(const std::string& m, const Face& F, float s0, float s1, float y0, float y1, float d = 0.0f, bool inward = false)
    {
        if (s1 - s0 < 1e-3f || y1 - y0 < 1e-3f)
            return;
        const float tile = mat(m).tile;
        const float u0 = s0 / tile, u1 = s1 / tile, v0 = y0 / tile, v1 = y1 / tile;
        mesh.quad(m, F.at(s0, y0, d), F.at(s1, y0, d), F.at(s1, y1, d), F.at(s0, y1, d), inward ? -F.n : F.n, {u0, -v0}, {u1, -v0}, {u1, -v1},
                  {u0, -v1});
        if (open_ && !inward && d == 0.0f && m != "grime") // ruins and enterable houses are open: the walls have an inside
            mesh.quad("plaster", F.at(s0, y0, kWall), F.at(s1, y0, kWall), F.at(s1, y1, kWall), F.at(s0, y1, kWall), -F.n, {u0, -v0}, {u1, -v0},
                      {u1, -v1}, {u0, -v1});
    }

    // Side faces of an opening (into depth D).
    void reveal(const std::string& m, const Face& F, float s0, float s1, float y0, float y1, float D, bool bottom = true)
    {
        const float tile = mat(m).tile;
        auto v = [&](float d, float y) { return glm::vec2(d / tile, -y / tile); };
        mesh.quad(m, F.at(s0, y0, 0.0f), F.at(s0, y0, D), F.at(s0, y1, D), F.at(s0, y1, 0.0f), F.t, v(0, y0), v(D, y0), v(D, y1), v(0, y1));
        mesh.quad(m, F.at(s1, y0, 0.0f), F.at(s1, y0, D), F.at(s1, y1, D), F.at(s1, y1, 0.0f), -F.t, v(0, y0), v(D, y0), v(D, y1), v(0, y1));
        mesh.quad(m, F.at(s0, y1, 0.0f), F.at(s1, y1, 0.0f), F.at(s1, y1, D), F.at(s0, y1, D), {0, -1, 0}, v(0, s0), v(0, s1), v(D, s1),
                  v(D, s0));
        if (bottom)
            mesh.quad(m, F.at(s0, y0, 0.0f), F.at(s1, y0, 0.0f), F.at(s1, y0, D), F.at(s0, y0, D), {0, 1, 0}, v(0, s0), v(0, s1), v(D, s1),
                      v(D, s0));
    }

    // A horizontal band along the facade projecting `out` (front, top, bottom faces).
    void band(const std::string& m, const Face& F, float s0, float s1, float y0, float y1, float out)
    {
        const float tile = mat(m).tile;
        const float e = out; // reach round convex corners
        s0 -= e;
        s1 += e;
        auto uv = [&](float s, float y) { return glm::vec2(s / tile, -y / tile); };
        mesh.quad(m, F.at(s0, y0, -out), F.at(s1, y0, -out), F.at(s1, y1, -out), F.at(s0, y1, -out), F.n, uv(s0, y0), uv(s1, y0), uv(s1, y1),
                  uv(s0, y1));
        mesh.quad(m, F.at(s0, y1, 0.0f), F.at(s1, y1, 0.0f), F.at(s1, y1, -out), F.at(s0, y1, -out), {0, 1, 0}, uv(s0, 0), uv(s1, 0),
                  uv(s1, out), uv(s0, out));
        mesh.quad(m, F.at(s0, y0, 0.0f), F.at(s1, y0, 0.0f), F.at(s1, y0, -out), F.at(s0, y0, -out), {0, -1, 0}, uv(s0, 0), uv(s1, 0),
                  uv(s1, out), uv(s0, out));
    }

    void grime(const Face& F, float s0, float s1, float yTop, float yBot, int region, int k = 0)
    {
        if (yBot >= yTop || s1 <= s0 || lod_ > 0)
            return;
        float u0, u1, v0, v1;
        if (region == 0) {
            u0 = static_cast<float>(k) / 4.0f;
            u1 = static_cast<float>(k + 1) / 4.0f;
            v0 = 0.0f;
            v1 = 0.5f;
        } else if (region == 1) {
            u0 = 0.0f, u1 = 0.5f, v0 = 0.5f, v1 = 1.0f;
        } else {
            u0 = 0.5f, u1 = 1.0f, v0 = 0.5f, v1 = 1.0f;
        }
        mesh.quad("grime", F.at(s0, yBot, -0.012f), F.at(s1, yBot, -0.012f), F.at(s1, yTop, -0.012f), F.at(s0, yTop, -0.012f), F.n, {u0, v1},
                  {u1, v1}, {u1, v0}, {u0, v0});
    }

    struct Opening {
        float s0, s1, y0, y1;
        enum Kind { Window, Door, Shop, Loggia, BalconyDoor } kind;
        int floor;
    };

    // The state of a window pane, by the rules of decades of nobody.
    void filling(const Face& F, const Opening& o)
    {
        const float D = open_ ? kWall : kReveal;
        const std::string frame = st_.role == "panel" ? "st_metal_paint" : "wood";
        if (lod_ > 0) {
            mesh.quad("interior_dark", F.at(o.s0, o.y0, -0.02f), F.at(o.s1, o.y0, -0.02f), F.at(o.s1, o.y1, -0.02f), F.at(o.s0, o.y1, -0.02f), F.n,
                      {0, 0}, {1, 0}, {1, 1}, {0, 1});
            return;
        }
        reveal(st_.wall, F, o.s0, o.s1, o.y0, o.y1, D, o.kind != Opening::Door);
        if (!open_) // the dark room behind
            mesh.quad("interior_dark", F.at(o.s0, o.y0, D), F.at(o.s1, o.y0, D), F.at(o.s1, o.y1, D), F.at(o.s0, o.y1, D), F.n, {0, 0}, {1, 0},
                      {1, 1}, {0, 1});
        if (o.kind == Opening::Door) {
            if (b_.interior) // the door was taken long ago: the way in
                return;
            mesh.quad("st_metal_paint", F.at(o.s0, o.y0, 0.16f), F.at(o.s1, o.y0, 0.16f), F.at(o.s1, o.y1, 0.16f), F.at(o.s0, o.y1, 0.16f), F.n,
                      {0, 0}, {1, 0}, {1, -1.2f}, {0, -1.2f});
            return;
        }
        const float r = rng_.uniform();
        enum { Glass, Broken, Boarded, Empty } state = r < 0.14f ? Glass : r < 0.58f ? Broken : (o.floor == 0 && r < 0.9f) ? Boarded : Empty;
        if (b_.ruin && state == Glass)
            state = Empty;
        const float fw = 0.07f, fd = 0.12f;
        if (state != Empty && o.kind != Opening::Shop) {
            const float a = o.s0, b = o.s1, y0 = o.y0, y1 = o.y1;
            auto fq = [&](float s0, float s1, float ya, float yb) {
                mesh.quad(frame, F.at(s0, ya, fd), F.at(s1, ya, fd), F.at(s1, yb, fd), F.at(s0, yb, fd), F.n, {s0, -ya}, {s1, -ya}, {s1, -yb},
                          {s0, -yb});
            };
            fq(a, a + fw, y0, y1);
            fq(b - fw, b, y0, y1);
            fq(a + fw, b - fw, y1 - fw, y1);
            fq(a + fw, b - fw, y0, y0 + fw);
            const float mid = (a + b) * 0.5f;
            if (b - a > 1.0f && o.kind == Opening::Window)
                fq(mid - 0.03f, mid + 0.03f, y0 + fw, y1 - fw);
            const float ty = y0 + (y1 - y0) * 0.72f;
            fq(a + fw, b - fw, ty - 0.03f, ty + 0.03f);
        }
        if (state == Glass)
            mesh.quad("glass", F.at(o.s0, o.y0, fd + 0.01f), F.at(o.s1, o.y0, fd + 0.01f), F.at(o.s1, o.y1, fd + 0.01f), F.at(o.s0, o.y1, fd + 0.01f),
                      F.n, {0, 0}, {1, 0}, {1, 1}, {0, 1});
        else if (state == Boarded) {
            const int nb = std::max(2, static_cast<int>((o.y1 - o.y0) / 0.24f));
            for (int i = 0; i < nb; ++i) {
                if (rng_.chance(0.15f))
                    continue;
                const float y = o.y0 + 0.04f + static_cast<float>(i) * (o.y1 - o.y0 - 0.08f) / static_cast<float>(nb);
                mesh.quad("wood", F.at(o.s0 - 0.08f, y, -0.03f), F.at(o.s1 + 0.08f, y, -0.03f), F.at(o.s1 + 0.08f, y + 0.18f, -0.03f),
                          F.at(o.s0 - 0.08f, y + 0.18f, -0.03f), F.n, {o.s0 / 1.5f, -y / 1.5f}, {o.s1 / 1.5f, -y / 1.5f},
                          {o.s1 / 1.5f, -(y + 0.18f) / 1.5f}, {o.s0 / 1.5f, -(y + 0.18f) / 1.5f});
            }
        }
        if (o.kind == Opening::Window) {
            // tin sill, and the rain streak it sheds
            const float a = o.s0 - 0.04f, b = o.s1 + 0.04f, y = o.y0;
            mesh.quad("rust", F.at(a, y, -0.07f), F.at(b, y, -0.07f), F.at(b, y, 0.0f), F.at(a, y, 0.0f), {0, 1, 0}, {a, 0}, {b, 0}, {b, 0.05f},
                      {a, 0.05f});
            mesh.quad("rust", F.at(a, y - 0.04f, -0.07f), F.at(b, y - 0.04f, -0.07f), F.at(b, y, -0.07f), F.at(a, y, -0.07f), F.n, {a, 0}, {b, 0},
                      {b, 0.05f}, {a, 0.05f});
            if (rng_.chance(0.8f)) {
                const float w = (o.s1 - o.s0) * rng_.range(0.8f, 1.15f), c = (o.s0 + o.s1) * 0.5f;
                grime(F, c - w * 0.5f, c + w * 0.5f, y - 0.04f, std::max(0.05f, y - rng_.range(0.7f, 2.4f)), 0, rng_.below(4));
            }
        }
    }

    void loggia(const Face& F, const Opening& o)
    {
        const float D = 1.2f;
        if (lod_ > 0) {
            filling(F, o);
            return;
        }
        reveal(st_.wall, F, o.s0, o.s1, o.y0, o.y1, D);
        if (open_) { // seen from the room behind: the loggia is a box jutting in
            const float tl = 2.5f;
            mesh.quad("plaster", F.at(o.s0, o.y0, D + 0.01f), F.at(o.s1, o.y0, D + 0.01f), F.at(o.s1, o.y1, D + 0.01f), F.at(o.s0, o.y1, D + 0.01f), -F.n,
                      {o.s0 / tl, 0}, {o.s1 / tl, 0}, {o.s1 / tl, -1}, {o.s0 / tl, -1});
            for (const float s : {o.s0, o.s1})
                mesh.quad("plaster", F.at(s, o.y0, kWall), F.at(s, o.y0, D), F.at(s, o.y1, D), F.at(s, o.y1, kWall), s == o.s0 ? F.t : -F.t,
                          {0, 0}, {D / tl, 0}, {D / tl, -1}, {0, -1});
            mesh.quad("plaster", F.at(o.s0, o.y1, kWall), F.at(o.s1, o.y1, kWall), F.at(o.s1, o.y1, D), F.at(o.s0, o.y1, D), {0, 1, 0}, {0, 0},
                      {1, 0}, {1, 0.5f}, {0, 0.5f});
        }
        mesh.quad("plaster", F.at(o.s0, o.y0, D), F.at(o.s1, o.y0, D), F.at(o.s1, o.y1, D), F.at(o.s0, o.y1, D), F.n, {o.s0 / 2.5f, 0},
                  {o.s1 / 2.5f, 0}, {o.s1 / 2.5f, -1}, {o.s0 / 2.5f, -1});
        const float w = o.s1 - o.s0;
        auto dark = [&](float s0, float s1, float ya, float yb) {
            mesh.quad("interior_dark", F.at(s0, ya, D - 0.02f), F.at(s1, ya, D - 0.02f), F.at(s1, yb, D - 0.02f), F.at(s0, yb, D - 0.02f), F.n,
                      {0, 0}, {1, 0}, {1, 1}, {0, 1});
        };
        dark(o.s0 + 0.35f, o.s0 + std::min(1.15f, w * 0.4f), o.y0 + 0.02f, o.y0 + 2.15f);              // door
        dark(o.s0 + std::min(1.45f, w * 0.5f), o.s1 - 0.35f, o.y0 + 0.85f, o.y0 + 2.15f);              // window
        if (rng_.chance(0.9f)) { // front parapet panel (some fell long ago)
            const float y1 = o.y0 + 1.05f, d0 = 0.03f, d1 = 0.15f;
            const float tile = mat(st_.wall).tile;
            mesh.quad(st_.wall, F.at(o.s0, o.y0, d0), F.at(o.s1, o.y0, d0), F.at(o.s1, y1, d0), F.at(o.s0, y1, d0), F.n, {o.s0 / tile, 0},
                      {o.s1 / tile, 0}, {o.s1 / tile, -1.05f / tile}, {o.s0 / tile, -1.05f / tile});
            mesh.quad(st_.wall, F.at(o.s0, y1, d0), F.at(o.s1, y1, d0), F.at(o.s1, y1, d1), F.at(o.s0, y1, d1), {0, 1, 0}, {0, 0}, {1, 0},
                      {1, 0.05f}, {0, 0.05f});
            mesh.quad(st_.wall, F.at(o.s0, o.y0, d1), F.at(o.s1, o.y0, d1), F.at(o.s1, y1, d1), F.at(o.s0, y1, d1), -F.n, {0, 0}, {1, 0},
                      {1, 0.5f}, {0, 0.5f});
            grime(F, o.s0, o.s1, o.y0, std::max(0.1f, o.y0 - rng_.range(0.8f, 2.2f)), 0, rng_.below(4));
        }
    }

    void balcony(const Face& F, float s0, float s1, float y)
    {
        if (lod_ > 0)
            return;
        const float out = 1.0f, th = 0.18f;
        const std::string m = "concrete";
        const float a = s0 - 0.3f, b = s1 + 0.3f;
        balconies_.push_back({F.at((a + b) * 0.5f, y, -out * 0.5f), F.n, b - a});
        mesh.quad(m, F.at(a, y, 0), F.at(b, y, 0), F.at(b, y, -out), F.at(a, y, -out), {0, 1, 0}, {a / 3, 0}, {b / 3, 0}, {b / 3, out / 3},
                  {a / 3, out / 3});
        mesh.quad(m, F.at(a, y - th, 0), F.at(b, y - th, 0), F.at(b, y - th, -out), F.at(a, y - th, -out), {0, -1, 0}, {a / 3, 0}, {b / 3, 0},
                  {b / 3, out / 3}, {a / 3, out / 3});
        mesh.quad(m, F.at(a, y - th, -out), F.at(b, y - th, -out), F.at(b, y, -out), F.at(a, y, -out), F.n, {a / 3, 0}, {b / 3, 0},
                  {b / 3, th / 3}, {a / 3, th / 3});
        for (const float s : {a, b}) {
            const glm::vec3 side = s == a ? -F.t : F.t;
            mesh.quad(m, F.at(s, y - th, 0), F.at(s, y - th, -out), F.at(s, y, -out), F.at(s, y, 0), side, {0, 0}, {out / 3, 0},
                      {out / 3, th / 3}, {0, th / 3});
        }
        if (rng_.chance(0.2f))
            return; // the railing rusted away
        // railing: top rail, bottom rail, posts (thin plates facing out and in)
        auto plate = [&](glm::vec3 p0, glm::vec3 p1, float h) {
            const glm::vec3 up{0.0f, h, 0.0f};
            mesh.quad("rust", p0, p1, p1 + up, p0 + up, F.n, {0, 0}, {1, 0}, {1, h}, {0, h});
            mesh.quad("rust", p0, p1, p1 + up, p0 + up, -F.n, {0, 0}, {1, 0}, {1, h}, {0, h});
        };
        plate(F.at(a, y + 0.95f, -out + 0.03f), F.at(b, y + 0.95f, -out + 0.03f), 0.05f);
        plate(F.at(a, y + 0.1f, -out + 0.03f), F.at(b, y + 0.1f, -out + 0.03f), 0.04f);
        for (float s = a + 0.05f; s < b; s += 0.14f)
            plate(F.at(s, y, -out + 0.03f), F.at(s + 0.018f, y, -out + 0.03f), 1.0f);
    }

    void facade(int i)
    {
        const Poly& fp = b_.footprint;
        const std::size_t n = fp.size();
        const glm::vec2 a = fp[static_cast<std::size_t>(i)], b = fp[(static_cast<std::size_t>(i) + 1) % n];
        const float len = glm::length(b - a);
        if (len < 0.2f)
            return;
        const glm::vec2 t2 = (b - a) / len;
        Face F;
        F.o = {a.x, 0.0f, a.y};
        F.t = {t2.x, 0.0f, t2.y};
        F.n = {t2.y, 0.0f, -t2.x}; // right of the edge = outside for a counter-clockwise (x, z) polygon
        F.len = len;
        const EdgeRole role = static_cast<std::size_t>(i) < b_.edges.size() ? b_.edges[static_cast<std::size_t>(i)] : EdgeRole::Street;
        const int FL = std::max(1, b_.floors);
        const float parapet = st_.pitched ? 0.0f : 0.9f;

        // Party walls: blank fire walls (seen above a lower neighbour).
        if (role == EdgeRole::Party || len < st_.bay * 0.8f) {
            if (b_.ruin) {
                const int nb = std::max(1, static_cast<int>(len / 3.0f));
                for (int j = 0; j < nb; ++j) {
                    const float s0 = len * static_cast<float>(j) / static_cast<float>(nb), s1 = len * static_cast<float>(j + 1) / static_cast<float>(nb);
                    const int f = floorsAt(a + t2 * ((s0 + s1) * 0.5f));
                    wall(st_.firewall, F, s0, s1, -0.6f, floorY(f));
                    capTop(F, s0, s1, floorY(f));
                }
            } else {
                wall(role == EdgeRole::Party ? st_.firewall : st_.wall, F, 0.0f, len, -0.6f, top_ + parapet);
                if (role != EdgeRole::Party)
                    facadeTrim(F, len, {});
            }
            return;
        }

        // split(x): piers at the corners, window axes fitted to the facade length.
        const float pier = 0.7f;
        const int nb = std::max(1, static_cast<int>(std::lround((len - 2.0f * pier) / st_.bay)));
        const float bay = (len - 2.0f * pier) / static_cast<float>(nb);
        std::vector<int> colFloors(static_cast<std::size_t>(nb));
        for (int j = 0; j < nb; ++j)
            colFloors[static_cast<std::size_t>(j)] = floorsAt(a + t2 * (pier + bay * (static_cast<float>(j) + 0.5f)));
        // Column rules: loggia stacks (panels), balcony columns (tenements), the entrance.
        std::vector<char> loggiaCol(static_cast<std::size_t>(nb), 0), balconyCol(static_cast<std::size_t>(nb), 0);
        if (loggiaCols_ > 0.0f && nb >= 4)
            for (int j = 1; j < nb - 1; ++j)
                if (rng_.chance(loggiaCols_) && (j < 1 || !loggiaCol[static_cast<std::size_t>(j - 1)]))
                    loggiaCol[static_cast<std::size_t>(j)] = 1;
        if (st_.balconies > 0.0f && nb >= 3)
            for (int j = 1; j < nb - 1; ++j)
                if (!loggiaCol[static_cast<std::size_t>(j)] && rng_.chance(st_.balconies))
                    balconyCol[static_cast<std::size_t>(j)] = 1;
        const int entrance = role == EdgeRole::Street ? nb / 2 : (role == EdgeRole::Yard && st_.role != "tenement" ? -1 : nb / 2);
        const bool arcade = st_.arcade > 0.0f && role == EdgeRole::Street && !b_.ruin && FL >= 3 && nb >= 3 && rng_.chance(st_.arcade);
        const bool shop = (role == EdgeRole::Street && rng_.chance(st_.shops)) || arcade;

        std::vector<Opening> ops;
        for (int f = 0; f < FL; ++f) {
            const float y0 = floorY(f);
            for (int j = 0; j < nb; ++j) {
                if (f >= colFloors[static_cast<std::size_t>(j)])
                    continue;
                const float c = pier + bay * (static_cast<float>(j) + 0.5f);
                const float w = std::min(st_.windowWidth, bay - 0.5f);
                if (f == 0) {
                    if (j == entrance)
                        ops.push_back({c - 0.75f, c + 0.75f, 0.0f, 2.4f, Opening::Door, 0});
                    else if (shop)
                        ops.push_back({c - (bay - 0.6f) * 0.5f, c + (bay - 0.6f) * 0.5f, 0.45f, std::min(st_.groundStorey - 0.6f, 3.0f), Opening::Shop, 0});
                    else
                        ops.push_back({c - w * 0.5f, c + w * 0.5f, std::max(1.0f, st_.groundStorey - 0.6f - st_.windowHeight),
                                       st_.groundStorey - 0.6f, Opening::Window, 0});
                    continue;
                }
                if (loggiaCol[static_cast<std::size_t>(j)])
                    ops.push_back({c - (bay - 0.35f) * 0.5f, c + (bay - 0.35f) * 0.5f, y0 + 0.02f, y0 + std::min(2.6f, st_.storey - 0.25f), Opening::Loggia, f});
                else if (balconyCol[static_cast<std::size_t>(j)] && f < FL - 1)
                    ops.push_back({c - 0.5f, c + 0.5f, y0 + 0.05f, y0 + st_.sill + st_.windowHeight, Opening::BalconyDoor, f});
                else
                    ops.push_back({c - w * 0.5f, c + w * 0.5f, y0 + st_.sill, y0 + st_.sill + st_.windowHeight, Opening::Window, f});
            }
        }
        windows += static_cast<int>(ops.size());

        // Walls: per storey band, around the openings (columns between, pieces above and below).
        if (lod_ > 0 && !b_.ruin) {
            wall(st_.wall, F, 0.0f, len, -0.6f, top_ + parapet);
        } else {
            for (int f = 0; f < FL; ++f) {
                const float yb0 = f == 0 ? -0.6f : floorY(f), yb1 = floorY(f + 1);
                std::vector<const Opening*> band;
                for (const auto& o : ops)
                    if (o.floor == f)
                        band.push_back(&o);
                std::sort(band.begin(), band.end(), [](const Opening* x, const Opening* y) { return x->s0 < y->s0; });
                // which bays still stand at this storey (ruins)
                auto standsAt = [&](float s) {
                    const int j = std::clamp(static_cast<int>((s - pier) / bay), 0, nb - 1);
                    return f < colFloors[static_cast<std::size_t>(j)];
                };
                float s = 0.0f;
                auto run = [&](float s0, float s1) { // solid wall run, split where a ruin column ends
                    if (!b_.ruin) {
                        wall(st_.wall, F, s0, s1, yb0, yb1);
                        return;
                    }
                    const float step = 0.5f;
                    for (float x = s0; x < s1 - 1e-3f; x += step) {
                        const float x1 = std::min(s1, x + step);
                        if (standsAt((x + x1) * 0.5f)) {
                            wall(st_.wall, F, x, x1, yb0, yb1);
                            if (f + 1 >= colFloors[static_cast<std::size_t>(std::clamp(static_cast<int>(((x + x1) * 0.5f - pier) / bay), 0, nb - 1))])
                                capTop(F, x, x1, yb1);
                        }
                    }
                };
                for (const Opening* o : band) {
                    run(s, o->s0);
                    wall(st_.wall, F, o->s0, o->s1, yb0, o->y0);
                    wall(st_.wall, F, o->s0, o->s1, o->y1, yb1);
                    if (b_.ruin && f + 1 >= colFloors[static_cast<std::size_t>(std::clamp(static_cast<int>(((o->s0 + o->s1) * 0.5f - pier) / bay), 0, nb - 1))])
                        capTop(F, o->s0, o->s1, yb1);
                    s = o->s1;
                }
                run(s, len);
            }
            if (!b_.ruin && parapet > 0.0f)
                wall(st_.wall, F, 0.0f, len, top_, top_ + parapet);
        }
        for (const auto& o : ops) {
            if (o.kind == Opening::Loggia)
                loggia(F, o);
            else
                filling(F, o);
            if (o.kind == Opening::BalconyDoor)
                balcony(F, o.s0, o.s1, o.y0 - 0.03f);
        }
        if (shop && lod_ == 0)
            shopSigns(F, ops, i);
        if (arcade)
            colonnade(F, len, pier, bay, nb);
        else if (st_.fireEscapes > 0.0f && !b_.ruin && FL >= 4 && nb >= 4 && rng_.chance(st_.fireEscapes))
            fireEscape(F, pier, bay, nb, FL, entrance);
        // Entrance canopy.
        if (entrance >= 0 && lod_ == 0 && !arcade) {
            const float c = pier + bay * (static_cast<float>(entrance) + 0.5f);
            band("concrete", F, c - 1.4f, c + 1.4f, 2.6f, 2.8f, 1.4f);
        }
        if (!b_.ruin)
            facadeTrim(F, len, ops);
    }

    // A box in facade coordinates: s0..s1 along, y0..y1 up, d0..d1 into the wall (negative = out).
    void fbox(const std::string& m, const Face& F, float s0, float s1, float y0, float y1, float d0, float d1)
    {
        const float tl = mat(m).tile;
        auto uv = [&](float x, float y) { return glm::vec2(x / tl, -y / tl); };
        mesh.quad(m, F.at(s0, y0, d0), F.at(s1, y0, d0), F.at(s1, y1, d0), F.at(s0, y1, d0), F.n, uv(s0, y0), uv(s1, y0), uv(s1, y1), uv(s0, y1));
        mesh.quad(m, F.at(s0, y0, d1), F.at(s1, y0, d1), F.at(s1, y1, d1), F.at(s0, y1, d1), -F.n, uv(s0, y0), uv(s1, y0), uv(s1, y1), uv(s0, y1));
        mesh.quad(m, F.at(s0, y0, d0), F.at(s0, y0, d1), F.at(s0, y1, d1), F.at(s0, y1, d0), -F.t, uv(d0, y0), uv(d1, y0), uv(d1, y1), uv(d0, y1));
        mesh.quad(m, F.at(s1, y0, d0), F.at(s1, y0, d1), F.at(s1, y1, d1), F.at(s1, y1, d0), F.t, uv(d0, y0), uv(d1, y0), uv(d1, y1), uv(d0, y1));
        mesh.quad(m, F.at(s0, y1, d0), F.at(s1, y1, d0), F.at(s1, y1, d1), F.at(s0, y1, d1), {0, 1, 0}, uv(s0, d0), uv(s1, d0), uv(s1, d1), uv(s0, d1));
        mesh.quad(m, F.at(s0, y0, d0), F.at(s1, y0, d0), F.at(s1, y0, d1), F.at(s0, y0, d1), {0, -1, 0}, uv(s0, d0), uv(s1, d0), uv(s1, d1), uv(s0, d1));
    }

    // Painted fascia boards over a run of shop windows: one board per shop, a few gone.
    void shopSigns(const Face& F, const std::vector<Opening>& ops, int edge)
    {
        float s0 = 1e9f, s1 = -1e9f, top = 0.0f;
        for (const auto& o : ops)
            if (o.kind == Opening::Shop) {
                s0 = std::min(s0, o.s0);
                s1 = std::max(s1, o.s1);
                top = std::max(top, o.y1);
            }
        if (s1 <= s0)
            return;
        const float y0 = top + 0.12f, y1 = std::min(y0 + 0.65f, st_.groundStorey - 0.1f);
        if (y1 - y0 < 0.3f)
            return;
        Rng sr((static_cast<std::uint64_t>(b_.seed) << 20) ^ (0x5160u + static_cast<std::uint64_t>(edge) * 7919u));
        const float Lb = std::clamp((y1 - y0) * 4.2f, 2.2f, 5.5f);
        const int n = std::max(1, static_cast<int>((s1 - s0 + 0.4f) / (Lb + 0.6f)));
        const float step = (s1 - s0) / static_cast<float>(n);
        for (int k = 0; k < n; ++k) {
            if (sr.chance(0.2f))
                continue; // fell off long ago
            const float c = s0 + step * (static_cast<float>(k) + 0.5f), a = c - Lb * 0.5f, b = c + Lb * 0.5f;
            fbox("rust_dark", F, a - 0.04f, b + 0.04f, y0 - 0.04f, y1 + 0.04f, -0.05f, 0.0f);
            const int sign = sr.below(16), col = sign % 2, row = sign / 2;
            const float u0 = static_cast<float>(col) * 0.5f, u1 = u0 + 0.5f, v0 = static_cast<float>(row) / 8.0f, v1 = v0 + 1.0f / 8.0f;
            // tilted a little: one fixing gave way
            const float sag = sr.chance(0.25f) ? sr.range(-0.12f, 0.12f) : 0.0f;
            mesh.quad("shop_signs", F.at(a, y0 + std::max(0.0f, sag), -0.06f), F.at(b, y0 + std::max(0.0f, -sag), -0.06f), F.at(b, y1 + std::max(0.0f, -sag), -0.06f),
                      F.at(a, y1 + std::max(0.0f, sag), -0.06f), F.n, {u1, v1}, {u0, v1}, {u0, v0}, {u1, v0});
        }
    }

    // A New York fire escape over two window columns: a grated platform at every storey above
    // the ground floor, railings, a stair zig-zagging between them, the drop ladder at the bottom
    // pulled up. Rust everywhere; now and then a platform has torn off one end and hangs.
    void fireEscape(const Face& F, float pier, float bay, int nb, int FL, int entrance)
    {
        int j0 = 1 + rng_.below(std::max(1, nb - 3));
        if (j0 == entrance || j0 + 1 == entrance)
            j0 = entrance + 1 < nb - 1 ? entrance + 1 : std::max(0, entrance - 2);
        const float s0 = pier + bay * static_cast<float>(j0) + 0.15f, s1 = pier + bay * static_cast<float>(j0 + 2) - 0.15f;
        const float D = 1.2f, m = 0.04f;
        const std::string iron = "rust_dark";
        for (int f = 1; f < FL; ++f) {
            const float y = floorY(f) - 0.05f;
            if (lod_ > 0) {
                fbox(iron, F, s0, s1, y - 0.06f, y, -D, 0.0f);
                continue;
            }
            const bool torn = f > 1 && rng_.chance(0.06f);
            if (torn) { // hangs from one end
                bar(mesh, iron, F.at(s0, y, -D * 0.5f), F.at(s1, y - 1.8f, -D * 0.5f - 0.3f), D, 0.05f);
                continue;
            }
            fbox(iron, F, s0, s1, y - 0.05f, y, -D, 0.0f);                // the grating
            bar(mesh, iron, F.at(s0, y + 1.0f, -D), F.at(s1, y + 1.0f, -D), m, m); // top rail
            bar(mesh, iron, F.at(s0, y + 0.5f, -D), F.at(s1, y + 0.5f, -D), m * 0.7f, m * 0.7f);
            for (const float s : {s0, s1}) {
                bar(mesh, iron, F.at(s, y + 1.0f, -D), F.at(s, y + 1.0f, 0.0f), m, m);
                bar(mesh, iron, F.at(s, y, 0.0f - 0.02f), F.at(s, y + 1.0f, -0.02f), m, m);
            }
            for (float s = s0; s <= s1 + 0.01f; s += (s1 - s0) / 5.0f)
                bar(mesh, iron, F.at(s, y, -D + 0.02f), F.at(s, y + 1.0f, -D + 0.02f), m * 0.6f, m * 0.6f);
            bar(mesh, iron, F.at(s0, y - 0.1f, -D + 0.1f), F.at(s0, y - 0.9f, -0.02f), m, m); // brackets into the wall
            bar(mesh, iron, F.at(s1, y - 0.1f, -D + 0.1f), F.at(s1, y - 0.9f, -0.02f), m, m);
            // The stair up to the next platform, alternating ends; stringers and treads.
            if (f + 1 < FL) {
                const bool right = (f % 2) == 0;
                const float sa = right ? s0 + 0.3f : s1 - 0.3f, sb = right ? s1 - 1.0f : s0 + 1.0f;
                const float ya = y, yb = floorY(f + 1) - 0.05f;
                for (const float d : {-D + 0.15f, -D + 0.75f})
                    bar(mesh, iron, F.at(sa, ya, d), F.at(sb, yb, d), 0.05f, 0.12f);
                const int steps = std::max(4, static_cast<int>((yb - ya) / 0.24f));
                for (int k = 1; k < steps; ++k) {
                    const float t = static_cast<float>(k) / static_cast<float>(steps);
                    const float s = sa + (sb - sa) * t, yy = ya + (yb - ya) * t;
                    bar(mesh, iron, F.at(s, yy, -D + 0.15f), F.at(s, yy, -D + 0.75f), 0.18f, 0.03f);
                }
            }
        }
        // The drop ladder at the first platform: on most houses pulled up (bottom 2.6 m above the
        // pavement), on some let down to the ground long ago, so the player can climb up (ladderSpots).
        // Its own random stream: the rest of the house stays as it was.
        if (lod_ == 0) {
            // At the end the first stair leaves free (it climbs from the far end towards this one).
            const float y = floorY(1) - 0.05f, s = s0 + 0.55f;
            Rng drop(b_.seed ^ (0xD70Full + static_cast<std::uint64_t>(j0) * 0x9E37ull + static_cast<std::uint64_t>(std::lround(F.o.x * 7.0f + F.o.z * 13.0f))));
            const bool down = drop.chance(0.45f);
            const float bottom = down ? 0.0f : std::max(2.6f, y - 1.8f);
            for (const float o : {-0.22f, 0.22f})
                bar(mesh, iron, F.at(s + o, y + 0.9f, -D + 0.08f), F.at(s + o, bottom, -D + 0.08f), 0.04f, 0.04f);
            for (float yy = bottom + (down ? 0.3f : 0.1f); yy < y + 0.9f; yy += 0.3f)
                bar(mesh, iron, F.at(s - 0.22f, yy, -D + 0.08f), F.at(s + 0.22f, yy, -D + 0.08f), 0.03f, 0.03f);
            if (down) // the climbing box just outside the railing, up to over its top rail
                ladders_.push_back({F.at(s, 0.0f, -D - 0.05f), F.n, y + 1.07f});
        }
    }

    // Podloubi: square columns on the bay lines 2.2 m out (over the pavement), a slab over the walk at the ground
    // storey with a deep fascia; moss on top, a column or two gone.
    void colonnade(const Face& F, float len, float pier, float bay, int nb)
    {
        const float A = 2.2f, y = st_.groundStorey, w = 0.22f;
        const int step = bay < 3.0f ? 2 : 1;
        for (int j = 0; j <= nb; j += step) {
            const float s = j == 0 ? pier * 0.5f + w : j == nb ? len - pier * 0.5f - w : pier + bay * static_cast<float>(j);
            if (j != 0 && j != nb && lod_ == 0 && rng_.chance(0.08f))
                continue; // knocked out (the slab spans on)
            fbox("concrete", F, s - w, s + w, -0.6f, y - 0.35f, -A, -A + 2.0f * w);
        }
        fbox("concrete", F, 0.0f, len, y - 0.35f, y - 0.05f, -A - 0.05f, 0.0f);
        fbox("concrete", F, 0.0f, len, y - 0.75f, y - 0.35f, -A - 0.05f, -A + 0.25f); // the beam over the columns
        if (lod_ == 0)
            mesh.quad("concrete_moss", F.at(0.0f, y - 0.04f, -A - 0.05f), F.at(len, y - 0.04f, -A - 0.05f), F.at(len, y - 0.04f, 0.0f),
                      F.at(0.0f, y - 0.04f, 0.0f), {0, 1, 0}, {0, 0}, {len / 4.0f, 0}, {len / 4.0f, A / 4.0f}, {0, A / 4.0f});
        balconies_.push_back({F.at(len * 0.5f, y - 0.05f, -A * 0.5f), F.n, len}); // its top is a long planter
    }

    // Plinth, string course, cornice and the roof drips.
    void facadeTrim(const Face& F, float len, const std::vector<Opening>& ops)
    {
        // Plinth: split round doors and shop windows that reach down to it.
        float s = 0.0f;
        std::vector<const Opening*> low;
        for (const auto& o : ops)
            if (o.floor == 0 && o.y0 < 0.6f)
                low.push_back(&o);
        std::sort(low.begin(), low.end(), [](const Opening* x, const Opening* y) { return x->s0 < y->s0; });
        auto plinth = [&](float s0, float s1, float y1) {
            if (s1 - s0 < 0.05f)
                return;
            const float tile = mat(st_.plinth).tile;
            mesh.quad(st_.plinth, F.at(s0, -0.6f, -0.05f), F.at(s1, -0.6f, -0.05f), F.at(s1, y1, -0.05f), F.at(s0, y1, -0.05f), F.n,
                      {s0 / tile, 0.6f / tile}, {s1 / tile, 0.6f / tile}, {s1 / tile, -y1 / tile}, {s0 / tile, -y1 / tile});
            mesh.quad(st_.plinth, F.at(s0, y1, 0.0f), F.at(s1, y1, 0.0f), F.at(s1, y1, -0.05f), F.at(s0, y1, -0.05f), {0, 1, 0}, {0, 0}, {1, 0},
                      {1, 0.02f}, {0, 0.02f});
        };
        for (const Opening* o : low) {
            plinth(s, o->s0, 0.6f);
            plinth(o->s0, o->s1, std::max(-0.55f, o->y0 - 0.02f));
            s = o->s1;
        }
        plinth(s, len, 0.6f);
        if (lod_ == 0) {
            grime(F, 0.2f, len - 0.2f, 1.2f, 0.0f, 2);
            const float roofEdge = top_ - (st_.cornice > 0.0f ? 0.6f : 0.0f) + (st_.pitched ? 0.0f : 0.85f);
            for (float x = 0.2f; x < len - 0.7f;) {
                const float w = std::min(rng_.range(1.6f, 3.4f), len - 0.2f - x);
                if (rng_.chance(0.7f))
                    grime(F, x, x + w, roofEdge, roofEdge - rng_.range(1.0f, 3.2f), 1);
                x += w;
            }
        }
        if (st_.bands > 0.0f && b_.floors > 1)
            band(st_.trim, F, 0.0f, len, floorY(1) - 0.3f, floorY(1) - 0.05f, 0.08f);
        if (st_.cornice > 0.0f)
            band(st_.trim, F, 0.0f, len, top_ - 0.6f, top_, st_.cornice);
    }

    void capTop(const Face& F, float s0, float s1, float y)
    {
        mesh.quad("concrete", F.at(s0, y, 0.0f), F.at(s1, y, 0.0f), F.at(s1, y, kWall), F.at(s0, y, kWall), {0, 1, 0}, {s0 / 3, 0}, {s1 / 3, 0},
                  {s1 / 3, kWall / 3}, {s0 / 3, kWall / 3});
    }

    void roof()
    {
        if (b_.ruin)
            return;
        const Poly& fp = b_.footprint;
        const std::size_t n = fp.size();
        if (st_.pitched) {
            // Hipped sheet roof: slopes up to the footprint inset by r, flat cap over it.
            float minW = 1e9f;
            glm::vec2 c, ax, half;
            minAreaRect(fp, c, ax, half);
            minW = std::min(half.x, half.y);
            const float r = std::clamp(minW * 0.75f, 1.0f, 4.5f);
            Poly in;
            if (mitreInset(fp, std::vector<float>(n, r), in)) {
                const float rise = r * 0.62f;
                const std::string& m = st_.roof;
                const float tile = mat(m).tile;
                for (std::size_t i = 0; i < n; ++i) {
                    const std::size_t j = (i + 1) % n;
                    const glm::vec3 p0{fp[i].x, top_, fp[i].y}, p1{fp[j].x, top_, fp[j].y};
                    const glm::vec3 q0{in[i].x, top_ + rise, in[i].y}, q1{in[j].x, top_ + rise, in[j].y};
                    const glm::vec3 nrm = glm::normalize(glm::cross(p1 - p0, q0 - p0)) * 1.0f;
                    const glm::vec3 up = nrm.y < 0.0f ? -nrm : nrm;
                    const float L = glm::length(p1 - p0), slope = glm::length(q0 - (p0 + (p1 - p0) * glm::dot(q0 - p0, p1 - p0) / (L * L)));
                    const float sq0 = glm::dot(q0 - p0, (p1 - p0) / L), sq1 = glm::dot(q1 - p0, (p1 - p0) / L);
                    mesh.quad(m, p0, p1, q1, q0, up, {0, 0}, {L / tile, 0}, {sq1 / tile, slope / tile}, {sq0 / tile, slope / tile});
                    // the eaves: a short overhang under the sheet
                }
                mesh.flat(m, in, top_ + rise, true);
                if (lod_ == 0) { // a chimney or two
                    const int k = 1 + rng_.below(2);
                    for (int i = 0; i < k; ++i) {
                        const glm::vec2 p = centroid(in) + ax * rng_.range(-half.x * 0.5f, half.x * 0.5f);
                        box("brick", {p.x - 0.35f, top_ + rise * 0.5f, p.y - 0.35f}, {p.x + 0.35f, top_ + rise + 1.2f, p.y + 0.35f});
                    }
                }
                return;
            }
        }
        // Flat roof inside a parapet.
        mesh.flat(st_.roof, fp, top_ + 0.02f, true);
        if (lod_ == 0 || true) {
            const float par = 0.9f, d = 0.25f;
            for (std::size_t i = 0; i < n; ++i) {
                const glm::vec2 a = fp[i], b = fp[(i + 1) % n];
                const float len = glm::length(b - a);
                if (len < 0.1f)
                    continue;
                const glm::vec2 t2 = (b - a) / len;
                Face F;
                F.o = {a.x, 0.0f, a.y};
                F.t = {t2.x, 0.0f, t2.y};
                F.n = {t2.y, 0.0f, -t2.x};
                const float tile = mat(st_.wall).tile;
                mesh.quad(st_.wall, F.at(0, top_, d), F.at(len, top_, d), F.at(len, top_ + par, d), F.at(0, top_ + par, d), -F.n, {0, 0},
                          {len / tile, 0}, {len / tile, -par / tile}, {0, -par / tile});
                mesh.quad("concrete", F.at(-0.05f, top_ + par, -0.05f), F.at(len + 0.05f, top_ + par, -0.05f), F.at(len + 0.05f, top_ + par, d),
                          F.at(-0.05f, top_ + par, d), {0, 1, 0}, {0, 0}, {len / 3, 0}, {len / 3, 0.1f}, {0, 0.1f});
            }
        }
        if (st_.role == "panel" && b_.floors >= 9) { // lift machine room
            const glm::vec2 c = centroid(fp);
            box("concrete", {c.x - 3.0f, top_, c.y - 2.6f}, {c.x + 3.0f, top_ + 3.0f, c.y + 2.6f});
        }
    }

    void box(const std::string& m, glm::vec3 lo, glm::vec3 hi)
    {
        const float t = mat(m).tile;
        const glm::vec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                                {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
        auto q = [&](int a, int b, int cc, int d, glm::vec3 n, float w, float h) {
            mesh.quad(m, c[a], c[b], c[cc], c[d], n, {0, 0}, {w / t, 0}, {w / t, -h / t}, {0, -h / t});
        };
        const glm::vec3 s = hi - lo;
        q(0, 1, 2, 3, {0, 0, -1}, s.x, s.y);
        q(5, 4, 7, 6, {0, 0, 1}, s.x, s.y);
        q(4, 0, 3, 7, {-1, 0, 0}, s.z, s.y);
        q(1, 5, 6, 2, {1, 0, 0}, s.z, s.y);
        q(3, 2, 6, 7, {0, 1, 0}, s.x, s.z);
    }

    // ---- enterable houses
    // How far a ray from p (local x, z) along dir runs inside the footprint.
    float depthInside(glm::vec2 p, glm::vec2 dir) const
    {
        const Poly& fp = b_.footprint;
        float best = 1e9f;
        for (std::size_t i = 0; i < fp.size(); ++i) {
            const glm::vec2 a = fp[i], b = fp[(i + 1) % fp.size()], e = b - a;
            const float den = dir.x * e.y - dir.y * e.x;
            if (std::abs(den) < 1e-6f)
                continue;
            const glm::vec2 w = a - p;
            const float t = (w.x * e.y - w.y * e.x) / den, u = (w.x * dir.y - w.y * dir.x) / den;
            if (t > 0.05f && u >= 0.0f && u <= 1.0f)
                best = std::min(best, t);
        }
        return best;
    }

    // Gutted inside of an enterable house:
    //   Floors    -> slab per storey (concrete, underside), the stairwell cut out of it
    //   Stairwell -> behind the main street door: entrance landing, two flights a storey
    //                round a spine wall, half landings at the back, a landing per floor
    //                with doorways into the flats either side
    //   Flats     -> plaster partitions off the facades between window pairs, doorways at
    //                their inner end; everything else long carried off
    // Own random numbers (the facades stay as they are).
    void interior()
    {
        const Poly& fp = b_.footprint;
        const std::size_t n = fp.size();
        Poly in;
        std::vector<int> src;
        if (!insetPolygon(fp, std::vector<float>(n, kWall), in, &src) || in.size() < 3)
            return;
        Rng ir(b_.seed ^ 0x1E7E51ull);
        const int FL = std::max(1, b_.floors);
        const float pier = 0.7f;
        auto faceOf = [&](std::size_t i) {
            const glm::vec2 a = fp[i], b = fp[(i + 1) % n];
            const float len = glm::length(b - a);
            const glm::vec2 t2 = (b - a) / std::max(len, 1e-4f);
            Face F;
            F.o = {a.x, 0.0f, a.y};
            F.t = {t2.x, 0.0f, t2.y};
            F.n = {t2.y, 0.0f, -t2.x};
            F.len = len;
            return F;
        };
        auto at2 = [](const Face& F, float s, float d) { const glm::vec3 p = F.at(s, 0.0f, d); return glm::vec2(p.x, p.z); };
        auto inward = [](const Face& F) { return glm::vec2(-F.n.x, -F.n.z); };
        // The stairwell: behind the entrance of the longest street front deep enough for it.
        const float halfW = 1.3f, dLand = 1.6f, dRun = 4.1f, dBack = 5.4f, wallT = 0.15f;
        int stairEdge = -1;
        float stairC = 0.0f, bestLen = 0.0f;
        for (int pass = 0; pass < 2 && stairEdge < 0; ++pass)
            for (std::size_t i = 0; i < n; ++i) {
                const EdgeRole role = i < b_.edges.size() ? b_.edges[i] : EdgeRole::Street;
                if (role != (pass == 0 ? EdgeRole::Street : EdgeRole::Yard))
                    continue;
                const Face F = faceOf(i);
                if (F.len < std::max(st_.bay * 0.8f, 2.0f * halfW + 2.0f * pier + 0.6f))
                    continue;
                const int nb = std::max(1, static_cast<int>(std::lround((F.len - 2.0f * pier) / st_.bay)));
                const float bay = (F.len - 2.0f * pier) / static_cast<float>(nb);
                const float c = pier + bay * (static_cast<float>(nb / 2) + 0.5f);
                if (c - halfW - wallT < 0.4f || c + halfW + wallT > F.len - 0.4f)
                    continue;
                bool deep = true;
                for (const float s : {c - halfW - wallT, c, c + halfW + wallT})
                    deep = deep && depthInside(at2(F, s, 0.01f), inward(F)) > dBack + 1.2f;
                if (deep && F.len > bestLen) {
                    bestLen = F.len;
                    stairEdge = static_cast<int>(i);
                    stairC = c;
                }
            }
        const bool stairs = stairEdge >= 0 && FL >= 2;
        const Face SF = stairs ? faceOf(static_cast<std::size_t>(stairEdge)) : Face{};
        auto inStair = [&](glm::vec2 p, float pad) {
            if (!stairs)
                return false;
            const glm::vec2 o{SF.o.x, SF.o.z}, t{SF.t.x, SF.t.z};
            const float s = glm::dot(p - o, t), d = glm::dot(p - o, inward(SF));
            return s > stairC - halfW - wallT - pad && s < stairC + halfW + wallT + pad && d > -pad && d < dBack + wallT + pad;
        };
        // Floors: the ground slab whole, the upper ones with the stairwell notched out of the edge.
        Poly notched = in;
        if (stairs) {
            int k = -1;
            for (std::size_t j = 0; j < in.size(); ++j)
                if (j < src.size() && src[j] == stairEdge)
                    k = static_cast<int>(j);
            if (k >= 0) {
                const glm::vec2 a = in[static_cast<std::size_t>(k)], b = in[(static_cast<std::size_t>(k) + 1) % in.size()];
                const glm::vec2 t = glm::normalize(b - a);
                const glm::vec2 p0 = at2(SF, stairC - halfW, kWall), p1 = at2(SF, stairC + halfW, kWall);
                const float s0 = glm::dot(p0 - a, t), s1 = glm::dot(p1 - a, t), L = glm::length(b - a);
                if (s0 > 0.05f && s1 < L - 0.05f) {
                    const Poly notch{a + t * s0, at2(SF, stairC - halfW, dBack), at2(SF, stairC + halfW, dBack), a + t * s1};
                    notched.insert(notched.begin() + k + 1, notch.begin(), notch.end());
                    if (!isSimple(notched))
                        notched = in;
                }
            }
        }
        mesh.flat("concrete", in, 0.02f, true);
        for (int f = 1; f < FL; ++f) {
            const float y = floorY(f);
            mesh.flat("concrete", notched, y, true);
            mesh.flat("plaster", notched, y - 0.22f, false);
        }
        mesh.flat("plaster", in, top_ - 0.02f, false);
        // Partitions off every front between window pairs (and none through the stairwell).
        for (std::size_t i = 0; i < n; ++i) {
            const EdgeRole role = i < b_.edges.size() ? b_.edges[i] : EdgeRole::Street;
            const Face F = faceOf(i);
            if (role == EdgeRole::Party || F.len < st_.bay * 2.5f)
                continue;
            const int nb = std::max(1, static_cast<int>(std::lround((F.len - 2.0f * pier) / st_.bay)));
            const float bay = (F.len - 2.0f * pier) / static_cast<float>(nb);
            for (int j = 2; j < nb - 1; j += 2) {
                const float s = pier + bay * static_cast<float>(j);
                const float depth = depthInside(at2(F, s, 0.01f), inward(F));
                const float L = std::min(ir.range(3.4f, 5.0f), depth - 1.6f);
                if (L < 2.4f || inStair(at2(F, s, kWall), 0.6f) || inStair(at2(F, s, L), 0.6f) || inStair(at2(F, s, L * 0.5f), 0.6f))
                    continue;
                const float g0 = L - ir.range(1.0f, 1.3f), g1 = g0 + 0.9f; // doorway at the inner end
                for (int f = 0; f < FL; ++f) {
                    const float y0 = f == 0 ? 0.02f : floorY(f), y1 = floorY(f + 1) - (f + 1 < FL ? 0.22f : 0.02f);
                    fbox("plaster", F, s - 0.06f, s + 0.06f, y0, y1, kWall, g0);
                    fbox("plaster", F, s - 0.06f, s + 0.06f, std::min(y1, y0 + 2.1f), y1, g0, g1);
                    if (g1 < L)
                        fbox("plaster", F, s - 0.06f, s + 0.06f, y0, y1, g1, L);
                }
            }
        }
        if (!stairs)
            return;
        // Stairwell walls per storey: sides with doorways off the landing, the back, the spine.
        const float c = stairC;
        for (int f = 0; f < FL; ++f) {
            const float y0 = f == 0 ? 0.02f : floorY(f), y1 = floorY(f + 1) - (f + 1 < FL ? 0.22f : 0.02f);
            for (const float side : {-1.0f, 1.0f}) {
                const float sa = c + side * halfW, sb = c + side * (halfW + wallT);
                const float s0 = std::min(sa, sb), s1 = std::max(sa, sb);
                fbox("plaster", SF, s0, s1, y0, y1, kWall, 0.5f);
                fbox("plaster", SF, s0, s1, std::min(y1, y0 + 2.1f), y1, 0.5f, 1.45f);
                fbox("plaster", SF, s0, s1, y0, y1, 1.45f, dBack + wallT);
            }
            fbox("plaster", SF, c - halfW, c + halfW, y0, y1, dBack, dBack + wallT);
            fbox("plaster", SF, c - 0.07f, c + 0.07f, y0, y1, dLand, dRun);
        }
        // Flights: up the left half to the half landing, back up the right half to the next floor.
        for (int f = 0; f + 1 < FL; ++f) {
            const float y0 = f == 0 ? 0.02f : floorY(f), y1 = floorY(f + 1), h = y1 - y0, mid = y0 + h * 0.5f;
            const int steps = std::max(6, static_cast<int>(std::ceil(h * 0.5f / 0.18f)));
            const float tread = (dRun - dLand) / static_cast<float>(steps), rise = h * 0.5f / static_cast<float>(steps);
            for (int k = 1; k <= steps; ++k) {
                const float ya = y0 + rise * static_cast<float>(k), yb = mid + rise * static_cast<float>(k);
                fbox("concrete", SF, c - halfW, c - 0.07f, ya - rise - 0.16f, ya, dLand + tread * static_cast<float>(k - 1), dLand + tread * static_cast<float>(k));
                fbox("concrete", SF, c + 0.07f, c + halfW, yb - rise - 0.16f, yb, dRun - tread * static_cast<float>(k), dRun - tread * static_cast<float>(k - 1));
            }
            fbox("concrete", SF, c - halfW, c + halfW, mid - 0.2f, mid, dRun, dBack);                 // half landing
            fbox("concrete", SF, c - halfW, c + halfW, y1 - 0.22f, y1, kWall, dLand);                 // floor landing
        }
        // The top floor: a rail over the last flight's well.
        const float yt = floorY(FL - 1);
        fbox("rust", SF, c - halfW, c - 0.07f, yt + 0.95f, yt + 1.0f, dLand, dLand + 0.05f);
        fbox("rust", SF, c - halfW, c - 0.07f, yt, yt + 0.95f, dLand + 0.01f, dLand + 0.04f);
        (void)ir.next();
    }

    // Floor slabs of a ruin, seen through the empty windows and the broken top.
    void ruinSlabs()
    {
        Poly in;
        const std::size_t n = b_.footprint.size();
        if (!insetPolygon(b_.footprint, std::vector<float>(n, kWall), in))
            return;
        const int keep = std::max(1, b_.floors - ruinLoss_);
        for (int f = 1; f <= keep; ++f) {
            const float y = floorY(f);
            mesh.flat("concrete", in, y, true);
            mesh.flat("concrete", in, y - 0.22f, false);
        }
        mesh.flat("concrete", in, 0.02f, true);
    }

    static constexpr float kReveal = 0.25f;
    static constexpr float kWall = 0.35f;
    const BuildingSpec& b_;
    const FacadeStyle& st_;
    int lod_;
    Rng rng_;
    float top_ = 10.0f;
    float loggiaCols_ = 0.0f;
    glm::vec2 ruinDir_{1.0f, 0.0f};
    float ruinR_ = 10.0f;
    int ruinLoss_ = 0;
    bool open_ = false; // the walls have an inside (ruins, enterable houses at LOD0)
};


// ---------------------------------------------------------------- concrete skeletons
// A frame building gutted down to its structure (role "skeleton"):
//   Frame   -> grid(bay) over the footprint's oriented rectangle, cells inside the footprint
//   Storey  -> Slab per cell (some fallen through) + Column per grid node + edge Beams
//   Edge    -> Spandrel (a parapet panel, share `balconies`) | Infill with an empty window
//              (share `loggias`) | open
//   Collapse-> one side came down: storeys lost toward it, slabs hang from the last
//              columns or lie tilted on the heap, rebar out of every broken edge
// Everything but the frame and the slabs is LOD0 only.
class SkeletonBuilder {
public:
    SkeletonBuilder(const BuildingSpec& b, int lod) : b_(b), st_(b.style), lod_(lod), rng_(b.seed ^ 0x5CE1E70Bull) {}
    Mesh mesh;

    void run()
    {
        const Poly& fp = b_.footprint;
        if (fp.size() < 3)
            return;
        glm::vec2 half;
        minAreaRect(fp, c_, ax_, half);
        ay_ = {-ax_.y, ax_.x};
        const float bay = std::clamp(st_.bay, 3.0f, 12.0f);
        nu_ = std::max(1, static_cast<int>(std::round(2.0f * half.x / bay)));
        nv_ = std::max(1, static_cast<int>(std::round(2.0f * half.y / bay)));
        du_ = 2.0f * half.x / static_cast<float>(nu_);
        dv_ = 2.0f * half.y / static_cast<float>(nv_);
        o_ = c_ - ax_ * half.x - ay_ * half.y;
        F_ = std::max(1, b_.floors);
        // Which cells exist: inside the footprint.
        inside_.assign(static_cast<std::size_t>(nu_ * nv_), false);
        for (int i = 0; i < nu_; ++i)
            for (int j = 0; j < nv_; ++j)
                inside_[idx(i, j)] = pointInPolygon(fp, cell(i + 0.5f, j + 0.5f));
        // The collapse: storeys left per cell fall off toward one side.
        const float a = rng_.range(0.0f, 6.2831853f);
        const glm::vec2 dir{std::cos(a), std::sin(a)};
        const float R = std::max(4.0f, glm::length(half));
        const int loss = std::max(1, static_cast<int>(static_cast<float>(F_) * rng_.range(0.45f, 0.85f)));
        floors_.assign(inside_.size(), 0);
        for (int i = 0; i < nu_; ++i)
            for (int j = 0; j < nv_; ++j) {
                if (!inside_[idx(i, j)])
                    continue;
                const float k = std::clamp(glm::dot(cell(i + 0.5f, j + 0.5f) - c_, dir) / R + 0.35f, 0.0f, 1.0f);
                const int jag = rng_.below(2);
                floors_[idx(i, j)] = std::max(1, F_ - static_cast<int>(std::lround(static_cast<float>(loss) * k * 1.2f)) - (k > 0.3f ? jag : 0));
            }
        // Slabs (holes where a slab fell through), beams, columns.
        holes_.assign(inside_.size() * static_cast<std::size_t>(F_ + 1), false);
        for (int f = 1; f <= F_; ++f)
            for (int i = 0; i < nu_; ++i)
                for (int j = 0; j < nv_; ++j)
                    holes_[hidx(i, j, f)] = f < F_ && rng_.chance(0.06f + 0.12f * static_cast<float>(f) / static_cast<float>(F_));
        for (int f = 1; f <= F_; ++f)
            for (int i = 0; i < nu_; ++i)
                for (int j = 0; j < nv_; ++j)
                    if (slab(i, j, f))
                        slabCell(i, j, f);
        for (int i = 0; i <= nu_; ++i)
            for (int j = 0; j <= nv_; ++j)
                column(i, j);
        for (int f = 1; f <= F_; ++f)
            edges(f);
        if (lod_ == 0)
            heap(dir, R);
        // Ground floor slab (on the ground, cracked).
        for (int i = 0; i < nu_; ++i)
            for (int j = 0; j < nv_; ++j)
                if (inside_[idx(i, j)])
                    quadH("concrete_moss", cell(i, j), cell(i + 1, j), cell(i + 1, j + 1), cell(i, j + 1), 0.04f, true);
    }

private:
    std::size_t idx(int i, int j) const { return static_cast<std::size_t>(i * nv_ + j); }
    std::size_t hidx(int i, int j, int f) const { return idx(i, j) * static_cast<std::size_t>(F_ + 1) + static_cast<std::size_t>(f); }
    glm::vec2 cell(float i, float j) const { return o_ + ax_ * (i * du_) + ay_ * (j * dv_); }
    float y(int f) const { return f <= 0 ? 0.0f : st_.groundStorey + static_cast<float>(f - 1) * st_.storey; }
    bool in(int i, int j) const { return i >= 0 && j >= 0 && i < nu_ && j < nv_ && inside_[idx(i, j)]; }
    // A slab over storey f-1 at the height y(f) (f = 1 .. F, F = the roof).
    bool slab(int i, int j, int f) const { return in(i, j) && f <= floors_[idx(i, j)] && !holes_[hidx(i, j, f)]; }

    void quadH(const std::string& m, glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d, float yy, bool up)
    {
        const float t = mat(m).tile;
        mesh.quad(m, {a.x, yy, a.y}, {b.x, yy, b.y}, {c.x, yy, c.y}, {d.x, yy, d.y}, {0, up ? 1.0f : -1.0f, 0}, a / t, b / t, c / t, d / t);
    }
    // Vertical quad from a to b (plan), y0..y1, facing n.
    void quadV(const std::string& m, glm::vec2 a, glm::vec2 b, float y0, float y1, glm::vec2 n)
    {
        const float t = mat(m).tile, L = glm::length(b - a);
        mesh.quad(m, {a.x, y0, a.y}, {b.x, y0, b.y}, {b.x, y1, b.y}, {a.x, y1, a.y}, {n.x, 0, n.y}, {0, y0 / t}, {L / t, y0 / t}, {L / t, y1 / t},
                  {0, y1 / t});
    }
    // An oriented box: centre, three unit axes, half sizes.
    void obox(const std::string& m, glm::vec3 c, glm::vec3 u, glm::vec3 v, glm::vec3 w, glm::vec3 h)
    {
        const float t = mat(m).tile;
        const glm::vec3 axes[3] = {u * h.x, v * h.y, w * h.z};
        for (int k = 0; k < 3; ++k)
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                const glm::vec3 n = glm::normalize(axes[k]) * static_cast<float>(sgn);
                const glm::vec3 a = axes[(k + 1) % 3], b = axes[(k + 2) % 3], f = c + axes[k] * static_cast<float>(sgn);
                const float la = 2.0f * glm::length(a) / t, lb = 2.0f * glm::length(b) / t;
                mesh.quad(m, f - a - b, f + a - b, f + a + b, f - a + b, n, {0, 0}, {la, 0}, {la, lb}, {0, lb});
            }
    }
    glm::vec3 v3(glm::vec2 p, float yy) const { return {p.x, yy, p.y}; }

    void slabCell(int i, int j, int f)
    {
        const float top = y(f), th = 0.22f;
        const glm::vec2 a = cell(i, j), b = cell(i + 1, j), c = cell(i + 1, j + 1), d = cell(i, j + 1);
        quadH(f == floors_[idx(i, j)] ? "concrete_moss" : "frame_concrete", a, b, c, d, top, true);
        quadH("frame_concrete", a, b, c, d, top - th, false);
        // Edges where no slab continues: the broken face (and rebar out of it).
        const glm::vec2 side[4][2] = {{a, b}, {b, c}, {c, d}, {d, a}};
        const int ni[4] = {i, i + 1, i, i - 1}, nj[4] = {j - 1, j, j + 1, j};
        for (int e = 0; e < 4; ++e) {
            if (slab(ni[e], nj[e], f) || lod_)
                continue;
            const glm::vec2 p = side[e][0], q = side[e][1];
            const glm::vec2 t = glm::normalize(q - p), n{t.y, -t.x};
            quadV("st_concrete_crack", p, q, top - th, top, glm::dot(n, (p + q) * 0.5f - (a + c) * 0.5f) > 0.0f ? n : -n);
            const bool broken = in(ni[e], nj[e]); // a neighbour that fell, not the facade
            if (broken && lod_ == 0)
                rebar(p, q, n * (glm::dot(n, (p + q) * 0.5f - (a + c) * 0.5f) > 0.0f ? 1.0f : -1.0f), top - th * 0.5f);
        }
    }

    void rebar(glm::vec2 p, glm::vec2 q, glm::vec2 out, float yy)
    {
        const float L = glm::length(q - p);
        const int n = static_cast<int>(L / 0.6f);
        const glm::vec2 t = (q - p) / L;
        for (int k = 1; k < n; ++k) {
            if (!rng_.chance(0.55f))
                continue;
            const float len = rng_.range(0.3f, 1.3f), droop = rng_.range(0.1f, 0.9f);
            const glm::vec2 s = p + t * (static_cast<float>(k) * L / static_cast<float>(n));
            const glm::vec3 a = v3(s, yy), b = v3(s + out * len, yy - droop * len);
            const glm::vec3 dir = glm::normalize(b - a);
            const glm::vec3 side = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
            const glm::vec3 up = glm::cross(side, dir);
            obox("rust", (a + b) * 0.5f, dir, side, up, {glm::length(b - a) * 0.5f, 0.012f, 0.012f});
        }
    }

    void column(int i, int j)
    {
        // Height: up to the highest slab among the cells around the node.
        int top = 0;
        for (int di = -1; di <= 0; ++di)
            for (int dj = -1; dj <= 0; ++dj)
                if (in(i + di, j + dj))
                    top = std::max(top, floors_[idx(i + di, j + dj)]);
        if (top <= 0)
            return;
        const glm::vec2 p = cell(static_cast<float>(i), static_cast<float>(j));
        const float w = 0.24f;
        float y1 = y(top);
        // A broken stub above the last slab now and then, rebar out of its top.
        const bool stub = top < F_ && rng_.chance(0.5f);
        if (stub)
            y1 += st_.storey * rng_.range(0.25f, 0.8f);
        obox("frame_concrete", v3(p, y1 * 0.5f), glm::vec3(ax_.x, 0, ax_.y), glm::vec3(0, 1, 0), glm::vec3(ay_.x, 0, ay_.y), {w, y1 * 0.5f, w});
        if (stub && lod_ == 0)
            for (int k = 0; k < 4; ++k) {
                const glm::vec2 c = p + (ax_ * ((k & 1) ? 0.14f : -0.14f)) + (ay_ * ((k & 2) ? 0.14f : -0.14f));
                const float h = rng_.range(0.3f, 0.9f);
                obox("rust", v3(c, y1 + h * 0.5f), {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.012f, h * 0.5f, 0.012f});
            }
    }

    // Facade edges of storey f: beams under the slab, spandrels, infill walls with an empty window.
    void edges(int f)
    {
        const float top = y(f), bottom = y(f - 1);
        for (int i = 0; i < nu_; ++i)
            for (int j = 0; j < nv_; ++j) {
                if (!in(i, j) || f > floors_[idx(i, j)])
                    continue;
                const glm::vec2 a = cell(i, j), b = cell(i + 1, j), c = cell(i + 1, j + 1), d = cell(i, j + 1);
                const glm::vec2 side[4][2] = {{a, b}, {b, c}, {c, d}, {d, a}};
                const int ni[4] = {i, i + 1, i, i - 1}, nj[4] = {j - 1, j, j + 1, j};
                const glm::vec2 mid = (a + c) * 0.5f;
                for (int e = 0; e < 4; ++e) {
                    if (in(ni[e], nj[e]))
                        continue; // inside the building: open plan
                    const glm::vec2 p = side[e][0], q = side[e][1];
                    const glm::vec2 t = glm::normalize(q - p);
                    glm::vec2 n{t.y, -t.x};
                    if (glm::dot(n, (p + q) * 0.5f - mid) < 0.0f)
                        n = -n;
                    // Edge beam under the slab.
                    if (slab(i, j, f) && lod_ == 0)
                        obox("frame_concrete", v3((p + q) * 0.5f - n * 0.15f, top - 0.45f), glm::vec3(t.x, 0, t.y), {0, 1, 0}, glm::vec3(n.x, 0, n.y),
                             {glm::length(q - p) * 0.5f, 0.23f, 0.15f});
                    if (f == 1)
                        continue; // the ground floor stands open (shops, entrances long gone)
                    const float r = rng_.uniform();
                    const glm::vec2 p2 = p + t * 0.24f, q2 = q - t * 0.24f;
                    if (r < st_.loggias) { // infill wall with an empty window
                        const float L = glm::length(q2 - p2), ww = std::min(L * 0.55f, st_.windowWidth * 1.4f);
                        const float s0 = (L - ww) * 0.5f, sill = bottom + st_.sill, head = std::min(top - 0.5f, sill + st_.windowHeight);
                        const glm::vec2 w0 = p2 + t * s0, w1 = p2 + t * (s0 + ww);
                        for (int sd = 0; sd < (lod_ ? 1 : 2); ++sd) { // outside and inside
                            const glm::vec2 nn = sd ? -n : n;
                            const glm::vec2 off = n * (sd ? -0.1f : 0.1f);
                            quadV(st_.wall, p2 + off, w0 + off, bottom, top - 0.68f, nn);
                            quadV(st_.wall, w1 + off, q2 + off, bottom, top - 0.68f, nn);
                            quadV(st_.wall, w0 + off, w1 + off, bottom, sill, nn);
                            quadV(st_.wall, w0 + off, w1 + off, head, top - 0.68f, nn);
                        }
                    } else if (r < st_.loggias + st_.balconies) { // spandrel panel (a parapet at the slab edge)
                        for (int sd = 0; sd < (lod_ ? 1 : 2); ++sd) {
                            const glm::vec2 off = n * (sd ? -0.1f : 0.1f);
                            quadV(st_.wall, p2 + off, q2 + off, bottom, bottom + 0.95f, sd ? -n : n);
                        }
                        quadH("frame_concrete", p2 + n * 0.1f, q2 + n * 0.1f, q2 - n * 0.1f, p2 - n * 0.1f, bottom + 0.95f, true);
                    }
                }
            }
    }

    // What came down lies in a heap on that side: tilted slabs, a beam leaning on the frame,
    // chunks of concrete and a few twisted rods.
    void heap(glm::vec2 dir, float R)
    {
        const glm::vec2 side{-dir.y, dir.x};
        const glm::vec2 base = c_ + dir * R * 0.55f;
        const int slabs = 6 + F_ + rng_.below(6);
        for (int k = 0; k < slabs; ++k) {
            const glm::vec2 p = base + side * rng_.range(-R * 0.5f, R * 0.5f) + dir * rng_.range(-R * 0.3f, R * 0.25f);
            const float yaw = rng_.range(0.0f, 6.2831853f), tilt = rng_.range(0.1f, 0.9f);
            const glm::vec3 u{std::cos(yaw), 0, std::sin(yaw)};
            const glm::vec3 w = glm::vec3(-u.z, 0, u.x);
            const glm::vec3 uu = glm::normalize(u * std::cos(tilt) + glm::vec3(0, std::sin(tilt), 0));
            const glm::vec3 vv = glm::normalize(glm::cross(w, uu));
            const glm::vec3 hs{rng_.range(1.2f, 2.8f), 0.11f, rng_.range(0.8f, 1.6f)};
            obox("frame_concrete", v3(p, hs.x * std::sin(tilt) * 0.8f + 0.1f), uu, vv, w, hs);
        }
        // A beam leaning from the ground onto the first slab still standing.
        if (F_ >= 2) {
            const glm::vec2 foot = c_ + dir * R * 0.9f + side * rng_.range(-R * 0.3f, R * 0.3f);
            const glm::vec2 head = c_ + dir * R * 0.2f + side * rng_.range(-R * 0.2f, R * 0.2f);
            const glm::vec3 a = v3(foot, 0.0f), b = v3(head, y(1) + rng_.range(0.0f, st_.storey));
            const glm::vec3 d = glm::normalize(b - a), s = glm::normalize(glm::cross(d, glm::vec3(0, 1, 0))), u = glm::cross(s, d);
            obox("frame_concrete", (a + b) * 0.5f, d, s, u, {glm::length(b - a) * 0.5f, 0.2f, 0.28f});
        }
        for (int k = 0; k < 18 + 3 * F_; ++k) {
            const glm::vec2 p = base + side * rng_.range(-R * 0.6f, R * 0.6f) + dir * rng_.range(-R * 0.35f, R * 0.4f);
            const float sz = rng_.range(0.2f, 0.9f), yaw = rng_.range(0.0f, 6.2831853f);
            const glm::vec3 u{std::cos(yaw), 0, std::sin(yaw)};
            obox(k % 3 ? "frame_concrete" : "st_concrete_crack", v3(p, sz * 0.35f), u, {0, 1, 0}, {-u.z, 0, u.x}, {sz, sz * 0.45f, sz * 0.7f});
        }
    }

    const BuildingSpec& b_;
    const FacadeStyle& st_;
    int lod_;
    Rng rng_;
    glm::vec2 c_{0.0f}, ax_{1.0f, 0.0f}, ay_{0.0f, 1.0f}, o_{0.0f};
    int nu_ = 1, nv_ = 1, F_ = 1;
    float du_ = 6.0f, dv_ = 6.0f;
    std::vector<bool> inside_, holes_;
    std::vector<int> floors_;
};

// ---------------------------------------------------------------- factory halls
// An abandoned steel-framed production hall (role "hall"), open inside:
//   Hall    -> Floor + LongWall x2 + Gable x2 + Frames(bay) + Roof + [Gallery] + Pipes + Debris
//   LongWall-> brick plinth + brick piers at the frames + steel window grid (glass share `loggias`) + top band
//   Gable   -> brick, one with the big gate (no doors left), the other with a high window grid
//   Frame   -> I-section columns + a truss (chords, posts, diagonals); purlins between frames
//   Roof    -> pitched, per bay: sheet panels (some fallen in) and a glazed strip either side of the ridge
//   Gallery -> a walkway along one side at 4.5 m (chance `balconies`), brackets, railing, stair, one bay down
// LOD1: walls, piers, roof sheets, columns and chords only. Eave height = ground storey.
void orientedBox(Mesh& mesh, const std::string& m, glm::vec3 c, glm::vec3 u, glm::vec3 v, glm::vec3 w, glm::vec3 h)
{
    const float t = mat(m).tile;
    const glm::vec3 axes[3] = {u * h.x, v * h.y, w * h.z};
    for (int k = 0; k < 3; ++k)
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            const glm::vec3 n = glm::normalize(axes[k]) * static_cast<float>(sgn);
            const glm::vec3 a = axes[(k + 1) % 3], b = axes[(k + 2) % 3], f = c + axes[k] * static_cast<float>(sgn);
            const float la = 2.0f * glm::length(a) / t, lb = 2.0f * glm::length(b) / t;
            mesh.quad(m, f - a - b, f + a - b, f + a + b, f - a + b, n, {0, 0}, {la, 0}, {la, lb}, {0, lb});
        }
}

// A bar from a to b with a square-ish section (w across, h "up").
void bar(Mesh& mesh, const std::string& m, glm::vec3 a, glm::vec3 b, float w, float h)
{
    const glm::vec3 d = b - a;
    const float L = glm::length(d);
    if (L < 1e-3f)
        return;
    const glm::vec3 t = d / L;
    glm::vec3 s = glm::cross(t, glm::vec3(0, 1, 0));
    if (glm::length(s) < 1e-3f)
        s = glm::vec3(1, 0, 0);
    s = glm::normalize(s);
    const glm::vec3 up = glm::cross(s, t);
    orientedBox(mesh, m, (a + b) * 0.5f, t, s, up, {L * 0.5f, w * 0.5f, h * 0.5f});
}

class HallBuilder {
public:
    HallBuilder(const BuildingSpec& b, int lod) : b_(b), st_(b.style), lod_(lod), rng_(b.seed ^ 0x4A11B00Bull) {}
    Mesh mesh;

    void run()
    {
        glm::vec2 half;
        minAreaRect(b_.footprint, c_, ax_, half);
        if (half.y > half.x) { // the long side along u
            std::swap(half.x, half.y);
            ax_ = {-ax_.y, ax_.x};
        }
        ay_ = {-ax_.y, ax_.x};
        L_ = half.x * 2.0f;
        W_ = half.y * 2.0f;
        He_ = std::max(5.0f, st_.groundStorey);
        rise_ = W_ * 0.17f;
        const int n = std::max(2, static_cast<int>(std::round(L_ / std::clamp(st_.bay, 4.0f, 12.0f))));
        for (int k = 0; k <= n; ++k)
            frames_.push_back(-L_ * 0.5f + L_ * static_cast<float>(k) / static_cast<float>(n));
        gallery_ = W_ > 14.0f && rng_.chance(st_.balconies);
        gate_ = rng_.chance(0.5f) ? 1 : -1;
        floor();
        for (const float side : {-1.0f, 1.0f})
            longWall(side);
        for (const float side : {-1.0f, 1.0f})
            gable(side);
        for (std::size_t k = 0; k < frames_.size(); ++k)
            frame(frames_[k], k == 0 || k + 1 == frames_.size());
        roof();
        if (gallery_)
            gallery();
        if (lod_ == 0) {
            pipes();
            debris();
        }
    }

private:
    glm::vec3 P(float u, float v, float y) const
    {
        const glm::vec2 p = c_ + ax_ * u + ay_ * v;
        return {p.x, y, p.y};
    }
    glm::vec3 U() const { return {ax_.x, 0, ax_.y}; }
    glm::vec3 V() const { return {ay_.x, 0, ay_.y}; }
    float roofY(float v) const { return He_ + rise_ * (1.0f - std::abs(v) / (W_ * 0.5f)); }
    // A wall slab between (u0, v0) and (u1, v1), y0..y1, thickness t, centred on that line.
    void slab(const std::string& m, float u0, float v0, float u1, float v1, float y0, float y1, float t)
    {
        const glm::vec3 a = P(u0, v0, 0), b = P(u1, v1, 0);
        const glm::vec3 d = b - a;
        const float L = glm::length(d);
        if (L < 0.02f || y1 - y0 < 0.02f)
            return;
        const glm::vec3 x = d / L, z = glm::normalize(glm::cross(x, glm::vec3(0, 1, 0)));
        orientedBox(mesh, m, (a + b) * 0.5f + glm::vec3(0, (y0 + y1) * 0.5f, 0), x, {0, 1, 0}, z, {L * 0.5f, (y1 - y0) * 0.5f, t * 0.5f});
    }

    void floor()
    {
        const float t = mat("concrete_moss").tile;
        const glm::vec3 a = P(-L_ / 2, -W_ / 2, 0.03f), b = P(L_ / 2, -W_ / 2, 0.03f), c = P(L_ / 2, W_ / 2, 0.03f), d = P(-L_ / 2, W_ / 2, 0.03f);
        mesh.quad("concrete_moss", a, b, c, d, {0, 1, 0}, {0, 0}, {L_ / t, 0}, {L_ / t, W_ / t}, {0, W_ / t});
    }

    // Long wall at v = side * W/2.
    void longWall(float side)
    {
        const float v = side * (W_ * 0.5f - 0.15f), plinth = 1.4f, band = 0.9f;
        slab(st_.plinth, -L_ / 2, v, L_ / 2, v, 0.0f, plinth, 0.38f);
        slab(st_.wall, -L_ / 2, v, L_ / 2, v, He_ - band, He_ + 0.3f, 0.38f);
        for (std::size_t k = 0; k < frames_.size(); ++k) {
            const float u = std::clamp(frames_[k], -L_ / 2 + 0.35f, L_ / 2 - 0.35f);
            slab(st_.wall, u - 0.35f, v, u + 0.35f, v, plinth, He_ - band, 0.5f);
            if (k + 1 == frames_.size())
                break;
            const float u0 = frames_[k] + 0.35f, u1 = frames_[k + 1] - 0.35f;
            windowGrid(u0, u1, v, plinth, He_ - band, side);
        }
    }

    // Steel window between piers: mullions every ~1 m, transoms every ~1.2 m, some panes left.
    void windowGrid(float u0, float u1, float v, float y0, float y1, float side)
    {
        const int nx = std::max(2, static_cast<int>(std::round((u1 - u0) / 1.0f)));
        const int ny = std::max(2, static_cast<int>(std::round((y1 - y0) / std::max(0.8f, st_.windowHeight))));
        const glm::vec3 n = V() * side;
        if (lod_ == 0) {
            for (int i = 0; i <= nx; ++i) {
                const float u = u0 + (u1 - u0) * static_cast<float>(i) / static_cast<float>(nx);
                bar(mesh, "rust_dark", P(u, v, y0), P(u, v, y1), 0.06f, 0.1f);
            }
            for (int j = 0; j <= ny; ++j) {
                const float y = y0 + (y1 - y0) * static_cast<float>(j) / static_cast<float>(ny);
                bar(mesh, "rust_dark", P(u0, v, y), P(u1, v, y), 0.1f, 0.06f);
            }
        }
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j) {
                if (!rng_.chance(st_.loggias))
                    continue;
                const float ua = u0 + (u1 - u0) * static_cast<float>(i) / static_cast<float>(nx) + 0.04f;
                const float ub = u0 + (u1 - u0) * static_cast<float>(i + 1) / static_cast<float>(nx) - 0.04f;
                const float ya = y0 + (y1 - y0) * static_cast<float>(j) / static_cast<float>(ny) + 0.04f;
                const float yb = y0 + (y1 - y0) * static_cast<float>(j + 1) / static_cast<float>(ny) - 0.04f;
                mesh.quad("glass", P(ua, v, ya), P(ub, v, ya), P(ub, v, yb), P(ua, v, yb), n, {0, 0}, {1, 0}, {1, 1}, {0, 1});
            }
    }

    void gable(float side)
    {
        const float u = side * (L_ * 0.5f - 0.15f), hw = W_ * 0.5f;
        const float t = 0.38f;
        if (side == static_cast<float>(gate_)) { // the gate: an opening 5-6 m wide, 5 m high
            const float gw = std::min(W_ * 0.4f, 6.0f) * 0.5f, gh = std::min(He_ - 1.5f, 5.2f);
            slab(st_.wall, u, -hw, u, -gw, 0.0f, He_, t);
            slab(st_.wall, u, gw, u, hw, 0.0f, He_, t);
            slab(st_.wall, u, -gw, u, gw, gh, He_, t);
            bar(mesh, "rust_dark", P(u, -gw, gh), P(u, gw, gh), 0.3f, 0.3f); // the gate's steel lintel
            if (lod_ == 0 && rng_.chance(0.6f)) {                             // one door leaf fallen in
                const glm::vec3 a = P(u - side * 0.2f, gw * 0.2f, 0.15f), b = P(u - side * (gh * 0.9f), gw * 0.1f, 0.6f);
                bar(mesh, "rust_sheet", a, b, gw * 0.9f, 0.06f);
            }
        } else { // a high window grid over a brick base
            slab(st_.wall, u, -hw, u, hw, 0.0f, He_ * 0.45f, t);
            slab(st_.wall, u, -hw, u, -hw * 0.6f, He_ * 0.45f, He_, t);
            slab(st_.wall, u, hw * 0.6f, u, hw, He_ * 0.45f, He_, t);
            slab(st_.wall, u, -hw * 0.6f, u, hw * 0.6f, He_ - 0.6f, He_, t);
            // window grid in the gable plane (reuse the long-wall routine by swapping axes)
            const int nx = std::max(2, static_cast<int>(std::round(hw * 1.2f / 1.0f)));
            const float y0 = He_ * 0.45f, y1 = He_ - 0.6f;
            if (lod_ == 0) {
                for (int i = 0; i <= nx; ++i) {
                    const float v = -hw * 0.6f + hw * 1.2f * static_cast<float>(i) / static_cast<float>(nx);
                    bar(mesh, "rust_dark", P(u, v, y0), P(u, v, y1), 0.06f, 0.1f);
                }
                for (int j = 0; j <= 3; ++j) {
                    const float y = y0 + (y1 - y0) * static_cast<float>(j) / 3.0f;
                    bar(mesh, "rust_dark", P(u, -hw * 0.6f, y), P(u, hw * 0.6f, y), 0.1f, 0.06f);
                }
            }
        }
        // The triangle up to the ridge: two faces.
        const float tt = mat(st_.wall).tile;
        for (const float face : {-1.0f, 1.0f}) {
            const float uu = u + face * t * 0.5f;
            const glm::vec3 a = P(uu, -hw, He_ + 0.3f), b = P(uu, hw, He_ + 0.3f), c = P(uu, 0.0f, He_ + rise_ + 0.3f);
            mesh.quad(st_.wall, a, b, c, c, U() * face, {0, 0}, {W_ / tt, 0}, {hw / tt, rise_ / tt}, {hw / tt, rise_ / tt});
        }
    }

    void frame(float u, bool end)
    {
        const float hw = W_ * 0.5f - 0.6f;
        for (const float side : {-1.0f, 1.0f}) {
            const float v = side * hw;
            if (lod_ == 0) { // I-section: two flanges and a web
                for (const float f : {-0.14f, 0.14f})
                    orientedBox(mesh, "st_metal_paint", P(u + f, v, He_ * 0.5f), U(), {0, 1, 0}, V(), {0.015f, He_ * 0.5f, 0.15f});
                orientedBox(mesh, "st_metal_paint", P(u, v, He_ * 0.5f), U(), {0, 1, 0}, V(), {0.14f, He_ * 0.5f, 0.01f});
            } else {
                orientedBox(mesh, "st_metal_paint", P(u, v, He_ * 0.5f), U(), {0, 1, 0}, V(), {0.15f, He_ * 0.5f, 0.15f});
            }
        }
        if (end && lod_ == 1)
            return;
        // Truss: bottom chord, two top chords, posts and diagonals.
        const float yb = He_ - 0.2f;
        bar(mesh, "rust", P(u, -hw, yb), P(u, hw, yb), 0.14f, 0.16f);
        const float rY = He_ + rise_ - 0.1f;
        bar(mesh, "rust", P(u, -hw, yb), P(u, 0.0f, rY), 0.14f, 0.16f);
        bar(mesh, "rust", P(u, hw, yb), P(u, 0.0f, rY), 0.14f, 0.16f);
        if (lod_ == 1)
            return;
        const int panels = 6;
        for (int i = 1; i < panels; ++i) {
            const float v = -hw + 2.0f * hw * static_cast<float>(i) / static_cast<float>(panels);
            const float top = yb + (rY - yb) * (1.0f - std::abs(v) / hw);
            bar(mesh, "rust", P(u, v, yb), P(u, v, top), 0.08f, 0.08f);
            const float vn = -hw + 2.0f * hw * static_cast<float>(i + (v < 0.0f ? 1 : -1)) / static_cast<float>(panels);
            const float topn = yb + (rY - yb) * (1.0f - std::abs(vn) / hw);
            bar(mesh, "rust", P(u, v, yb), P(u, vn, topn), 0.07f, 0.07f);
        }
    }

    void roof()
    {
        const float hw = W_ * 0.5f + 0.3f;
        const float tt = mat(st_.roof).tile;
        for (std::size_t k = 0; k + 1 < frames_.size(); ++k) {
            const float u0 = frames_[k], u1 = frames_[k + 1];
            for (const float side : {-1.0f, 1.0f}) {
                // Four strips from the eave to the ridge; strip 2 is glazed.
                for (int s = 0; s < 4; ++s) {
                    const float v0 = side * hw * (1.0f - static_cast<float>(s) / 4.0f), v1 = side * hw * (1.0f - static_cast<float>(s + 1) / 4.0f);
                    const float y0 = roofY(std::min(std::abs(v0), W_ * 0.5f)) + 0.3f, y1 = roofY(std::abs(v1)) + 0.3f;
                    const glm::vec3 a = P(u0, v0, y0), b = P(u1, v0, y0), c = P(u1, v1, y1), d = P(u0, v1, y1);
                    const glm::vec3 nrm = glm::normalize(glm::cross(b - a, d - a)) * (glm::cross(b - a, d - a).y > 0.0f ? 1.0f : -1.0f);
                    if (s == 2) { // skylight: mullions and a few panes left
                        if (lod_ == 0) {
                            for (int i = 0; i <= 4; ++i) {
                                const float f = static_cast<float>(i) / 4.0f;
                                bar(mesh, "rust_dark", a + (b - a) * f, d + (c - d) * f, 0.06f, 0.08f);
                            }
                            for (int i = 0; i < 4; ++i)
                                if (rng_.chance(st_.loggias * 0.8f)) {
                                    const float f0 = static_cast<float>(i) / 4.0f + 0.01f, f1 = static_cast<float>(i + 1) / 4.0f - 0.01f;
                                    mesh.quad("glass", a + (b - a) * f0, a + (b - a) * f1, d + (c - d) * f1, d + (c - d) * f0, nrm, {0, 0}, {1, 0}, {1, 1}, {0, 1});
                                }
                        }
                        continue;
                    }
                    if (rng_.chance(0.16f)) { // fallen in: the sky shows, light comes down
                        holes_.push_back({(u0 + u1) * 0.5f, (v0 + v1) * 0.5f});
                        continue;
                    }
                    const float lu = (u1 - u0) / tt, lv = glm::length(d - a) / tt;
                    mesh.quad(st_.roof, a, b, c, d, nrm, {0, 0}, {lu, 0}, {lu, lv}, {0, lv});
                    mesh.quad(st_.roof, a - nrm * 0.03f, b - nrm * 0.03f, c - nrm * 0.03f, d - nrm * 0.03f, -nrm, {0, 0}, {lu, 0}, {lu, lv}, {0, lv});
                }
            }
        }
        // Purlins along the hall.
        if (lod_ == 0)
            for (const float f : {-0.75f, -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 0.75f}) {
                const float v = f * (W_ * 0.5f - 0.6f);
                const float y = roofY(std::abs(v)) + 0.15f;
                bar(mesh, "rust", P(-L_ / 2 + 0.3f, v, y), P(L_ / 2 - 0.3f, v, y), 0.1f, 0.14f);
            }
    }

    void gallery()
    {
        const float side = rng_.chance(0.5f) ? 1.0f : -1.0f;
        const float y = 4.5f, wv = 1.8f;
        const float vOut = side * (W_ * 0.5f - 0.6f), vIn = vOut - side * wv;
        const float u0 = -L_ / 2 + 1.0f, u1 = L_ / 2 - 1.0f;
        const int fallen = 1 + rng_.below(std::max(1, static_cast<int>(frames_.size()) - 2));
        for (std::size_t k = 0; k + 1 < frames_.size(); ++k) {
            const float a = std::max(u0, frames_[k]), b = std::min(u1, frames_[k + 1]);
            if (static_cast<int>(k) == fallen) { // this bay came down: the deck hangs from one end to the floor
                if (lod_ == 0)
                    bar(mesh, "st_metal_paint", P(a, (vOut + vIn) * 0.5f, y), P(b - 0.5f, (vOut + vIn) * 0.5f + side * 0.3f, 0.2f), wv * 0.9f, 0.06f);
                continue;
            }
            orientedBox(mesh, "st_metal_paint", P((a + b) * 0.5f, (vOut + vIn) * 0.5f, y), U(), {0, 1, 0}, V(), {(b - a) * 0.5f, 0.04f, wv * 0.5f});
            if (lod_ == 1)
                continue;
            // Railing on the open side and a bracket under the deck at the column.
            for (float u = a; u <= b + 0.01f; u += (b - a) / 4.0f)
                bar(mesh, "rust", P(u, vIn, y), P(u, vIn, y + 1.05f), 0.04f, 0.04f);
            bar(mesh, "rust", P(a, vIn, y + 1.05f), P(b, vIn, y + 1.05f), 0.05f, 0.05f);
            bar(mesh, "rust", P(a, vIn, y + 0.55f), P(b, vIn, y + 0.55f), 0.04f, 0.04f);
            bar(mesh, "rust", P(a, vOut, y - 1.2f), P(a, vIn, y - 0.05f), 0.08f, 0.08f);
        }
        // A straight stair up at the end away from the gate.
        if (lod_ == 0) {
            const float uS = -static_cast<float>(gate_) * (L_ / 2 - 1.5f), dir = static_cast<float>(gate_);
            const int steps = 15;
            for (int i = 0; i < steps; ++i) {
                const float uu = uS + dir * (static_cast<float>(i) * 0.3f), yy = y * static_cast<float>(i + 1) / static_cast<float>(steps);
                orientedBox(mesh, "st_metal_paint", P(uu, vIn + side * 0.0f - side * 0.5f, yy - 0.02f), U(), {0, 1, 0}, V(), {0.15f, 0.02f, 0.5f});
            }
            bar(mesh, "rust", P(uS, vIn - side, 0.0f), P(uS + dir * 4.5f, vIn - side, y), 0.06f, 0.2f);
        }
    }

    void pipes()
    {
        const float side = gallery_ ? -1.0f : (rng_.chance(0.5f) ? 1.0f : -1.0f);
        const float v = side * (W_ * 0.5f - 0.65f);
        const int broken = rng_.below(static_cast<int>(frames_.size()) - 1);
        for (const float y : {3.2f, 3.65f}) {
            for (std::size_t k = 0; k + 1 < frames_.size(); ++k) {
                const float a = frames_[k], b = frames_[k + 1];
                if (static_cast<int>(k) == broken && y > 3.5f) { // snapped, one end on the floor
                    bar(mesh, "rust_dark", P(a, v, y), P(b - 1.0f, v - side * 0.8f, 0.2f), 0.24f, 0.24f);
                    continue;
                }
                bar(mesh, "rust_dark", P(a, v, y), P(b, v, y), 0.22f, 0.22f);
            }
        }
        for (const float u : frames_)
            bar(mesh, "rust", P(u, v - side * 0.1f, 3.0f), P(u, v - side * 0.1f, 3.9f), 0.06f, 0.06f);
    }

    void debris()
    {
        // Planks and sheets fallen from the roof, bricks under the walls.
        for (int i = 0; i < 14; ++i) {
            const float u = rng_.range(-L_ * 0.45f, L_ * 0.45f), v = rng_.range(-W_ * 0.4f, W_ * 0.4f);
            const float yaw = rng_.range(0.0f, 6.2831853f), tilt = rng_.range(0.0f, 0.35f);
            const glm::vec3 d{std::cos(yaw), std::sin(tilt), std::sin(yaw)};
            const float len = rng_.range(1.5f, 4.0f);
            const glm::vec3 c = P(u, v, 0.08f + std::sin(tilt) * len * 0.5f);
            if (i < 9)
                bar(mesh, "wood", c - d * (len * 0.5f), c + d * (len * 0.5f), 0.22f, 0.04f);
            else
                bar(mesh, st_.roof, c - d * (len * 0.5f), c + d * (len * 0.5f), rng_.range(0.8f, 1.4f), 0.02f);
        }
        for (const auto& h : holes_) // what fell through the holes in the roof
            if (rng_.chance(0.6f)) {
                const glm::vec3 c = P(h.x + rng_.range(-1.0f, 1.0f), h.y * 0.8f, 0.1f);
                const float yaw = rng_.range(0.0f, 6.2831853f);
                const glm::vec3 d{std::cos(yaw), 0.08f, std::sin(yaw)};
                bar(mesh, st_.roof, c - d, c + d, 1.0f, 0.02f);
            }
        for (int i = 0; i < 20; ++i) {
            const float side = rng_.chance(0.5f) ? 1.0f : -1.0f;
            const float u = rng_.range(-L_ * 0.48f, L_ * 0.48f), v = side * (W_ * 0.5f - rng_.range(0.4f, 1.6f));
            const float sz = rng_.range(0.12f, 0.3f), yaw = rng_.range(0.0f, 6.2831853f);
            orientedBox(mesh, st_.wall, P(u, v, sz * 0.4f), {std::cos(yaw), 0, std::sin(yaw)}, {0, 1, 0}, {-std::sin(yaw), 0, std::cos(yaw)},
                        {sz, sz * 0.45f, sz * 0.6f});
        }
    }

    const BuildingSpec& b_;
    const FacadeStyle& st_;
    int lod_;
    Rng rng_;
    glm::vec2 c_{0.0f}, ax_{1, 0}, ay_{0, 1};
    float L_ = 30.0f, W_ = 18.0f, He_ = 8.0f, rise_ = 3.0f;
    std::vector<float> frames_;
    std::vector<glm::vec2> holes_;
    bool gallery_ = false;
    int gate_ = 1;
};

// ---------------------------------------------------------------- glb
std::string jsonEscape(const std::string& s)
{
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\')
            o += '\\';
        o += c;
    }
    return o;
}

std::string num(float v)
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << v;
    return o.str();
}

bool writeGlb(const Mesh& mesh, const fs::path& file, const std::string& texRel, const std::string& name, std::string* error)
{
    std::string bin;
    std::ostringstream views, accessors, prims, materials, textures, images;
    views.imbue(std::locale::classic());
    accessors.imbue(std::locale::classic());
    prims.imbue(std::locale::classic());
    int nViews = 0, nAcc = 0, nMat = 0, nTex = 0;
    std::map<std::string, int> texIndex;
    auto texOf = [&](const std::string& f) {
        auto it = texIndex.find(f);
        if (it != texIndex.end())
            return it->second;
        if (nTex) {
            textures << ',';
            images << ',';
        }
        images << "{\"uri\":\"" << jsonEscape(texRel + "/" + f) << "\"}";
        textures << "{\"source\":" << nTex << ",\"sampler\":0}";
        texIndex[f] = nTex;
        return nTex++;
    };
    auto addView = [&](const void* data, std::size_t bytes, int target) {
        while (bin.size() % 4)
            bin.push_back('\0');
        if (nViews)
            views << ',';
        views << "{\"buffer\":0,\"byteOffset\":" << bin.size() << ",\"byteLength\":" << bytes << ",\"target\":" << target << '}';
        bin.append(static_cast<const char*>(data), bytes);
        return nViews++;
    };
    bool firstPrim = true;
    for (const auto& [mname, P] : mesh.prims) {
        if (P.idx.empty())
            continue;
        const MatDef& m = mat(mname);
        // material
        if (nMat)
            materials << ',';
        materials << "{\"name\":\"" << jsonEscape(mname) << "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[" << num(m.r) << ','
                  << num(m.g) << ',' << num(m.b) << ',' << num(m.alpha) << "],\"metallicFactor\":" << num(m.metal)
                  << ",\"roughnessFactor\":" << num(m.rough);
        if (m.tex && m.decal) {
            materials << ",\"baseColorTexture\":{\"index\":" << texOf(std::string(m.tex) + "_albedo.png") << "}}";
            materials << ",\"alphaMode\":\"BLEND\"";
        } else if (m.tex) {
            const int a = texOf(std::string(m.tex) + "_albedo.jpg");
            const int orm = texOf(std::string(m.tex) + "_orm.jpg");
            const int nrm = texOf(std::string(m.tex) + "_normal.jpg");
            materials << ",\"baseColorTexture\":{\"index\":" << a << "},\"metallicRoughnessTexture\":{\"index\":" << orm << "}}";
            materials << ",\"normalTexture\":{\"index\":" << nrm << "},\"occlusionTexture\":{\"index\":" << orm << '}';
        } else {
            materials << '}';
        }
        if (m.alpha < 1.0f && !m.decal)
            materials << ",\"alphaMode\":\"BLEND\",\"doubleSided\":true";
        materials << '}';
        const int matIndex = nMat++;
        // vertex data
        glm::vec3 lo{1e30f}, hi{-1e30f};
        for (const auto& p : P.p) {
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        const int pv = addView(P.p.data(), P.p.size() * sizeof(glm::vec3), 34962);
        const int nv = addView(P.n.data(), P.n.size() * sizeof(glm::vec3), 34962);
        const int tv = addView(P.uv.data(), P.uv.size() * sizeof(glm::vec2), 34962);
        const int iv = addView(P.idx.data(), P.idx.size() * sizeof(std::uint32_t), 34963);
        if (nAcc)
            accessors << ',';
        accessors << "{\"bufferView\":" << pv << ",\"componentType\":5126,\"count\":" << P.p.size() << ",\"type\":\"VEC3\",\"min\":[" << num(lo.x)
                  << ',' << num(lo.y) << ',' << num(lo.z) << "],\"max\":[" << num(hi.x) << ',' << num(hi.y) << ',' << num(hi.z) << "]}";
        accessors << ",{\"bufferView\":" << nv << ",\"componentType\":5126,\"count\":" << P.n.size() << ",\"type\":\"VEC3\"}";
        accessors << ",{\"bufferView\":" << tv << ",\"componentType\":5126,\"count\":" << P.uv.size() << ",\"type\":\"VEC2\"}";
        accessors << ",{\"bufferView\":" << iv << ",\"componentType\":5125,\"count\":" << P.idx.size() << ",\"type\":\"SCALAR\"}";
        if (!firstPrim)
            prims << ',';
        firstPrim = false;
        prims << "{\"attributes\":{\"POSITION\":" << nAcc << ",\"NORMAL\":" << nAcc + 1 << ",\"TEXCOORD_0\":" << nAcc + 2
              << "},\"indices\":" << nAcc + 3 << ",\"material\":" << matIndex << '}';
        nAcc += 4;
    }
    while (bin.size() % 4)
        bin.push_back('\0');
    std::ostringstream js;
    js.imbue(std::locale::classic());
    js << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"FPS engine city grammar (CC0)\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
       << "\"nodes\":[{\"mesh\":0,\"name\":\"" << jsonEscape(name) << "\"}],\"meshes\":[{\"name\":\"" << jsonEscape(name)
       << "\",\"primitives\":[" << prims.str() << "]}],\"materials\":[" << materials.str() << "],\"buffers\":[{\"byteLength\":" << bin.size()
       << "}],\"bufferViews\":[" << views.str() << "],\"accessors\":[" << accessors.str() << ']';
    if (nTex)
        js << ",\"textures\":[" << textures.str() << "],\"images\":[" << images.str()
           << "],\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}]";
    js << '}';
    std::string json = js.str();
    while (json.size() % 4)
        json.push_back(' ');
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) {
            if (error)
                *error = "cannot write " + file.string();
            return false;
        }
        auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
        u32(0x46546C67u);
        u32(2u);
        u32(static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
        u32(static_cast<std::uint32_t>(json.size()));
        u32(0x4E4F534Au);
        out.write(json.data(), static_cast<std::streamsize>(json.size()));
        u32(static_cast<std::uint32_t>(bin.size()));
        u32(0x004E4942u);
        out.write(bin.data(), static_cast<std::streamsize>(bin.size()));
        if (!out) {
            if (error)
                *error = "cannot write " + file.string();
            return false;
        }
    }
    fs::rename(tmp, file, ec); // atomic: parallel writers of the same house never leave half a file
    if (ec) {
        fs::remove(tmp, ec);
        return fs::exists(file);
    }
    return true;
}

} // namespace

std::vector<BalconySpot> balconySpots(const BuildingSpec& spec)
{
    if (spec.style.role == "hall" || isSkeleton(spec.style) || spec.footprint.size() < 3)
        return {};
    BuildingSpec b = spec;
    if (signedArea(b.footprint) < 0.0f) { // as writeBuildingGlb does
        std::reverse(b.footprint.begin(), b.footprint.end());
        std::vector<EdgeRole> e(b.edges.size());
        const std::size_t n = b.footprint.size();
        for (std::size_t i = 0; i < n && n == b.edges.size(); ++i)
            e[i] = b.edges[(2 * n - 2 - i) % n];
        b.edges = e;
    }
    Builder bld(b, 0);
    bld.run();
    return std::move(bld.balconies_);
}

std::vector<LadderSpot> ladderSpots(const BuildingSpec& spec)
{
    if (spec.style.role == "hall" || isSkeleton(spec.style) || spec.footprint.size() < 3 || spec.style.fireEscapes <= 0.0f)
        return {};
    BuildingSpec b = spec;
    if (signedArea(b.footprint) < 0.0f) { // as writeBuildingGlb does
        std::reverse(b.footprint.begin(), b.footprint.end());
        std::vector<EdgeRole> e(b.edges.size());
        const std::size_t n = b.footprint.size();
        for (std::size_t i = 0; i < n && n == b.edges.size(); ++i)
            e[i] = b.edges[(2 * n - 2 - i) % n];
        b.edges = e;
    }
    Builder bld(b, 0);
    bld.run();
    return std::move(bld.ladders_);
}

const std::vector<std::string>& grammarMaterials()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> v;
        for (const auto& m : kMats)
            if (std::strcmp(m.name, "glass") != 0 && std::strcmp(m.name, "grime") != 0 && std::strcmp(m.name, "shop_signs") != 0)
                v.push_back(m.name);
        return v;
    }();
    return names;
}

float buildingHeight(const BuildingSpec& b)
{
    const int f = std::max(1, b.floors);
    return b.style.groundStorey + static_cast<float>(f - 1) * b.style.storey;
}

std::string buildingKey(const BuildingSpec& b)
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    const FacadeStyle& s = b.style;
    o << (s.role == "hall" ? "hall1|" : isSkeleton(s) ? "sk3|" : "v3|") << s.role << '|' << s.groundStorey << '|' << s.storey << '|' << s.bay << '|' << s.windowWidth << '|' << s.windowHeight << '|'
      << s.sill << '|' << s.wall << '|' << s.plinth << '|' << s.trim << '|' << s.roof << '|' << s.firewall << '|' << s.cornice << '|'
      << s.bands << '|' << s.loggias << '|' << s.balconies << '|' << s.shops << '|' << s.pitched << '|' << b.floors << '|' << b.seed << '|'
      << b.ruin;
    if (s.arcade > 0.0f)
        o << "|arc" << s.arcade;
    if (s.fireEscapes > 0.0f)
        o << "|fe" << s.fireEscapes << "|dl2"; // dl2: drop ladder at the free end, some let down (2026-11-03)
    if (s.shops > 0.0f || s.arcade > 0.0f)
        o << "|sg2"; // shop signs over the shop windows
    if (b.interior && !b.ruin)
        o << "|in1"; // enterable
    for (const auto& p : b.footprint)
        o << '|' << std::lround(p.x * 100.0f) << ',' << std::lround(p.y * 100.0f);
    for (const auto e : b.edges)
        o << '|' << static_cast<int>(e);
    const std::string s2 = o.str();
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s2) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

bool writeBuildingGlb(const BuildingSpec& b, const fs::path& file, int lod, const fs::path& textureDir, std::string* error, GrammarStats* stats)
{
    BuildingSpec spec = b;
    if (signedArea(spec.footprint) < 0.0f) {
        std::reverse(spec.footprint.begin(), spec.footprint.end());
        // edge i of the reversed ring is old edge n - 2 - i
        std::vector<EdgeRole> e(spec.edges.size());
        const std::size_t n = spec.footprint.size();
        for (std::size_t i = 0; i < n && n == spec.edges.size(); ++i)
            e[i] = spec.edges[(2 * n - 2 - i) % n];
        spec.edges = e;
    }
    Mesh mesh;
    int windows = 0;
    if (spec.style.role == "hall") {
        HallBuilder hb(spec, lod);
        hb.run();
        mesh = std::move(hb.mesh);
    } else if (isSkeleton(spec.style)) {
        SkeletonBuilder sk(spec, lod);
        sk.run();
        mesh = std::move(sk.mesh);
    } else {
        Builder bld(spec, lod);
        bld.run();
        mesh = std::move(bld.mesh);
        windows = bld.windows;
    }
    std::error_code ec;
    std::string rel = fs::relative(textureDir, file.parent_path(), ec).generic_string();
    if (ec || rel.empty())
        rel = textureDir.generic_string();
    if (stats) {
        stats->triangles = mesh.triangles();
        stats->windows = windows;
    }
    return writeGlb(mesh, file, rel, b.style.name, error);
}

} // namespace engine::city
