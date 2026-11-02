#include "engine/CityRoads.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <unordered_map>

namespace engine::city {

namespace {

constexpr float kPi = 3.14159265358979f;

float cross2(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

// ---------------------------------------------------------------- deterministic noise
std::uint32_t hash32(std::uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float lattice(int x, int y, std::uint32_t seed)
{
    const std::uint32_t h = hash32(static_cast<std::uint32_t>(x) * 0x8da6b343U ^ static_cast<std::uint32_t>(y) * 0xd8163841U ^ seed * 0xcb1ab31fU);
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}
float valueNoise(glm::vec2 p, std::uint32_t seed)
{
    const float fx = std::floor(p.x), fy = std::floor(p.y);
    const int x = static_cast<int>(fx), y = static_cast<int>(fy);
    float u = p.x - fx, v = p.y - fy;
    u = u * u * (3.0f - 2.0f * u);
    v = v * v * (3.0f - 2.0f * v);
    const float a = lattice(x, y, seed), b = lattice(x + 1, y, seed), c = lattice(x, y + 1, seed), d = lattice(x + 1, y + 1, seed);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}
float fbm(glm::vec2 p, std::uint32_t seed)
{
    return 0.65f * valueNoise(p, seed) + 0.35f * valueNoise(p * 2.03f + glm::vec2(17.1f, 3.7f), seed + 1);
}

struct SplitMix {
    std::uint64_t s;
    explicit SplitMix(std::uint64_t seed) : s(seed) {}
    std::uint64_t next()
    {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float uniform() { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * uniform(); }
};

// ---------------------------------------------------------------- the tensor field
struct Basis {
    bool radial = false;
    glm::vec2 c{0.0f};
    float theta = 0.0f;
    float decay = 100.0f;
    float w = 1.0f;
};

struct Field {
    explicit Field(const NetworkSettings& s) : s_(s)
    {
        SplitMix rng(static_cast<std::uint64_t>(s.seed) * 7919u + 17u);
        const float a = s.angle * kPi / 180.0f;
        bases_.push_back({false, {0.0f, 0.0f}, a, s.size * 0.9f, 1.0f});
        const int n = std::max(1, s.districts);
        for (int i = 1; i < n; ++i) {
            const float ang = 2.0f * kPi * static_cast<float>(i - 1) / static_cast<float>(std::max(1, n - 1)) + rng.range(-0.6f, 0.6f);
            const float r = s.size * rng.range(0.28f, 0.42f);
            const float turn = s.districtTurn * kPi / 180.0f * rng.range(-1.0f, 1.0f);
            bases_.push_back({false, {std::cos(ang) * r, std::sin(ang) * r}, a + turn, s.size * 0.24f, 1.6f});
        }
        if (s.radial > 0.001f)
            bases_.push_back({true, {0.0f, 0.0f}, 0.0f, s.size * 0.3f, s.radial * 1.6f});
    }

    // Major direction (unit).
    glm::vec2 major(glm::vec2 p) const
    {
        glm::vec2 t{0.0f};
        for (const auto& b : bases_) {
            const glm::vec2 d = p - b.c;
            const float wgt = b.w * std::exp(-glm::dot(d, d) / (b.decay * b.decay));
            float th = b.theta;
            float w = wgt;
            if (b.radial) {
                const float r = glm::length(d);
                th = std::atan2(d.y, d.x) + kPi * 0.5f; // rings around the centre are the major roads
                w *= std::clamp(r / 40.0f, 0.0f, 1.0f);
            }
            t += w * glm::vec2(std::cos(2.0f * th), std::sin(2.0f * th));
        }
        float th = 0.5f * std::atan2(t.y, t.x);
        if (s_.curvature > 0.0f)
            th += s_.curvature * (fbm(p / std::max(10.0f, s_.noiseScale), s_.seed) * 2.0f - 1.0f) * 1.6f;
        return {std::cos(th), std::sin(th)};
    }
    glm::vec2 dir(glm::vec2 p, bool minor) const
    {
        const glm::vec2 m = major(p);
        return minor ? glm::vec2(-m.y, m.x) : m;
    }

    const NetworkSettings& s_;
    std::vector<Basis> bases_;
};

// ---------------------------------------------------------------- point hash for spacing tests
struct PointGrid {
    float cell;
    std::unordered_map<std::uint64_t, std::vector<std::pair<glm::vec2, int>>> cells;
    explicit PointGrid(float c) : cell(c) {}
    static std::uint64_t key(int x, int y) { return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(y); }
    void add(glm::vec2 p, int id)
    {
        cells[key(static_cast<int>(std::floor(p.x / cell)), static_cast<int>(std::floor(p.y / cell)))].push_back({p, id});
    }
    bool near(glm::vec2 p, float r, int ignore = -1) const
    {
        const int x0 = static_cast<int>(std::floor((p.x - r) / cell)), x1 = static_cast<int>(std::floor((p.x + r) / cell));
        const int y0 = static_cast<int>(std::floor((p.y - r) / cell)), y1 = static_cast<int>(std::floor((p.y + r) / cell));
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y) {
                const auto it = cells.find(key(x, y));
                if (it == cells.end())
                    continue;
                for (const auto& [q, id] : it->second)
                    if (id != ignore && glm::dot(q - p, q - p) < r * r)
                        return true;
            }
        return false;
    }
};

struct Line {
    std::vector<glm::vec2> pts;
    int cls = 2;
    bool minorDir = false;  // along the minor eigenvector
    bool side = false;      // side street (else main road)
    bool endAtEdge[2] = {false, false}; // front, back stopped on the town edge
};

bool inside(glm::vec2 p, float half) { return std::abs(p.x) <= half && std::abs(p.y) <= half; }

// Clips the segment a->b (a inside) to the square: the point on the edge.
glm::vec2 clipToSquare(glm::vec2 a, glm::vec2 b, float half)
{
    float t = 1.0f;
    const glm::vec2 d = b - a;
    for (int k = 0; k < 2; ++k) {
        if (d[k] > 1e-6f)
            t = std::min(t, (half - a[k]) / d[k]);
        else if (d[k] < -1e-6f)
            t = std::min(t, (-half - a[k]) / d[k]);
    }
    return a + d * std::clamp(t, 0.0f, 1.0f);
}

bool segmentHit(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d, float& t, float& u)
{
    const glm::vec2 r = b - a, s = d - c;
    const float den = cross2(r, s);
    if (std::abs(den) < 1e-9f)
        return false;
    t = cross2(c - a, s) / den;
    u = cross2(c - a, r) / den;
    return t >= -1e-4f && t <= 1.0f + 1e-4f && u >= -1e-4f && u <= 1.0f + 1e-4f;
}

class Tracer {
public:
    Tracer(const NetworkSettings& s, const Field& f)
        : s_(s), f_(f), half_(s.size * 0.5f), grid_{PointGrid(s.minorSpacing * 0.5f), PointGrid(s.minorSpacing * 0.5f)}
    {
    }

    std::vector<Line> lines;

    // Main roads along both eigenvectors first, then the side streets between them.
    void run()
    {
        family(false, false);
        family(false, true);
        family(true, false);
        family(true, true);
    }

private:
    PointGrid& grid(bool minorDir) { return grid_[minorDir ? 1 : 0]; }

    bool trace(glm::vec2 seed, bool minorDir, bool side, Line& out)
    {
        const float dsep = side ? s_.minorSpacing : s_.majorSpacing;
        const float dtest = dsep * 0.5f;
        const float h = std::max(0.5f, s_.step);
        const int maxSteps = static_cast<int>(s_.size * 3.0f / h);
        std::vector<glm::vec2> half[2];
        bool edge[2] = {false, false};
        for (int dirSide = 0; dirSide < 2; ++dirSide) {
            glm::vec2 p = seed;
            glm::vec2 prev = f_.dir(seed, minorDir) * (dirSide == 0 ? 1.0f : -1.0f);
            auto& H = half[dirSide];
            for (int i = 0; i < maxSteps; ++i) {
                auto aligned = [&](glm::vec2 q, glm::vec2 ref) {
                    glm::vec2 d = f_.dir(q, minorDir);
                    return glm::dot(d, ref) < 0.0f ? -d : d;
                };
                const glm::vec2 d1 = aligned(p, prev);
                const glm::vec2 d2 = aligned(p + d1 * (h * 0.5f), d1);
                const glm::vec2 q = p + d2 * h;
                if (!inside(q, half_)) {
                    H.push_back(clipToSquare(p, q, half_));
                    edge[dirSide] = true;
                    break;
                }
                if (grid(minorDir).near(q, dtest))
                    break;
                // Closed ring (radial field): back at the start.
                const float len = static_cast<float>(H.size()) * h;
                if (len > 4.0f * dsep && glm::length(q - seed) < h * 1.5f) {
                    H.push_back(seed);
                    break;
                }
                // Spiralling into itself.
                bool self = false;
                const int skip = static_cast<int>(dsep / h) * 2;
                for (int k = 0; k + skip < static_cast<int>(H.size()); k += 2)
                    if (glm::length(H[static_cast<std::size_t>(k)] - q) < dtest * 0.6f) {
                        self = true;
                        break;
                    }
                if (self)
                    break;
                H.push_back(q);
                prev = d2;
                p = q;
            }
        }
        out.pts.clear();
        for (auto it = half[1].rbegin(); it != half[1].rend(); ++it)
            out.pts.push_back(*it);
        out.pts.push_back(seed);
        for (const auto& q : half[0])
            out.pts.push_back(q);
        out.minorDir = minorDir;
        out.side = side;
        out.cls = side ? s_.minorClass : s_.majorClass;
        out.endAtEdge[0] = edge[1];
        out.endAtEdge[1] = edge[0];
        float L = 0.0f;
        for (std::size_t i = 1; i < out.pts.size(); ++i)
            L += glm::length(out.pts[i] - out.pts[i - 1]);
        return L >= dsep * 0.6f;
    }

    void accept(Line&& l, std::vector<glm::vec2>& queue, float dsep)
    {
        const int id = static_cast<int>(lines.size());
        float acc = 0.0f;
        for (std::size_t i = 0; i < l.pts.size(); ++i) {
            grid(l.minorDir).add(l.pts[i], id);
            if (i > 0)
                acc += glm::length(l.pts[i] - l.pts[i - 1]);
            if (i > 0 && acc >= dsep * 0.5f) {
                acc = 0.0f;
                const glm::vec2 t = glm::normalize(l.pts[i] - l.pts[i - 1]);
                const glm::vec2 n{-t.y, t.x};
                queue.push_back(l.pts[i] + n * dsep);
                queue.push_back(l.pts[i] - n * dsep);
            }
        }
        lines.push_back(std::move(l));
    }

    void family(bool side, bool minorDir)
    {
        const float dsep = side ? s_.minorSpacing : s_.majorSpacing;
        std::vector<glm::vec2> queue;
        if (lines.empty())
            queue.push_back({0.0f, 0.0f});
        // Cross streets start on the roads already there (T and X junctions).
        for (const auto& l : lines) {
            if (l.minorDir == minorDir)
                continue;
            float acc = dsep * 0.5f;
            for (std::size_t i = 1; i < l.pts.size(); ++i) {
                acc += glm::length(l.pts[i] - l.pts[i - 1]);
                if (acc >= dsep) {
                    acc = 0.0f;
                    queue.push_back(l.pts[i]);
                }
            }
        }
        // Fallback seeds over the whole square, so no district stays empty.
        std::vector<glm::vec2> fill;
        for (float y = -half_ + dsep * 0.5f; y < half_; y += dsep)
            for (float x = -half_ + dsep * 0.5f; x < half_; x += dsep)
                fill.push_back({x, y});
        std::sort(fill.begin(), fill.end(), [](glm::vec2 a, glm::vec2 b) { return glm::dot(a, a) < glm::dot(b, b); });
        std::size_t qi = 0, fi = 0;
        int guard = 0;
        while (guard++ < 20000) {
            glm::vec2 seed;
            if (qi < queue.size())
                seed = queue[qi++];
            else if (fi < fill.size())
                seed = fill[fi++];
            else
                break;
            if (!inside(seed, half_ - 2.0f) || grid(minorDir).near(seed, dsep * 0.75f))
                continue;
            Line l;
            if (trace(seed, minorDir, side, l))
                accept(std::move(l), queue, dsep);
        }
    }

    const NetworkSettings& s_;
    const Field& f_;
    float half_;
    PointGrid grid_[2];
};

// ---------------------------------------------------------------- graph
struct SegRef {
    int line;
    int seg;
};

class SegmentGrid {
public:
    explicit SegmentGrid(float cell) : cell_(cell) {}
    void add(glm::vec2 a, glm::vec2 b, SegRef r)
    {
        const int x0 = cellOf(std::min(a.x, b.x)), x1 = cellOf(std::max(a.x, b.x));
        const int y0 = cellOf(std::min(a.y, b.y)), y1 = cellOf(std::max(a.y, b.y));
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                cells_[PointGrid::key(x, y)].push_back(r);
    }
    template <class F>
    void query(glm::vec2 a, glm::vec2 b, F&& f) const
    {
        const int x0 = cellOf(std::min(a.x, b.x)), x1 = cellOf(std::max(a.x, b.x));
        const int y0 = cellOf(std::min(a.y, b.y)), y1 = cellOf(std::max(a.y, b.y));
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y) {
                const auto it = cells_.find(PointGrid::key(x, y));
                if (it != cells_.end())
                    for (const auto& r : it->second)
                        f(r);
            }
    }

private:
    int cellOf(float v) const { return static_cast<int>(std::floor(v / cell_)); }
    float cell_;
    std::unordered_map<std::uint64_t, std::vector<SegRef>> cells_;
};

// Carries ends that stopped short (spacing test) on to the next street, if one is near.
void extendEnds(std::vector<Line>& lines, const NetworkSettings& s)
{
    SegmentGrid grid(20.0f);
    for (int li = 0; li < static_cast<int>(lines.size()); ++li)
        for (int k = 0; k + 1 < static_cast<int>(lines[static_cast<std::size_t>(li)].pts.size()); ++k)
            grid.add(lines[static_cast<std::size_t>(li)].pts[static_cast<std::size_t>(k)], lines[static_cast<std::size_t>(li)].pts[static_cast<std::size_t>(k + 1)], {li, k});
    std::vector<std::pair<int, glm::vec2>> appendFront, appendBack;
    for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
        const Line& l = lines[static_cast<std::size_t>(li)];
        if (l.pts.size() < 2)
            continue;
        const float reach = s.join * (l.side ? s.minorSpacing : s.majorSpacing);
        for (int end = 0; end < 2; ++end) {
            if (l.endAtEdge[end])
                continue;
            const glm::vec2 p = end == 0 ? l.pts.front() : l.pts.back();
            const glm::vec2 q = end == 0 ? l.pts[1] : l.pts[l.pts.size() - 2];
            if (glm::length(p - q) < 1e-4f)
                continue;
            const glm::vec2 d = glm::normalize(p - q);
            const glm::vec2 far = p + d * reach;
            float best = 2.0f;
            grid.query(p, far, [&](SegRef r) {
                if (r.line == li)
                    return;
                const auto& o = lines[static_cast<std::size_t>(r.line)].pts;
                float t, u;
                if (segmentHit(p, far, o[static_cast<std::size_t>(r.seg)], o[static_cast<std::size_t>(r.seg + 1)], t, u) && t > 1e-3f && t < best)
                    best = t;
            });
            const float half = s.size * 0.5f;
            if (best > 1.0f) { // nothing near: maybe the town edge
                const glm::vec2 c = clipToSquare(p, far, half);
                if (glm::length(c - far) > 1e-3f)
                    best = glm::length(c - p) / reach;
            }
            if (best <= 1.0f) {
                const glm::vec2 hit = p + (far - p) * best + d * 0.05f;
                (end == 0 ? appendFront : appendBack).push_back({li, hit});
            }
        }
    }
    for (const auto& [li, p] : appendFront)
        lines[static_cast<std::size_t>(li)].pts.insert(lines[static_cast<std::size_t>(li)].pts.begin(), p);
    for (const auto& [li, p] : appendBack)
        lines[static_cast<std::size_t>(li)].pts.push_back(p);
}

struct GEdge {
    int a = -1, b = -1;
    std::vector<glm::vec2> pts; // a ... b
    int cls = 2;
    bool alive = true;
};

std::vector<glm::vec2> simplifyOpen(const std::vector<glm::vec2>& p, float tol)
{
    if (p.size() < 3)
        return p;
    std::vector<char> keep(p.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<std::size_t, std::size_t>> stack{{0, p.size() - 1}};
    while (!stack.empty()) {
        const auto [i, j] = stack.back();
        stack.pop_back();
        float worst = 0.0f;
        std::size_t at = i;
        const glm::vec2 a = p[i], b = p[j];
        const glm::vec2 ab = b - a;
        const float L2 = glm::dot(ab, ab);
        for (std::size_t k = i + 1; k < j; ++k) {
            const float t = L2 > 0.0f ? std::clamp(glm::dot(p[k] - a, ab) / L2, 0.0f, 1.0f) : 0.0f;
            const float d = glm::length(p[k] - (a + ab * t));
            if (d > worst) {
                worst = d;
                at = k;
            }
        }
        if (worst > tol) {
            keep[at] = 1;
            stack.push_back({i, at});
            stack.push_back({at, j});
        }
    }
    std::vector<glm::vec2> out;
    for (std::size_t k = 0; k < p.size(); ++k)
        if (keep[k])
            out.push_back(p[k]);
    return out;
}

} // namespace

glm::vec2 fieldDirection(const NetworkSettings& s, glm::vec2 p)
{
    Field f(s);
    return f.major(p);
}

RoadNetwork buildRoadNetwork(const NetworkSettings& sIn)
{
    NetworkSettings s = sIn;
    s.size = std::max(60.0f, s.size);
    s.majorSpacing = std::max(20.0f, s.majorSpacing);
    s.minorSpacing = std::clamp(s.minorSpacing, 15.0f, s.majorSpacing);
    s.step = std::clamp(s.step, 0.5f, 10.0f);
    const float half = s.size * 0.5f;

    Field field(s);
    Tracer tracer(s, field);
    tracer.run();
    std::vector<Line> lines = std::move(tracer.lines);
    extendEnds(lines, s);
    for (auto& l : lines)
        l.pts = simplifyOpen(l.pts, 0.15f);
    // The town edge closes the outer blocks.
    {
        Line b;
        b.pts = {{-half, -half}, {half, -half}, {half, half}, {-half, half}, {-half, -half}};
        b.cls = s.boundaryClass;
        lines.push_back(b);
    }

    // Split points: every crossing of two lines, and the line ends.
    struct Split {
        float at; // segment index + t
        glm::vec2 p;
        int node = -1;
    };
    std::vector<std::vector<Split>> splits(lines.size());
    SegmentGrid grid(20.0f);
    for (int li = 0; li < static_cast<int>(lines.size()); ++li)
        for (int k = 0; k + 1 < static_cast<int>(lines[static_cast<std::size_t>(li)].pts.size()); ++k)
            grid.add(lines[static_cast<std::size_t>(li)].pts[static_cast<std::size_t>(k)], lines[static_cast<std::size_t>(li)].pts[static_cast<std::size_t>(k + 1)], {li, k});
    for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
        const auto& P = lines[static_cast<std::size_t>(li)].pts;
        splits[static_cast<std::size_t>(li)].push_back({0.0f, P.front()});
        splits[static_cast<std::size_t>(li)].push_back({static_cast<float>(P.size() - 1), P.back()});
        for (int k = 0; k + 1 < static_cast<int>(P.size()); ++k)
            grid.query(P[static_cast<std::size_t>(k)], P[static_cast<std::size_t>(k + 1)], [&](SegRef r) {
                if (r.line <= li)
                    return; // each pair once (and no self crossings)
                const auto& Q = lines[static_cast<std::size_t>(r.line)].pts;
                float t, u;
                if (segmentHit(P[static_cast<std::size_t>(k)], P[static_cast<std::size_t>(k + 1)], Q[static_cast<std::size_t>(r.seg)], Q[static_cast<std::size_t>(r.seg + 1)], t, u)) {
                    t = std::clamp(t, 0.0f, 1.0f);
                    u = std::clamp(u, 0.0f, 1.0f);
                    const glm::vec2 p = P[static_cast<std::size_t>(k)] + (P[static_cast<std::size_t>(k + 1)] - P[static_cast<std::size_t>(k)]) * t;
                    splits[static_cast<std::size_t>(li)].push_back({static_cast<float>(k) + t, p});
                    splits[static_cast<std::size_t>(r.line)].push_back({static_cast<float>(r.seg) + u, p});
                }
            });
    }
    // Nodes: split points closer than 1.5 m are one junction.
    std::vector<glm::vec2> nodes;
    PointGrid nodeGrid(4.0f);
    auto nodeAt = [&](glm::vec2 p) {
        const float r = 1.5f;
        const int x0 = static_cast<int>(std::floor((p.x - r) / 4.0f)), x1 = static_cast<int>(std::floor((p.x + r) / 4.0f));
        const int y0 = static_cast<int>(std::floor((p.y - r) / 4.0f)), y1 = static_cast<int>(std::floor((p.y + r) / 4.0f));
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y) {
                const auto it = nodeGrid.cells.find(PointGrid::key(x, y));
                if (it == nodeGrid.cells.end())
                    continue;
                for (const auto& [q, id] : it->second)
                    if (glm::length(q - p) < r)
                        return id;
            }
        const int id = static_cast<int>(nodes.size());
        nodes.push_back(p);
        nodeGrid.add(p, id);
        return id;
    };
    std::vector<GEdge> edges;
    std::map<std::pair<int, int>, std::vector<int>> byPair;
    for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
        auto& sp = splits[static_cast<std::size_t>(li)];
        std::sort(sp.begin(), sp.end(), [](const Split& a, const Split& b) { return a.at < b.at; });
        for (auto& x : sp)
            x.node = nodeAt(x.p);
        const auto& P = lines[static_cast<std::size_t>(li)].pts;
        for (std::size_t i = 0; i + 1 < sp.size(); ++i) {
            const Split& A = sp[i];
            const Split& B = sp[i + 1];
            if (A.node == B.node)
                continue;
            GEdge e;
            e.a = A.node;
            e.b = B.node;
            e.cls = lines[static_cast<std::size_t>(li)].cls;
            e.pts.push_back(nodes[static_cast<std::size_t>(A.node)]);
            for (int k = static_cast<int>(std::floor(A.at)) + 1; k <= static_cast<int>(std::ceil(B.at)) - 1; ++k)
                if (static_cast<float>(k) > A.at + 1e-3f && static_cast<float>(k) < B.at - 1e-3f)
                    e.pts.push_back(P[static_cast<std::size_t>(k)]);
            e.pts.push_back(nodes[static_cast<std::size_t>(B.node)]);
            // Outside the town (tails past the edge) or a duplicate of an existing edge.
            const glm::vec2 mid = e.pts[e.pts.size() / 2] * 0.5f + e.pts[(e.pts.size() - 1) / 2] * 0.5f;
            if (!inside(mid, half + 0.5f))
                continue;
            const auto key = std::minmax(e.a, e.b);
            bool dup = false;
            for (int other : byPair[key]) {
                const auto& o = edges[static_cast<std::size_t>(other)];
                if (std::abs(static_cast<float>(o.pts.size()) - static_cast<float>(e.pts.size())) < 3 &&
                    glm::length(o.pts[o.pts.size() / 2] - e.pts[e.a == o.a ? e.pts.size() / 2 : (e.pts.size() - 1) - e.pts.size() / 2]) < 3.0f)
                    dup = true;
            }
            if (dup)
                continue;
            byPair[key].push_back(static_cast<int>(edges.size()));
            edges.push_back(std::move(e));
        }
    }
    // Prune dead ends.
    for (bool changed = true; changed;) {
        changed = false;
        std::vector<int> degree(nodes.size(), 0);
        for (const auto& e : edges)
            if (e.alive) {
                ++degree[static_cast<std::size_t>(e.a)];
                ++degree[static_cast<std::size_t>(e.b)];
            }
        for (auto& e : edges)
            if (e.alive && (degree[static_cast<std::size_t>(e.a)] < 2 || degree[static_cast<std::size_t>(e.b)] < 2)) {
                e.alive = false;
                changed = true;
            }
    }

    // Faces: half-edges sorted by angle around each node.
    std::vector<std::vector<std::pair<float, int>>> around(nodes.size()); // angle, half id (edge * 2 + reversed)
    auto halfFrom = [&](int h) { const auto& e = edges[static_cast<std::size_t>(h / 2)]; return (h & 1) ? e.b : e.a; };
    auto halfTo = [&](int h) { const auto& e = edges[static_cast<std::size_t>(h / 2)]; return (h & 1) ? e.a : e.b; };
    for (int ei = 0; ei < static_cast<int>(edges.size()); ++ei) {
        const auto& e = edges[static_cast<std::size_t>(ei)];
        if (!e.alive)
            continue;
        const glm::vec2 da = e.pts[1] - e.pts[0];
        const glm::vec2 db = e.pts[e.pts.size() - 2] - e.pts.back();
        around[static_cast<std::size_t>(e.a)].push_back({std::atan2(da.y, da.x), ei * 2});
        around[static_cast<std::size_t>(e.b)].push_back({std::atan2(db.y, db.x), ei * 2 + 1});
    }
    for (auto& a : around)
        std::sort(a.begin(), a.end());
    std::vector<int> posInNode(edges.size() * 2, -1);
    for (auto& a : around)
        for (int i = 0; i < static_cast<int>(a.size()); ++i)
            posInNode[static_cast<std::size_t>(a[static_cast<std::size_t>(i)].second)] = i;
    std::vector<char> used(edges.size() * 2, 0);
    RoadNetwork net;
    const float town = s.size * s.size;
    for (int h0 = 0; h0 < static_cast<int>(edges.size() * 2); ++h0) {
        if (!edges[static_cast<std::size_t>(h0 / 2)].alive || used[static_cast<std::size_t>(h0)])
            continue;
        Block b;
        int h = h0;
        int guard = 0;
        bool ok = true;
        while (!used[static_cast<std::size_t>(h)]) {
            if (++guard > 4000) {
                ok = false;
                break;
            }
            used[static_cast<std::size_t>(h)] = 1;
            const auto& e = edges[static_cast<std::size_t>(h / 2)];
            const int n = static_cast<int>(e.pts.size());
            for (int k = 0; k < n - 1; ++k) {
                b.outline.push_back((h & 1) ? e.pts[static_cast<std::size_t>(n - 1 - k)] : e.pts[static_cast<std::size_t>(k)]);
                b.edgeClass.push_back(e.cls);
            }
            const int v = halfTo(h);
            const int twin = h ^ 1;
            const auto& list = around[static_cast<std::size_t>(v)];
            const int idx = posInNode[static_cast<std::size_t>(twin)];
            const int m = static_cast<int>(list.size());
            h = list[static_cast<std::size_t>((idx - 1 + m) % m)].second;
        }
        (void)halfFrom;
        if (!ok || h != h0 || b.outline.size() < 3)
            continue;
        const float A = signedArea(b.outline);
        if (A < 150.0f || A > town * 0.95f) {
            if (A > 150.0f && std::getenv("CITY_DEBUG"))
                std::fprintf(stderr, "face area %g of %zu pts dropped\n", A, b.outline.size());
            continue;
        }
        net.blocks.push_back(std::move(b));
    }
    for (const auto& e : edges)
        if (e.alive)
            net.roads.push_back({simplifyOpen(e.pts, 0.3f), e.cls});
    std::vector<int> degree(nodes.size(), 0);
    for (const auto& e : edges)
        if (e.alive) {
            ++degree[static_cast<std::size_t>(e.a)];
            ++degree[static_cast<std::size_t>(e.b)];
        }
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (degree[i] >= 3)
            net.junctions.push_back(nodes[i]);
    return net;
}

// ============================================================================ polygons
float signedArea(const Poly& p)
{
    float a = 0.0f;
    for (std::size_t i = 0, n = p.size(); i < n; ++i)
        a += cross2(p[i], p[(i + 1) % n]);
    return a * 0.5f;
}

glm::vec2 centroid(const Poly& p)
{
    const float A = signedArea(p);
    if (std::abs(A) < 1e-6f) {
        glm::vec2 c{0.0f};
        for (const auto& q : p)
            c += q;
        return p.empty() ? c : c / static_cast<float>(p.size());
    }
    glm::vec2 c{0.0f};
    for (std::size_t i = 0, n = p.size(); i < n; ++i) {
        const glm::vec2 a = p[i], b = p[(i + 1) % n];
        c += (a + b) * cross2(a, b);
    }
    return c / (6.0f * A);
}

bool pointInPolygon(const Poly& p, glm::vec2 q)
{
    bool in = false;
    for (std::size_t i = 0, n = p.size(), j = n - 1; i < n; j = i++) {
        const glm::vec2 a = p[i], b = p[j];
        if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x)
            in = !in;
    }
    return in;
}

bool isSimple(const Poly& p)
{
    const std::size_t n = p.size();
    if (n < 3)
        return false;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1)
                continue;
            float t, u;
            const glm::vec2 a = p[i], b = p[(i + 1) % n], c = p[j], d = p[(j + 1) % n];
            const glm::vec2 r = b - a, s = d - c;
            const float den = cross2(r, s);
            if (std::abs(den) < 1e-9f)
                continue;
            t = cross2(c - a, s) / den;
            u = cross2(c - a, r) / den;
            if (t > 1e-4f && t < 1.0f - 1e-4f && u > 1e-4f && u < 1.0f - 1e-4f)
                return false;
        }
    return true;
}

float distanceToPolygonEdge(const Poly& p, glm::vec2 q)
{
    float best = 1e30f;
    for (std::size_t i = 0, n = p.size(); i < n; ++i) {
        const glm::vec2 a = p[i], b = p[(i + 1) % n];
        const glm::vec2 ab = b - a;
        const float L2 = glm::dot(ab, ab);
        const float t = L2 > 0.0f ? std::clamp(glm::dot(q - a, ab) / L2, 0.0f, 1.0f) : 0.0f;
        best = std::min(best, glm::length(q - (a + ab * t)));
    }
    return best;
}

Poly simplifyPolygon(const Poly& p, float tolerance, const std::vector<bool>* keep, std::vector<int>* kept)
{
    const std::size_t n = p.size();
    if (n <= 3) {
        if (kept) {
            kept->clear();
            for (std::size_t i = 0; i < n; ++i)
                kept->push_back(static_cast<int>(i));
        }
        return p;
    }
    // Anchor: the vertex farthest from vertex 0, so the closed ring splits in two open runs.
    std::size_t far = 0;
    float fd = -1.0f;
    for (std::size_t i = 1; i < n; ++i)
        if (const float d = glm::length(p[i] - p[0]); d > fd) {
            fd = d;
            far = i;
        }
    std::vector<char> mark(n, 0);
    mark[0] = mark[far] = 1;
    if (keep)
        for (std::size_t i = 0; i < n && i < keep->size(); ++i)
            if ((*keep)[i])
                mark[i] = 1;
    // Douglas-Peucker between consecutive marked vertices (around the ring).
    for (bool again = true; again;) {
        again = false;
        std::vector<std::size_t> marks;
        for (std::size_t i = 0; i < n; ++i)
            if (mark[i])
                marks.push_back(i);
        for (std::size_t m = 0; m < marks.size(); ++m) {
            const std::size_t i = marks[m], j = marks[(m + 1) % marks.size()];
            const std::size_t span = (j + n - i) % n;
            if (span < 2)
                continue;
            const glm::vec2 a = p[i], b = p[j];
            const glm::vec2 ab = b - a;
            const float L2 = glm::dot(ab, ab);
            float worst = 0.0f;
            std::size_t at = i;
            for (std::size_t k = 1; k < span; ++k) {
                const std::size_t idx = (i + k) % n;
                const float t = L2 > 0.0f ? std::clamp(glm::dot(p[idx] - a, ab) / L2, 0.0f, 1.0f) : 0.0f;
                const float d = glm::length(p[idx] - (a + ab * t));
                if (d > worst) {
                    worst = d;
                    at = idx;
                }
            }
            if (worst > tolerance) {
                mark[at] = 1;
                again = true;
            }
        }
    }
    Poly out;
    if (kept)
        kept->clear();
    for (std::size_t i = 0; i < n; ++i)
        if (mark[i]) {
            out.push_back(p[i]);
            if (kept)
                kept->push_back(static_cast<int>(i));
        }
    return out;
}

namespace {
glm::vec2 meet(glm::vec2 o1, glm::vec2 t1, glm::vec2 o2, glm::vec2 t2)
{
    const float den = cross2(t1, t2);
    if (std::abs(den) < 2e-3f)
        return (o2 + o1 + t1 * glm::dot(o2 - o1, t1)) * 0.5f;
    const float a = cross2(o2 - o1, t2) / den;
    return o1 + t1 * a;
}
} // namespace

bool insetPolygon(const Poly& p, const std::vector<float>& distance, Poly& out, std::vector<int>* sourceEdge)
{
    const std::size_t n = p.size();
    out.clear();
    if (n < 3)
        return false;
    struct L {
        glm::vec2 o, t;
        int src;
    };
    std::vector<L> lines;
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 a = p[i], b = p[(i + 1) % n];
        const float len = glm::length(b - a);
        if (len < 1e-4f)
            continue;
        const glm::vec2 t = (b - a) / len;
        const glm::vec2 nIn{-t.y, t.x};
        lines.push_back({a + nIn * (i < distance.size() ? distance[i] : distance.back()), t, static_cast<int>(i)});
    }
    for (int iter = 0; iter < static_cast<int>(n) + 2 && lines.size() >= 3; ++iter) {
        const std::size_t m = lines.size();
        std::vector<glm::vec2> v(m);
        for (std::size_t k = 0; k < m; ++k) {
            const L& a = lines[(k + m - 1) % m];
            const L& b = lines[k];
            v[k] = meet(a.o, a.t, b.o, b.t);
        }
        // The edge that turned inside out (or shrank to nothing) the most goes.
        float worst = 0.05f;
        std::size_t at = m;
        for (std::size_t k = 0; k < m; ++k) {
            const float along = glm::dot(v[(k + 1) % m] - v[k], lines[k].t);
            if (along < worst) {
                worst = along;
                at = k;
            }
        }
        if (at == m) {
            out = v;
            if (sourceEdge) {
                sourceEdge->clear();
                for (const auto& l : lines)
                    sourceEdge->push_back(l.src);
            }
            return out.size() >= 3 && signedArea(out) > 1.0f && isSimple(out);
        }
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(at));
    }
    out.clear();
    return false;
}

bool mitreInset(const Poly& p, const std::vector<float>& distance, Poly& out)
{
    const std::size_t n = p.size();
    out.assign(n, glm::vec2(0.0f));
    if (n < 3)
        return false;
    std::vector<glm::vec2> o(n), t(n);
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 a = p[i], b = p[(i + 1) % n];
        const float len = glm::length(b - a);
        if (len < 1e-4f)
            return false;
        t[i] = (b - a) / len;
        o[i] = a + glm::vec2(-t[i].y, t[i].x) * (i < distance.size() ? distance[i] : distance.back());
    }
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t pr = (i + n - 1) % n;
        out[i] = meet(o[pr], t[pr], o[i], t[i]);
    }
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        if (glm::dot(out[j] - out[i], t[i]) <= 0.2f)
            return false;
        // The two cut lines of the strip must not cross.
        float a, b;
        if (segmentHit(p[i], out[i], p[j], out[j], a, b) && a > 1e-3f && a < 1.0f - 1e-3f)
            return false;
    }
    return signedArea(out) > 1.0f && isSimple(out);
}

std::vector<std::uint32_t> triangulate(const Poly& pIn)
{
    std::vector<std::uint32_t> tris;
    const std::size_t n = pIn.size();
    if (n < 3)
        return tris;
    std::vector<std::uint32_t> idx(n);
    for (std::size_t i = 0; i < n; ++i)
        idx[i] = static_cast<std::uint32_t>(i);
    if (signedArea(pIn) < 0.0f)
        std::reverse(idx.begin(), idx.end());
    auto inTri = [](glm::vec2 q, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
        return cross2(b - a, q - a) >= -1e-6f && cross2(c - b, q - b) >= -1e-6f && cross2(a - c, q - c) >= -1e-6f;
    };
    int guard = 0;
    while (idx.size() > 3 && guard++ < static_cast<int>(n * n + 10)) {
        bool clipped = false;
        const std::size_t m = idx.size();
        for (std::size_t k = 0; k < m; ++k) {
            const std::uint32_t ia = idx[(k + m - 1) % m], ib = idx[k], ic = idx[(k + 1) % m];
            const glm::vec2 a = pIn[ia], b = pIn[ib], c = pIn[ic];
            if (cross2(b - a, c - b) <= 1e-7f)
                continue; // reflex or flat
            bool empty = true;
            for (std::size_t j = 0; j < m && empty; ++j) {
                const std::uint32_t ij = idx[j];
                if (ij == ia || ij == ib || ij == ic)
                    continue;
                if (inTri(pIn[ij], a, b, c))
                    empty = false;
            }
            if (!empty)
                continue;
            tris.insert(tris.end(), {ia, ib, ic});
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(k));
            clipped = true;
            break;
        }
        if (!clipped) { // degenerate rest: fan it
            for (std::size_t k = 1; k + 1 < idx.size(); ++k)
                tris.insert(tris.end(), {idx[0], idx[k], idx[k + 1]});
            return tris;
        }
    }
    if (idx.size() == 3)
        tris.insert(tris.end(), {idx[0], idx[1], idx[2]});
    return tris;
}

void minAreaRect(const Poly& p, glm::vec2& centre, glm::vec2& axis, glm::vec2& half)
{
    float best = 1e30f;
    centre = centroid(p);
    axis = {1.0f, 0.0f};
    half = {0.0f, 0.0f};
    for (std::size_t i = 0, n = p.size(); i < n; ++i) {
        const glm::vec2 e = p[(i + 1) % n] - p[i];
        const float len = glm::length(e);
        if (len < 1e-3f)
            continue;
        const glm::vec2 u = e / len, v{-u.y, u.x};
        float u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
        for (const auto& q : p) {
            const float a = glm::dot(q, u), b = glm::dot(q, v);
            u0 = std::min(u0, a);
            u1 = std::max(u1, a);
            v0 = std::min(v0, b);
            v1 = std::max(v1, b);
        }
        const float A = (u1 - u0) * (v1 - v0);
        if (A < best - 1e-3f) {
            best = A;
            centre = u * (u0 + u1) * 0.5f + v * (v0 + v1) * 0.5f;
            if (u1 - u0 >= v1 - v0) {
                axis = u;
                half = {(u1 - u0) * 0.5f, (v1 - v0) * 0.5f};
            } else {
                axis = v;
                half = {(v1 - v0) * 0.5f, (u1 - u0) * 0.5f};
            }
        }
    }
}

void splitPolygon(const Poly& p, glm::vec2 a, glm::vec2 d, Poly& left, Poly& right)
{
    left.clear();
    right.clear();
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 P = p[i], Q = p[(i + 1) % n];
        const float sp = cross2(d, P - a), sq = cross2(d, Q - a);
        if (sp >= 0.0f)
            left.push_back(P);
        if (sp <= 0.0f)
            right.push_back(P);
        if ((sp > 0.0f && sq < 0.0f) || (sp < 0.0f && sq > 0.0f)) {
            const glm::vec2 x = P + (Q - P) * (sp / (sp - sq));
            left.push_back(x);
            right.push_back(x);
        }
    }
    if (left.size() < 3)
        left.clear();
    if (right.size() < 3)
        right.clear();
}

} // namespace engine::city
