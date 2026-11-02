#include "engine/CityGen.h"

#include "engine/JobSystem.h"
#include "engine/Material.h"
#include "engine/SceneGraph.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_set>

namespace engine::city {

namespace fs = std::filesystem;

// ============================================================================ names
namespace {
constexpr const char* kStreetNames[] = {"none", "alley", "side", "main"};
constexpr const char* kBlockNames[] = {"perimeter", "microdistrict", "towers", "garages", "park", "ruin", "wasteland", "empty"};
constexpr const char* kBlockLabels[] = {"Perimeter block (tenements)", "Micro-district (panel slabs)", "Towers",
                                        "Garages", "Park", "Ruin", "Waste lot", "Empty lot"};
} // namespace

const char* streetClassName(StreetClass c) { return kStreetNames[static_cast<int>(c)]; }
StreetClass streetClassFromName(const std::string& name)
{
    for (int i = 0; i < kStreetClassCount; ++i)
        if (name == kStreetNames[i])
            return static_cast<StreetClass>(i);
    return StreetClass::Side;
}
const char* blockKindName(BlockKind k) { return kBlockNames[static_cast<int>(k)]; }
const char* blockKindLabel(BlockKind k) { return kBlockLabels[static_cast<int>(k)]; }
BlockKind blockKindFromName(const std::string& name)
{
    for (int i = 0; i < kBlockKindCount; ++i)
        if (name == kBlockNames[i])
            return static_cast<BlockKind>(i);
    return BlockKind::Perimeter;
}

const std::vector<std::string>& propRoles()
{
    static const std::vector<std::string> roles{"kerb",  "lamp",  "lamp_leaning", "cable", "car",    "tree",  "sapling",
                                                "snag",  "bush",  "grass",        "weed",  "nettle", "rubble", "slab",  "meadow"};
    return roles;
}

std::vector<const KitBuilding*> Kit::buildingsWithRole(const std::string& role) const
{
    std::vector<const KitBuilding*> out;
    for (const auto& b : buildings)
        if (b.role == role && b.length > 0.5f)
            out.push_back(&b);
    return out;
}

std::vector<const KitProp*> Kit::propsWithRole(const std::string& role) const
{
    std::vector<const KitProp*> out;
    for (const auto& p : props)
        if (p.role == role)
            out.push_back(&p);
    return out;
}

BlockSettings Settings::block(int row, int column) const
{
    const std::size_t i = static_cast<std::size_t>(row) * columns.size() + static_cast<std::size_t>(column);
    return i < blocks.size() ? blocks[i] : BlockSettings{};
}

void Settings::setBlock(int row, int column, const BlockSettings& b)
{
    normalize();
    blocks[static_cast<std::size_t>(row) * columns.size() + static_cast<std::size_t>(column)] = b;
}

void Settings::normalize()
{
    if (columns.empty())
        columns.push_back(100.0f);
    if (rows.empty())
        rows.push_back(100.0f);
    for (float& c : columns)
        c = std::clamp(c, 10.0f, 2000.0f);
    for (float& r : rows)
        r = std::clamp(r, 10.0f, 2000.0f);
    streetsX.resize(columns.size() + 1, StreetClass::Side);
    streetsZ.resize(rows.size() + 1, StreetClass::Side);
    blocks.resize(columns.size() * rows.size());
    for (auto& p : profiles) {
        p.carriageway = std::clamp(p.carriageway, 0.0f, 60.0f);
        p.pavement = std::clamp(p.pavement, 0.0f, 20.0f);
    }
    profiles[0] = StreetProfile{0.0f, 0.0f, false, false, false};
    gatewayMin = std::clamp(gatewayMin, 1.0f, 40.0f);
    gatewayMax = std::clamp(gatewayMax, gatewayMin, 60.0f);
    gatewayEvery = std::max(gatewayEvery, 10.0f);
    maxFloorStep = std::clamp(maxFloorStep, 0, 50);
    overlap = std::clamp(overlap, 0.0f, 1.0f);
    lampSpacing = std::max(lampSpacing, 5.0f);
    crossCableSpacing = std::max(crossCableSpacing, 3.0f);
    treeSpacing = std::max(treeSpacing, 2.0f);
    rowGapFactor = std::max(rowGapFactor, 0.0f);
    slabGap = std::max(slabGap, 0.0f);
    overgrowth = std::clamp(overgrowth, 0.0f, 1.0f);
    outskirts = std::clamp(outskirts, 0.0f, 2000.0f);
    treeSurvival = std::clamp(treeSurvival, 0.0f, 1.0f);
    cableChance = std::clamp(cableChance, 0.0f, 1.0f);
    wrecksPer100m = std::clamp(wrecksPer100m, 0.0f, 20.0f);
    rubblePer100m = std::clamp(rubblePer100m, 0.0f, 50.0f);
    if (group.empty())
        group = "City";
}

// ============================================================================ text files
namespace {

std::vector<std::string> tokenize(const std::string& line)
{
    std::vector<std::string> out;
    std::istringstream in(line);
    in >> std::ws;
    while (in && !in.eof()) {
        if (in.peek() == '#')
            break;
        std::string t;
        if (in.peek() == '"')
            in >> std::quoted(t);
        else
            in >> t;
        if (!in && t.empty())
            break;
        out.push_back(t);
        in >> std::ws;
    }
    return out;
}

// Locale-independent (strtod would read "0.15" as 0 under a Czech C locale, which Qt sets).
float toFloat(const std::string& s, float fallback = 0.0f)
{
    std::istringstream in(s);
    in.imbue(std::locale::classic());
    float v = fallback;
    if (!(in >> v) || !std::isfinite(v))
        return fallback;
    return v;
}

fs::path resolve(const fs::path& dir, const std::string& rel)
{
    if (rel.empty())
        return {};
    const fs::path p(rel);
    return (p.is_absolute() ? p : dir / p).lexically_normal();
}

std::string relPath(const fs::path& p, const fs::path& dir)
{
    if (p.empty())
        return {};
    std::error_code ec;
    const fs::path r = fs::relative(p, dir, ec);
    return (ec || r.empty() ? p : r).generic_string();
}

std::string q(const std::string& s)
{
    std::ostringstream o;
    o << std::quoted(s);
    return o.str();
}

// "key value key value ..." tail of a kit line.
std::map<std::string, std::string> pairs(const std::vector<std::string>& t, std::size_t from, std::string* lodPath, float* lodDist)
{
    std::map<std::string, std::string> kv;
    for (std::size_t i = from; i < t.size(); ++i) {
        if (t[i] == "lod" && i + 1 < t.size()) {
            if (lodPath)
                *lodPath = t[i + 1];
            if (lodDist && i + 2 < t.size())
                *lodDist = toFloat(t[i + 2], *lodDist);
            i += 2;
            continue;
        }
        if (i + 1 < t.size()) {
            kv[t[i]] = t[i + 1];
            ++i;
        }
    }
    return kv;
}

} // namespace

bool loadKit(Kit& kit, const fs::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        if (error)
            *error = "cannot read " + file.string();
        return false;
    }
    kit = {};
    const fs::path dir = file.parent_path();
    std::string line;
    while (std::getline(in, line)) {
        const auto t = tokenize(line);
        if (t.empty())
            continue;
        if (t[0] == "material" && t.size() >= 3) {
            const fs::path p = resolve(dir, t[2]);
            if (t[1] == "road") kit.road = p;
            else if (t[1] == "pavement") kit.pavement = p;
            else if (t[1] == "yard") kit.yard = p;
            else if (t[1] == "lot") kit.lot = p;
            else if (t[1] == "gate") kit.gate = p;
            else if (t[1] == "pillar") kit.pillar = p;
        } else if (t[0] == "building" && t.size() >= 3) {
            KitBuilding b;
            b.name = t[1];
            b.path = resolve(dir, t[2]);
            std::string lod;
            const auto kv = pairs(t, 3, &lod, &b.lodDistance);
            b.lodPath = resolve(dir, lod);
            auto get = [&](const char* k, float d) { const auto it = kv.find(k); return it == kv.end() ? d : toFloat(it->second, d); };
            if (const auto it = kv.find("role"); it != kv.end())
                b.role = it->second;
            b.length = get("length", b.length);
            b.depth = get("depth", b.depth);
            b.front = get("front", b.front);
            b.floors = static_cast<int>(get("floors", static_cast<float>(b.floors)));
            b.height = get("height", b.height);
            b.weight = get("weight", b.weight);
            b.cull = get("cull", b.cull);
            kit.buildings.push_back(std::move(b));
        } else if (t[0] == "prop" && t.size() >= 3) {
            KitProp p;
            p.role = t[1];
            p.path = resolve(dir, t[2]);
            std::string lod;
            const auto kv = pairs(t, 3, &lod, &p.lodDistance);
            p.lodPath = resolve(dir, lod);
            auto get = [&](const char* k, float d) { const auto it = kv.find(k); return it == kv.end() ? d : toFloat(it->second, d); };
            p.size = get("size", p.size);
            p.cull = get("cull", p.cull);
            p.sway = get("sway", p.sway);
            p.impostor = get("impostor", p.impostor);
            p.scaleMin = get("scale_min", p.scaleMin);
            p.scaleMax = get("scale_max", std::max(p.scaleMin, p.scaleMax));
            p.yaw = get("yaw", p.yaw);
            p.collision = get("collision", 1.0f) != 0.0f;
            p.shadows = get("shadows", 1.0f) != 0.0f;
            kit.props.push_back(std::move(p));
        }
    }
    return true;
}

bool saveKit(const Kit& kit, const fs::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        if (error)
            *error = "cannot write " + file.string();
        return false;
    }
    const fs::path dir = file.parent_path();
    out << "# City kit for the city generator (engine/CityGen.h, docs/CITY_GENERATOR.md). Paths are relative to this file.\n";
    out << "# building \"name\" \"model\" role <tenement|panel|tower|garage> length depth front floors height weight [lod \"model\" m] cull\n";
    out << "# prop <role> \"model\" [size m] [lod \"model\" m] cull sway impostor scale_min scale_max yaw collision shadows\n\n";
    const std::pair<const char*, const fs::path*> mats[] = {{"road", &kit.road}, {"pavement", &kit.pavement}, {"yard", &kit.yard},
                                                            {"lot", &kit.lot},   {"gate", &kit.gate},         {"pillar", &kit.pillar}};
    for (const auto& [k, p] : mats)
        if (!p->empty())
            out << "material " << k << ' ' << q(relPath(*p, dir)) << '\n';
    out << '\n';
    for (const auto& b : kit.buildings) {
        out << "building " << q(b.name) << ' ' << q(relPath(b.path, dir)) << " role " << b.role << " length " << b.length
            << " depth " << b.depth << " front " << b.front << " floors " << b.floors << " height " << b.height << " weight "
            << b.weight;
        if (!b.lodPath.empty())
            out << " lod " << q(relPath(b.lodPath, dir)) << ' ' << b.lodDistance;
        out << " cull " << b.cull << '\n';
    }
    out << '\n';
    for (const auto& p : kit.props) {
        out << "prop " << p.role << ' ' << q(relPath(p.path, dir));
        if (p.size > 0.0f)
            out << " size " << p.size;
        if (!p.lodPath.empty())
            out << " lod " << q(relPath(p.lodPath, dir)) << ' ' << p.lodDistance;
        out << " cull " << p.cull;
        if (p.sway != 0.0f)
            out << " sway " << p.sway;
        if (p.impostor != 0.0f)
            out << " impostor " << p.impostor;
        if (p.scaleMin != 1.0f || p.scaleMax != 1.0f)
            out << " scale_min " << p.scaleMin << " scale_max " << p.scaleMax;
        if (p.yaw != 0.0f)
            out << " yaw " << p.yaw;
        out << " collision " << (p.collision ? 1 : 0) << " shadows " << (p.shadows ? 1 : 0) << '\n';
    }
    return static_cast<bool>(out);
}

namespace {
struct ScalarField {
    const char* key;
    float Settings::*f = nullptr;
    int Settings::*i = nullptr;
    bool Settings::*b = nullptr;
};
const ScalarField kScalars[] = {
    {"kerb_height", &Settings::kerbHeight},
    {"gateway_min", &Settings::gatewayMin},
    {"gateway_max", &Settings::gatewayMax},
    {"gateway_every", &Settings::gatewayEvery},
    {"max_floor_step", nullptr, &Settings::maxFloorStep},
    {"tall_corners", nullptr, nullptr, &Settings::tallCorners},
    {"overlap", &Settings::overlap},
    {"courtyard_garages", nullptr, nullptr, &Settings::courtyardGarages},
    {"setback", &Settings::setback},
    {"row_gap_factor", &Settings::rowGapFactor},
    {"row_gap_min", &Settings::rowGapMin},
    {"slab_gap", &Settings::slabGap},
    {"lamp_spacing", &Settings::lampSpacing},
    {"cable_chance", &Settings::cableChance},
    {"cross_cable_spacing", &Settings::crossCableSpacing},
    {"tree_spacing", &Settings::treeSpacing},
    {"tree_survival", &Settings::treeSurvival},
    {"wrecks_per_100m", &Settings::wrecksPer100m},
    {"rubble_per_100m", &Settings::rubblePer100m},
    {"overgrowth", &Settings::overgrowth},
    {"outskirts", &Settings::outskirts},
};
} // namespace

bool loadSettings(Settings& s, const fs::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        if (error)
            *error = "cannot read " + file.string();
        return false;
    }
    s = Settings{};
    s.blocks.clear();
    const fs::path dir = file.parent_path();
    std::vector<std::tuple<int, int, BlockSettings>> blocks;
    std::string line;
    while (std::getline(in, line)) {
        const auto t = tokenize(line);
        if (t.empty())
            continue;
        const std::string& k = t[0];
        auto num = [&](std::size_t i, float d) { return i < t.size() ? toFloat(t[i], d) : d; };
        if (k == "seed") {
            std::istringstream n(t.size() > 1 ? t[1] : std::string("0"));
            unsigned long v = 0;
            if (n >> v)
                s.seed = static_cast<std::uint32_t>(v);
        } else if (k == "origin") s.origin = {num(1, 0), num(2, 0), num(3, 0)};
        else if (k == "rotation") s.rotation = num(1, 0);
        else if (k == "group" && t.size() > 1) s.group = t[1];
        else if (k == "kit" && t.size() > 1) s.kit = resolve(dir, t[1]);
        else if (k == "columns" || k == "rows") {
            auto& v = k == "columns" ? s.columns : s.rows;
            v.clear();
            for (std::size_t i = 1; i < t.size(); ++i)
                v.push_back(toFloat(t[i], 100.0f));
        } else if (k == "streets_x" || k == "streets_z") {
            auto& v = k == "streets_x" ? s.streetsX : s.streetsZ;
            v.clear();
            for (std::size_t i = 1; i < t.size(); ++i)
                v.push_back(streetClassFromName(t[i]));
        } else if (k == "block" && t.size() >= 4) {
            BlockSettings b;
            b.kind = blockKindFromName(t[3]);
            b.seed = t.size() > 4 ? static_cast<std::uint32_t>(num(4, 0)) : 0u;
            blocks.emplace_back(static_cast<int>(num(1, 0)), static_cast<int>(num(2, 0)), b);
        } else if (k == "profile" && t.size() >= 4) {
            const int c = static_cast<int>(streetClassFromName(t[1]));
            StreetProfile& p = s.profiles[static_cast<std::size_t>(c)];
            p.carriageway = num(2, p.carriageway);
            p.pavement = num(3, p.pavement);
            const auto kv = pairs(t, 4, nullptr, nullptr);
            auto flag = [&](const char* key, bool d) { const auto it = kv.find(key); return it == kv.end() ? d : toFloat(it->second) != 0.0f; };
            p.lamps = flag("lamps", p.lamps);
            p.trees = flag("trees", p.trees);
            p.crossCables = flag("cross_cables", p.crossCables);
        } else {
            for (const auto& f : kScalars)
                if (k == f.key) {
                    if (f.f) s.*(f.f) = num(1, s.*(f.f));
                    if (f.i) s.*(f.i) = static_cast<int>(num(1, static_cast<float>(s.*(f.i))));
                    if (f.b) s.*(f.b) = num(1, s.*(f.b) ? 1.0f : 0.0f) != 0.0f;
                }
        }
    }
    s.normalize();
    for (const auto& [r, c, b] : blocks)
        if (r >= 0 && c >= 0 && r < static_cast<int>(s.rows.size()) && c < static_cast<int>(s.columns.size()))
            s.setBlock(r, c, b);
    return true;
}

bool saveSettings(const Settings& settings, const fs::path& file, std::string* error)
{
    Settings s = settings;
    s.normalize();
    std::ofstream out(file);
    if (!out) {
        if (error)
            *error = "cannot write " + file.string();
        return false;
    }
    const fs::path dir = file.parent_path();
    out << "# City generator settings (engine/CityGen.h, docs/CITY_GENERATOR.md); edit in the editor: Tools > City generator.\n";
    out << "seed " << s.seed << '\n';
    out << "origin " << s.origin.x << ' ' << s.origin.y << ' ' << s.origin.z << '\n';
    out << "rotation " << s.rotation << '\n';
    out << "group " << q(s.group) << '\n';
    if (!s.kit.empty())
        out << "kit " << q(relPath(s.kit, dir)) << '\n';
    out << "columns";
    for (float c : s.columns)
        out << ' ' << c;
    out << "\nrows";
    for (float r : s.rows)
        out << ' ' << r;
    out << "\nstreets_x";
    for (auto c : s.streetsX)
        out << ' ' << streetClassName(c);
    out << "\nstreets_z";
    for (auto c : s.streetsZ)
        out << ' ' << streetClassName(c);
    out << '\n';
    for (int r = 0; r < static_cast<int>(s.rows.size()); ++r)
        for (int c = 0; c < static_cast<int>(s.columns.size()); ++c) {
            const auto b = s.block(r, c);
            out << "block " << r << ' ' << c << ' ' << blockKindName(b.kind) << ' ' << b.seed << '\n';
        }
    for (int i = 1; i < kStreetClassCount; ++i) {
        const auto& p = s.profiles[static_cast<std::size_t>(i)];
        out << "profile " << kStreetNames[i] << ' ' << p.carriageway << ' ' << p.pavement << " lamps " << (p.lamps ? 1 : 0)
            << " trees " << (p.trees ? 1 : 0) << " cross_cables " << (p.crossCables ? 1 : 0) << '\n';
    }
    for (const auto& f : kScalars) {
        out << f.key << ' ';
        if (f.f) out << s.*(f.f);
        if (f.i) out << s.*(f.i);
        if (f.b) out << (s.*(f.b) ? 1 : 0);
        out << '\n';
    }
    return static_cast<bool>(out);
}

// ============================================================================ generator
namespace {

// SplitMix64: the same numbers on every compiler (std distributions are not).
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed) {}
    std::uint64_t next()
    {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float uniform() { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); } // [0, 1)
    float range(float a, float b) { return a + (b - a) * uniform(); }
    int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
    bool chance(float p) { return uniform() < p; }
    template <class T>
    const T& pick(const std::vector<T>& v) { return v[static_cast<std::size_t>(below(static_cast<int>(v.size())))]; }
};

std::uint64_t mix(std::uint64_t a, std::uint64_t b)
{
    Rng r(a * 0x100000001B3ull ^ (b + 0x632BE59BD9B4E019ull));
    return r.next();
}

constexpr float kPi = 3.14159265358979f;
float deg(float rad) { return rad * 180.0f / kPi; }

struct Rect {
    float u0, v0, u1, v1;
    float w() const { return u1 - u0; }
    float h() const { return v1 - v0; }
    bool contains(glm::vec2 p, float margin = 0.0f) const
    {
        return p.x > u0 - margin && p.x < u1 + margin && p.y > v0 - margin && p.y < v1 + margin;
    }
    glm::vec2 centre() const { return {(u0 + u1) * 0.5f, (v0 + v1) * 0.5f}; }
};

struct Capsule {
    glm::vec2 a, b;
    float r;
    bool contains(glm::vec2 p) const
    {
        const glm::vec2 ab = b - a;
        const float len2 = glm::dot(ab, ab);
        const float t = len2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        const glm::vec2 d = p - (a + ab * t);
        return glm::dot(d, d) < r * r;
    }
};

// One side of a lot: from `start` along `t`, the street on the side of `n` (outward).
struct Edge {
    glm::vec2 start, t, n;
    float length = 0.0f;
    StreetClass cls = StreetClass::None;
    int line = -1;          // street line index: X lines 0..C, Z lines C+1.. (for staggering, species)
    bool alongU = true;     // t along U (a Z line street) or along V (an X line street)
};

const glm::vec4 kColStreet{1.0f, 0.85f, 0.2f, 1.0f};
const glm::vec4 kColLot{0.9f, 0.9f, 0.9f, 1.0f};
const glm::vec4 kColHouse{0.3f, 0.75f, 1.0f, 1.0f};
const glm::vec4 kColGate{0.3f, 1.0f, 0.4f, 1.0f};
const glm::vec4 kColPath{0.85f, 0.6f, 0.35f, 1.0f};
const glm::vec4 kColTree{0.2f, 0.8f, 0.2f, 1.0f};

class Builder {
public:
    Builder(const Settings& s, const Kit& k) : s_(s), kit_(k)
    {
        const float a = glm::radians(s.rotation);
        cos_ = std::cos(a);
        sin_ = std::sin(a);
    }

    Plan plan;

    // Local (u along X, v along Z, before the city's rotation) to world.
    glm::vec3 world(glm::vec2 p, float y) const
    {
        return s_.origin + glm::vec3(p.x * cos_ + p.y * sin_, y, -p.x * sin_ + p.y * cos_);
    }
    // Yaw that turns a model's +Z onto the local direction d.
    float yawZ(glm::vec2 d) const { return deg(std::atan2(d.x, d.y)) + s_.rotation; }
    // Yaw that turns a model's +X onto d.
    float yawX(glm::vec2 d) const { return deg(std::atan2(-d.y, d.x)) + s_.rotation; }

    void group(const std::string& name, const std::string& parent)
    {
        if (groupSet_.insert(name).second)
            plan.groups.push_back({name, parent});
    }

    std::string label(const std::string& base)
    {
        const int n = ++counts_[base];
        return n == 1 ? base : base + " " + std::to_string(n);
    }

    void line(glm::vec2 a, glm::vec2 b, const glm::vec4& c, float y = 0.4f)
    {
        plan.lines.push_back(world(a, y + s_.kerbHeight));
        plan.lines.push_back(world(b, y + s_.kerbHeight));
        plan.lineColors.push_back(c);
        plan.lineColors.push_back(c);
    }
    void rectLines(const Rect& r, const glm::vec4& c, float y = 0.4f)
    {
        line({r.u0, r.v0}, {r.u1, r.v0}, c, y);
        line({r.u1, r.v0}, {r.u1, r.v1}, c, y);
        line({r.u1, r.v1}, {r.u0, r.v1}, c, y);
        line({r.u0, r.v1}, {r.u0, r.v0}, c, y);
    }

    void box(const std::string& name, const std::string& grp, const fs::path& material, glm::vec2 c, float yCentre, glm::vec3 size,
             float localYaw = 0.0f)
    {
        Placement p;
        p.kind = Placement::Kind::Box;
        p.name = label(name);
        p.group = grp;
        p.path = material;
        p.position = world(c, yCentre);
        p.size = size;
        p.rotation = {0.0f, localYaw + s_.rotation, 0.0f};
        plan.placements.push_back(std::move(p));
        ++plan.stats.boxes;
    }

    void building(const KitBuilding& b, const std::string& grp, glm::vec2 origin, float yaw)
    {
        Placement p;
        p.name = label(b.name);
        p.group = grp;
        p.path = b.path;
        p.lodPath = b.lodPath;
        p.lodDistance = b.lodDistance;
        p.position = world(origin, s_.kerbHeight);
        p.rotation = {0.0f, yaw, 0.0f};
        p.cull = b.cull;
        plan.placements.push_back(std::move(p));
        ++plan.stats.buildings;
    }

    Placement* prop(const KitProp& k, const std::string& name, const std::string& grp, glm::vec2 at, float y, float yaw, Rng& rng,
                    glm::vec2 tilt = {0.0f, 0.0f}, float scale = -1.0f)
    {
        Placement p;
        p.name = label(name);
        p.group = grp;
        p.path = k.path;
        p.lodPath = k.lodPath;
        p.lodDistance = k.lodDistance;
        p.position = world(at, y);
        p.rotation = {tilt.x, yaw + k.yaw, tilt.y};
        p.scale = scale > 0.0f ? scale : rng.range(k.scaleMin, k.scaleMax);
        p.cull = k.cull;
        p.sway = k.sway;
        p.impostor = k.impostor;
        p.collision = k.collision;
        p.shadows = k.shadows;
        plan.placements.push_back(std::move(p));
        return &plan.placements.back();
    }

    const KitProp* anyProp(const std::string& role, Rng& rng) const
    {
        const auto v = kit_.propsWithRole(role);
        return v.empty() ? nullptr : v[static_cast<std::size_t>(rng.below(static_cast<int>(v.size())))];
    }

    // Free for plants: not inside a building, not on a kept-clear path.
    bool free(glm::vec2 p, float margin = 0.3f) const
    {
        for (const auto& r : solid_)
            if (r.contains(p, margin))
                return false;
        for (const auto& c : clear_)
            if (c.contains(p))
                return false;
        return true;
    }

    // A plant of one of the roles (first that exists), if the spot is free.
    bool plant(const std::vector<const char*>& roles, const std::string& name, const std::string& grp, glm::vec2 at, Rng& rng,
               float y, float margin = 0.3f)
    {
        if (!free(at, margin))
            return false;
        const KitProp* k = nullptr;
        for (const char* r : roles)
            if ((k = anyProp(r, rng)))
                break;
        if (!k)
            return false;
        prop(*k, name, grp, at, y, rng.range(0.0f, 360.0f), rng);
        ++plan.stats.plants;
        return true;
    }

    void tree(const KitProp& k, const std::string& grp, glm::vec2 at, Rng& rng, float y)
    {
        prop(k, "Tree", grp, at, y, rng.range(0.0f, 360.0f), rng);
        solid_.push_back({at.x - 0.4f, at.y - 0.4f, at.x + 0.4f, at.y + 0.4f});
        trees_.push_back(at);
        ++plan.stats.trees;
    }

    // Seedlings and young wood near a standing tree (Pripyat: dense regrowth along the streets).
    void seedlings(glm::vec2 at, int count, const std::string& grp, Rng& rng, float y, const Rect* inside = nullptr)
    {
        for (int i = 0; i < count; ++i) {
            const float a = rng.range(0.0f, 2.0f * kPi), d = rng.range(1.5f, 7.0f);
            const glm::vec2 p = at + glm::vec2(std::cos(a), std::sin(a)) * d;
            if (inside && !inside->contains(p))
                continue;
            plant({rng.chance(0.6f) ? "sapling" : "bush", "bush"}, rng.chance(0.6f) ? "Seedling" : "Bush", grp, p, rng, y);
        }
    }

    void cable(glm::vec3 a, glm::vec3 b, const std::string& grp)
    {
        const auto spans = kit_.propsWithRole("cable");
        if (spans.empty())
            return;
        const glm::vec3 d = b - a;
        const float L = std::hypot(d.x, d.z);
        if (L < 1.0f)
            return;
        const KitProp* best = spans.front();
        for (const auto* k : spans)
            if (std::abs(k->size - L) < std::abs(best->size - L))
                best = k;
        const float span = best->size > 0.0f ? best->size : L;
        Placement p;
        p.name = label("Cable");
        p.group = grp;
        p.path = best->path;
        p.position = (a + b) * 0.5f;
        p.rotation = {0.0f, deg(std::atan2(-d.z, d.x)), deg(std::atan2(d.y, L))};
        p.scale = L / span;
        p.cull = best->cull;
        p.collision = false;
        p.shadows = best->shadows;
        plan.placements.push_back(std::move(p));
    }

    void addSolid(const Rect& r) { solid_.push_back(r); }
    // True when a kept-clear path crosses the rectangle (sampled on a 1 m grid).
    bool crossesPath(const Rect& r) const
    {
        for (float u = r.u0; u <= r.u1 + 0.01f; u += std::max(0.5f, std::min(1.0f, r.w())))
            for (float v = r.v0; v <= r.v1 + 0.01f; v += std::max(0.5f, std::min(1.0f, r.h())))
                for (const auto& c : clear_)
                    if (c.contains({u, v}))
                        return true;
        return false;
    }
    void addClear(glm::vec2 a, glm::vec2 b, float r)
    {
        clear_.push_back({a, b, r});
        line(a, b, kColPath, 0.3f);
    }

    const Settings& s_;
    const Kit& kit_;

private:
    float cos_ = 1.0f, sin_ = 0.0f;
    std::map<std::string, int> counts_;
    std::set<std::string> groupSet_;
    std::vector<Rect> solid_;
    std::vector<Capsule> clear_;
    std::vector<glm::vec2> trees_;
};

// ---------------------------------------------------------------------------- frontage fill
struct Fill {
    std::vector<const KitBuilding*> houses;
    std::vector<float> gaps;   // gaps[i] between house i and i + 1 (0 = party wall)
    float error = 0.0f;        // |built - wanted|
    bool exact = false;
};

class FrontageFiller {
public:
    FrontageFiller(const std::vector<const KitBuilding*>& kit, const Settings& s, Rng& rng) : kit_(kit), s_(s), rng_(rng)
    {
        for (const auto* b : kit_)
            minLen_ = std::min(minLen_, b->length);
    }

    // Fills F metres: houses, overlapping `overlap` at party walls, with gateways
    // in [gatewayMin, gatewayMax] where the lengths do not add up.
    Fill fill(float F, bool accentStart, int prevFloors)
    {
        Fill best;
        if (kit_.empty() || F < minLen_ - kEps) {
            best.error = F;
            return best;
        }
        const int maxK = std::max(0, static_cast<int>(F / std::max(s_.gatewayEvery * 0.5f, 1.0f)));
        const int target = std::clamp(static_cast<int>(std::lround(F / s_.gatewayEvery)), 0, maxK);
        // Pass 1: the wanted number of gateways, all rules. Pass 2: any number of
        // gateways. Pass 3: relaxed rules (repeats and big floor steps allowed).
        for (int pass = 0; pass < 3; ++pass) {
            for (int k = (pass == 0 ? target : 0); k <= (pass == 0 ? target : maxK); ++k) {
                if (pass == 1 && k == target)
                    continue;
                k_ = k;
                strict_ = pass < 2;
                budget_ = 6000;
                seq_.clear();
                accent_ = accentStart && s_.tallCorners;
                prevFloors_ = prevFloors;
                if (dfs(F, 0.0f)) {
                    best = result(F, k);
                    return best;
                }
            }
        }
        // Last resort: greedy, the rest stays open (a waste gap, reported).
        float S = 0.0f;
        std::vector<const KitBuilding*> seq;
        while (true) {
            const KitBuilding* pickB = nullptr;
            for (const auto* b : kit_)
                if (S + b->length - (seq.empty() ? 0.0f : s_.overlap) <= F + kEps && (!pickB || b->length > pickB->length))
                    pickB = b;
            if (!pickB)
                break;
            S += pickB->length - (seq.empty() ? 0.0f : s_.overlap);
            seq.push_back(pickB);
        }
        best.houses = seq;
        best.gaps.assign(seq.empty() ? 0 : seq.size() - 1, 0.0f);
        best.error = F - S;
        return best;
    }

private:
    static constexpr float kEps = 0.02f;

    bool closes(float R, int n) const
    {
        if (k_ == 0)
            return std::abs(R) <= kEps;
        if (n - 1 < k_)
            return false;
        const float G = R - static_cast<float>(k_) * s_.overlap;
        return G >= static_cast<float>(k_) * s_.gatewayMin - kEps && G <= static_cast<float>(k_) * s_.gatewayMax + kEps;
    }

    bool dfs(float F, float S)
    {
        if (--budget_ < 0)
            return false;
        const int n = static_cast<int>(seq_.size());
        if (n > 0 && closes(F - S, n))
            return true;
        // Candidates allowed here, in a weighted random order.
        std::vector<std::pair<float, const KitBuilding*>> order;
        int topFloors = 0;
        if (n == 0 && accent_)
            for (const auto* b : kit_)
                if (b->length <= F + kEps)
                    topFloors = std::max(topFloors, b->floors);
        for (const auto* b : kit_) {
            const float add = b->length - (n > 0 ? s_.overlap : 0.0f);
            if (S + add > F + kEps)
                continue;
            const int prevFloors = n > 0 ? seq_.back()->floors : prevFloors_;
            if (strict_) {
                if (n > 0 && b == seq_.back())
                    continue; // never the same house twice side by side
                if (n > 1 && b == seq_[static_cast<std::size_t>(n - 2)] && kit_.size() > 2)
                    continue; // nor A B A
                if (prevFloors > 0 && std::abs(b->floors - prevFloors) > s_.maxFloorStep)
                    continue;
                if (n == 0 && accent_ && b->floors < topFloors - 1)
                    continue; // corner accent
            }
            const float key = -std::log(std::max(rng_.uniform(), 1e-6f)) / std::max(b->weight, 0.01f); // weighted shuffle
            order.emplace_back(key, b);
        }
        std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [key, b] : order) {
            seq_.push_back(b);
            if (dfs(F, S + b->length - (n > 0 ? s_.overlap : 0.0f)))
                return true;
            seq_.pop_back();
            if (budget_ < 0)
                return false;
        }
        return false;
    }

    Fill result(float F, int k) const
    {
        Fill f;
        f.houses = seq_;
        const int n = static_cast<int>(seq_.size());
        f.gaps.assign(static_cast<std::size_t>(std::max(0, n - 1)), 0.0f);
        float S = 0.0f;
        for (int i = 0; i < n; ++i)
            S += seq_[static_cast<std::size_t>(i)]->length - (i > 0 ? s_.overlap : 0.0f);
        if (k > 0) {
            const float G = (F - S - static_cast<float>(k) * s_.overlap) / static_cast<float>(k);
            // Spread the gateways evenly over the joints.
            std::set<int> used;
            for (int j = 0; j < k; ++j) {
                int idx = static_cast<int>(std::lround(static_cast<float>(j + 1) * static_cast<float>(n) / static_cast<float>(k + 1))) - 1;
                idx = std::clamp(idx, 0, n - 2);
                while (used.count(idx) && idx < n - 2)
                    ++idx;
                while (used.count(idx) && idx > 0)
                    --idx;
                used.insert(idx);
                f.gaps[static_cast<std::size_t>(idx)] = G;
            }
            S += static_cast<float>(k) * (G + s_.overlap);
        }
        f.error = std::abs(F - S);
        f.exact = f.error <= kEps;
        return f;
    }

    const std::vector<const KitBuilding*>& kit_;
    const Settings& s_;
    Rng& rng_;
    float minLen_ = 1e9f;
    int k_ = 0;
    bool strict_ = true;
    bool accent_ = false;
    int prevFloors_ = 0;
    int budget_ = 0;
    std::vector<const KitBuilding*> seq_;
};

// ---------------------------------------------------------------------------- the city
class CityLayout {
public:
    CityLayout(const Settings& s, const Kit& k) : s_(s), b_(s, k) {}

    Plan run()
    {
        grid();
        const std::string root = s_.group;
        b_.group(root, "");
        groundGroup_ = root + " ground";
        streetGroup_ = root + " streets";
        b_.group(groundGroup_, root);
        b_.group(streetGroup_, root);
        kerbs_ = root + " kerbs";
        lamps_ = root + " lamps and cables";
        streetTrees_ = root + " street trees";
        wrecks_ = root + " wrecks";
        streetPlants_ = root + " street plants";
        for (const auto* g : {&kerbs_, &lamps_, &streetTrees_, &wrecks_, &streetPlants_})
            b_.group(*g, streetGroup_);

        // Road surface under everything, pavements on top.
        const Rect all = extent();
        if (!b_.kit_.road.empty())
            b_.box("Road", groundGroup_, b_.kit_.road, all.centre(), -0.1f, {all.w(), 0.2f, all.h()});
        else
            b_.plan.warnings.push_back("The kit has no road material.");
        outskirts(all);
        for (int c = 0; c <= C_; ++c)
            if (s_.streetsX[static_cast<std::size_t>(c)] != StreetClass::None)
                b_.line({lx_[static_cast<std::size_t>(c)], all.v0}, {lx_[static_cast<std::size_t>(c)], all.v1}, kColStreet, 0.2f);
        for (int r = 0; r <= R_; ++r)
            if (s_.streetsZ[static_cast<std::size_t>(r)] != StreetClass::None)
                b_.line({all.u0, lz_[static_cast<std::size_t>(r)]}, {all.u1, lz_[static_cast<std::size_t>(r)]}, kColStreet, 0.2f);

        // Buildings first (they block plants), then the streets, then the blocks' greenery.
        std::vector<BlockState> blocks;
        for (int r = 0; r < R_; ++r)
            for (int c = 0; c < C_; ++c)
                blocks.push_back(layBlock(r, c));
        for (auto& bl : blocks)
            dressEdges(bl);
        streetSegments(blocks);
        for (auto& bl : blocks)
            green(bl);

        auto& p = b_.plan;
        p.boundsMin = glm::vec3(1e9f);
        p.boundsMax = glm::vec3(-1e9f);
        const float o = s_.outskirts;
        for (const glm::vec2 corner : {glm::vec2{all.u0 - o, all.v0 - o}, glm::vec2{all.u1 + o, all.v0 - o}, glm::vec2{all.u0 - o, all.v1 + o},
                                       glm::vec2{all.u1 + o, all.v1 + o}}) {
            const glm::vec3 w = b_.world(corner, 0.0f);
            p.boundsMin = glm::min(p.boundsMin, w);
            p.boundsMax = glm::max(p.boundsMax, w + glm::vec3(0.0f, 1.0f, 0.0f));
        }
        return std::move(b_.plan);
    }

private:
    struct BlockState {
        int r = 0, c = 0;
        BlockKind kind = BlockKind::Perimeter;
        Rect lot{}, pave{}, inner{};
        std::array<Edge, 4> edges{};
        std::string group, label;
        std::uint64_t seed = 0;
        std::vector<glm::vec2> gateways;            // gateway centres on the facade line
        std::vector<glm::vec2> entrances;           // slab fronts (paths to the street)
        std::array<bool, 4> built{};                 // edge has a street wall
        float groundY = 0.15f;                       // top of the ground plants stand on
    };

    float width(StreetClass c) const { return s_.profiles[static_cast<std::size_t>(c)].width(); }
    const StreetProfile& profile(StreetClass c) const { return s_.profiles[static_cast<std::size_t>(c)]; }

    void grid()
    {
        C_ = static_cast<int>(s_.columns.size());
        R_ = static_cast<int>(s_.rows.size());
        lx_.assign(static_cast<std::size_t>(C_ + 1), 0.0f);
        lz_.assign(static_cast<std::size_t>(R_ + 1), 0.0f);
        for (int i = 0; i < C_; ++i)
            lx_[static_cast<std::size_t>(i + 1)] = lx_[static_cast<std::size_t>(i)] + width(s_.streetsX[static_cast<std::size_t>(i)]) * 0.5f +
                                                   s_.columns[static_cast<std::size_t>(i)] + width(s_.streetsX[static_cast<std::size_t>(i + 1)]) * 0.5f;
        for (int i = 0; i < R_; ++i)
            lz_[static_cast<std::size_t>(i + 1)] = lz_[static_cast<std::size_t>(i)] + width(s_.streetsZ[static_cast<std::size_t>(i)]) * 0.5f +
                                                   s_.rows[static_cast<std::size_t>(i)] + width(s_.streetsZ[static_cast<std::size_t>(i + 1)]) * 0.5f;
        const float cu = (lx_.front() - width(s_.streetsX.front()) * 0.5f + lx_.back() + width(s_.streetsX.back()) * 0.5f) * 0.5f;
        const float cv = (lz_.front() - width(s_.streetsZ.front()) * 0.5f + lz_.back() + width(s_.streetsZ.back()) * 0.5f) * 0.5f;
        for (float& x : lx_)
            x -= cu;
        for (float& z : lz_)
            z -= cv;
    }

    Rect extent() const
    {
        return {lx_.front() - width(s_.streetsX.front()) * 0.5f, lz_.front() - width(s_.streetsZ.front()) * 0.5f,
                lx_.back() + width(s_.streetsX.back()) * 0.5f, lz_.back() + width(s_.streetsZ.back()) * 0.5f};
    }

    BlockState layBlock(int r, int c)
    {
        BlockState bl;
        bl.r = r;
        bl.c = c;
        bl.groundY = s_.kerbHeight;
        const BlockSettings bs = s_.block(r, c);
        bl.kind = bs.kind;
        bl.seed = mix(mix(s_.seed, static_cast<std::uint64_t>(r * 1000 + c)), bs.seed);
        bl.label = std::string(1, static_cast<char>('A' + c % 26)) + std::to_string(r + 1);
        bl.group = s_.group + " " + bl.label;
        b_.group(bl.group, s_.group);
        const auto sx = [&](int i) { return s_.streetsX[static_cast<std::size_t>(i)]; };
        const auto sz = [&](int i) { return s_.streetsZ[static_cast<std::size_t>(i)]; };
        const float u0 = lx_[static_cast<std::size_t>(c)] + width(sx(c)) * 0.5f, u1 = lx_[static_cast<std::size_t>(c + 1)] - width(sx(c + 1)) * 0.5f;
        const float v0 = lz_[static_cast<std::size_t>(r)] + width(sz(r)) * 0.5f, v1 = lz_[static_cast<std::size_t>(r + 1)] - width(sz(r + 1)) * 0.5f;
        bl.lot = {u0, v0, u1, v1};
        bl.pave = {u0 - profile(sx(c)).pavement, v0 - profile(sz(r)).pavement, u1 + profile(sx(c + 1)).pavement, v1 + profile(sz(r + 1)).pavement};
        // Edges in order around the lot, each starting where the previous one ends.
        bl.edges[0] = {{u0, v1}, {1, 0}, {0, 1}, u1 - u0, sz(r + 1), C_ + 1 + r + 1, true};
        bl.edges[1] = {{u1, v1}, {0, -1}, {1, 0}, v1 - v0, sx(c + 1), c + 1, false};
        bl.edges[2] = {{u1, v0}, {-1, 0}, {0, -1}, u1 - u0, sz(r), C_ + 1 + r, true};
        bl.edges[3] = {{u0, v0}, {0, 1}, {-1, 0}, v1 - v0, sx(c), c, false};
        b_.rectLines(bl.lot, kColLot);

        const float kh = s_.kerbHeight;
        if (!b_.kit_.pavement.empty())
            b_.box("Pavement " + bl.label, groundGroup_, b_.kit_.pavement, bl.pave.centre(), kh - 0.15f, {bl.pave.w(), 0.3f, bl.pave.h()});
        const bool bare = bl.kind == BlockKind::Ruin || bl.kind == BlockKind::Garages || bl.kind == BlockKind::Wasteland;
        const fs::path& ground = bare && !b_.kit_.lot.empty() ? b_.kit_.lot : b_.kit_.yard;
        if (!ground.empty())
            b_.box("Ground " + bl.label, groundGroup_, ground, bl.lot.centre(), kh - 0.13f, {bl.lot.w(), 0.3f, bl.lot.h()});

        bl.inner = bl.lot;
        Rng rng(bl.seed);
        switch (bl.kind) {
        case BlockKind::Perimeter: perimeter(bl, rng); break;
        case BlockKind::Microdistrict: microdistrict(bl, rng, "panel"); break;
        case BlockKind::Towers: towers(bl, rng); break;
        case BlockKind::Garages: garages(bl, rng); break;
        default: break;
        }
        return bl;
    }

    // ------------------------------------------------------------ perimeter block
    void perimeter(BlockState& bl, Rng& rng)
    {
        const auto kit = b_.kit_.buildingsWithRole("tenement");
        if (kit.empty()) {
            b_.plan.warnings.push_back("Block " + bl.label + ": the kit has no tenement houses.");
            return;
        }
        float D = 0.0f;
        for (const auto* k : kit)
            D = std::max(D, k->depth);
        const float corner = D - s_.overlap;
        FrontageFiller filler(kit, s_, rng);
        int prevFloors = 0;
        for (int e = 0; e < 4; ++e) {
            const Edge& E = bl.edges[static_cast<std::size_t>(e)];
            if (E.cls == StreetClass::None)
                continue;
            const Edge& next = bl.edges[static_cast<std::size_t>((e + 1) % 4)];
            const float F = E.length - (next.cls != StreetClass::None ? corner : 0.0f);
            if (F < 6.0f)
                continue;
            Fill f = filler.fill(F, true, prevFloors);
            if (f.houses.empty()) {
                b_.plan.warnings.push_back("Block " + bl.label + ": frontage of " + std::to_string(static_cast<int>(F)) +
                                           " m is shorter than every tenement.");
                continue;
            }
            if (!f.exact)
                b_.plan.warnings.push_back("Block " + bl.label + ": frontage misses by " + std::to_string(f.error) + " m.");
            b_.plan.stats.worstFill = std::max(b_.plan.stats.worstFill, f.error);
            b_.plan.stats.frontage += F;
            bl.built[static_cast<std::size_t>(e)] = true;
            float sPos = 0.0f;
            for (std::size_t i = 0; i < f.houses.size(); ++i) {
                const KitBuilding& h = *f.houses[i];
                const glm::vec2 c = E.start + E.t * (sPos + h.length * 0.5f);
                b_.building(h, bl.group, c - E.n * h.front, b_.yawZ(E.n));
                const glm::vec2 a = E.start + E.t * sPos, z = E.start + E.t * (sPos + h.length) - E.n * h.depth;
                const Rect fp{std::min(a.x, z.x), std::min(a.y, z.y), std::max(a.x, z.x), std::max(a.y, z.y)};
                b_.addSolid(fp);
                b_.rectLines(fp, kColHouse, 0.5f);
                sPos += h.length;
                if (i + 1 < f.houses.size()) {
                    const float g = f.gaps[i];
                    if (g > 0.0f) {
                        gateway(bl, E, sPos, g, D, rng);
                        sPos += g;
                    } else {
                        sPos -= s_.overlap;
                    }
                }
                prevFloors = h.floors;
            }
        }
        // Courtyard: the lot minus the house depth on every built side.
        Rect in = bl.lot;
        if (bl.built[0]) in.v1 -= D;
        if (bl.built[1]) in.u1 -= D;
        if (bl.built[2]) in.v0 += D;
        if (bl.built[3]) in.u0 += D;
        bl.inner = in;
        if (in.w() > 4.0f && in.h() > 4.0f)
            b_.rectLines(in, kColLot, 0.3f);
        // Paths from the gateways to the middle of the yard stay trodden.
        for (const glm::vec2 g : bl.gateways)
            b_.addClear(g, in.w() > 4.0f && in.h() > 4.0f ? in.centre() : g, 1.3f);
        if (s_.courtyardGarages && in.w() >= 34.0f && in.h() >= 34.0f)
            courtyardGarages(bl, rng);
    }

    void gateway(BlockState& bl, const Edge& E, float s, float g, float D, Rng& rng)
    {
        ++b_.plan.stats.gateways;
        const glm::vec2 c = E.start + E.t * (s + g * 0.5f);
        bl.gateways.push_back(c);
        b_.line(E.start + E.t * s, E.start + E.t * (s + g), kColGate, 0.6f);
        // The gap reaches back into the yard: nothing grows in the way.
        b_.addClear(c - E.n * 0.5f, c - E.n * (D + 1.0f), std::min(1.4f, g * 0.3f));
        const float kh = s_.kerbHeight;
        if (!b_.kit_.pillar.empty())
            for (const float side : {s + 0.3f, s + g - 0.3f})
                b_.box("Gate pillar", bl.group, b_.kit_.pillar, E.start + E.t * side - E.n * 0.3f, kh + 1.3f, {0.6f, 2.6f, 0.6f},
                       E.alongU ? 0.0f : 90.0f);
        if (b_.kit_.gate.empty() || g < 2.0f)
            return;
        // Two rusty gate leaves; one shut, one hanging open into the yard (or both open, or one missing).
        const float leaf = (g - 1.2f) * 0.5f;
        const int state = rng.below(4);
        for (int i = 0; i < 2; ++i) {
            if (state == 3 && i == 1)
                continue; // leaf gone
            const float hingeS = i == 0 ? s + 0.6f : s + g - 0.6f;
            const glm::vec2 hinge = E.start + E.t * hingeS - E.n * 0.3f;
            const glm::vec2 along = E.t * (i == 0 ? 1.0f : -1.0f);
            const float open = (state == 1 || (state == 0 && i == 1) || state == 3) ? glm::radians(rng.range(35.0f, 100.0f)) : 0.0f;
            const glm::vec2 dir = along * std::cos(open) - E.n * std::sin(open);
            const float localYaw = deg(std::atan2(-dir.y, dir.x));
            b_.box("Gate leaf", bl.group, b_.kit_.gate, hinge + dir * (leaf * 0.5f), kh + 1.15f, {leaf, 2.1f, 0.05f}, localYaw);
        }
    }

    void courtyardGarages(BlockState& bl, Rng& rng)
    {
        const auto kit = b_.kit_.buildingsWithRole("garage");
        if (kit.empty())
            return;
        const KitBuilding& g = *kit.front();
        const Rect in = bl.inner;
        // Along the longer side of the yard, 4 m off the back walls, doors into the yard.
        const bool alongU = in.w() >= in.h();
        const float len = alongU ? in.w() : in.h();
        const int n = static_cast<int>((len - 12.0f) / g.length);
        if (n < 2)
            return;
        const bool farSide = rng.chance(0.5f);
        const float run = static_cast<float>(n) * g.length;
        for (int i = 0; i < n; ++i) {
            const float a = -run * 0.5f + g.length * (static_cast<float>(i) + 0.5f);
            glm::vec2 c, face;
            if (alongU) {
                face = {0.0f, farSide ? -1.0f : 1.0f};
                const float v = farSide ? in.v1 - 4.0f - (g.depth - g.front) : in.v0 + 4.0f + (g.depth - g.front);
                c = {in.centre().x + a, v};
            } else {
                face = {farSide ? -1.0f : 1.0f, 0.0f};
                const float u = farSide ? in.u1 - 4.0f - (g.depth - g.front) : in.u0 + 4.0f + (g.depth - g.front);
                c = {u, in.centre().y + a};
            }
            const glm::vec2 h = alongU ? glm::vec2(g.length * 0.5f, g.depth * 0.5f) : glm::vec2(g.depth * 0.5f, g.length * 0.5f);
            const glm::vec2 mid = c + face * (g.front - g.depth * 0.5f);
            const Rect fp{mid.x - h.x, mid.y - h.y, mid.x + h.x, mid.y + h.y};
            if (b_.crossesPath(fp))
                continue; // the row leaves a passage where people walk to the gateway
            b_.building(g, bl.group, c, b_.yawZ(face));
            b_.addSolid(fp);
            b_.rectLines(fp, kColHouse, 0.5f);
        }
    }

    // ------------------------------------------------------------ micro-district
    void microdistrict(BlockState& bl, Rng& rng, const char* role)
    {
        auto kit = b_.kit_.buildingsWithRole(role);
        if (kit.empty()) {
            b_.plan.warnings.push_back("Block " + bl.label + ": the kit has no " + std::string(role) + " buildings.");
            return;
        }
        const Rect L = bl.lot;
        const bool alongU = L.w() >= L.h();
        const float A = (alongU ? L.w() : L.h()) - 2.0f * s_.setback; // along the slabs
        const float B = (alongU ? L.h() : L.w()) - 2.0f * s_.setback; // across the rows
        // The module that builds the most frontage in exact rows wins; others alternate in.
        struct Fit {
            const KitBuilding* b;
            int perRow;
        };
        std::vector<Fit> fits;
        for (const auto* b : kit) {
            const int perRow = static_cast<int>((A + s_.slabGap) / (b->length + s_.slabGap));
            if (perRow >= 1 && b->depth <= B)
                fits.push_back({b, perRow});
        }
        if (fits.empty()) {
            b_.plan.warnings.push_back("Block " + bl.label + ": too small for any slab (" + std::to_string(static_cast<int>(A)) + " x " +
                                       std::to_string(static_cast<int>(B)) + " m inside the setback).");
            return;
        }
        std::sort(fits.begin(), fits.end(), [](const Fit& a, const Fit& b) {
            return static_cast<float>(a.perRow) * a.b->length > static_cast<float>(b.perRow) * b.b->length;
        });
        // Rows: as many as fit with the gap the taller neighbour needs (sunlight between slabs).
        std::vector<const Fit*> rows;
        float used = 0.0f;
        for (int i = 0;; ++i) {
            const Fit& f = fits[static_cast<std::size_t>(i % static_cast<int>(std::min<std::size_t>(fits.size(), 2)))];
            float need = f.b->depth;
            if (!rows.empty())
                need += std::max(s_.rowGapMin, s_.rowGapFactor * std::max(f.b->height, rows.back()->b->height));
            if (used + need > B + 0.01f)
                break;
            used += need;
            rows.push_back(&f);
            if (rows.size() > 20)
                break;
        }
        if (rows.empty())
            return;
        // Tallest rows to the north (back), lower ones to the south, so the yard gets sun.
        std::stable_sort(rows.begin(), rows.end(), [](const Fit* a, const Fit* b) { return a->b->height > b->b->height; });
        used = rows.front()->b->depth;
        for (std::size_t i = 1; i < rows.size(); ++i)
            used += rows[i]->b->depth + std::max(s_.rowGapMin, s_.rowGapFactor * std::max(rows[i]->b->height, rows[i - 1]->b->height));
        float across = (alongU ? L.centre().y : L.centre().x) - used * 0.5f;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const Fit& f = *rows[i];
            if (i > 0)
                across += std::max(s_.rowGapMin, s_.rowGapFactor * std::max(f.b->height, rows[i - 1]->b->height));
            const float mid = across + f.b->depth * 0.5f;
            const float run = static_cast<float>(f.perRow) * f.b->length + static_cast<float>(f.perRow - 1) * s_.slabGap;
            const float a0 = (alongU ? L.centre().x : L.centre().y) - run * 0.5f;
            const glm::vec2 face = alongU ? glm::vec2(0.0f, 1.0f) : glm::vec2(1.0f, 0.0f); // fronts to the south / east
            for (int k = 0; k < f.perRow; ++k) {
                const float along = a0 + static_cast<float>(k) * (f.b->length + s_.slabGap) + f.b->length * 0.5f;
                const glm::vec2 centre = alongU ? glm::vec2(along, mid) : glm::vec2(mid, along);
                const glm::vec2 origin = centre - face * (f.b->front - f.b->depth * 0.5f);
                b_.building(*f.b, bl.group, origin, b_.yawZ(face));
                const glm::vec2 h = alongU ? glm::vec2(f.b->length * 0.5f, f.b->depth * 0.5f) : glm::vec2(f.b->depth * 0.5f, f.b->length * 0.5f);
                const Rect fp{centre.x - h.x, centre.y - h.y, centre.x + h.x, centre.y + h.y};
                b_.addSolid(fp);
                b_.rectLines(fp, kColHouse, 0.5f);
                bl.entrances.push_back(centre + face * (f.b->depth * 0.5f));
            }
            across += f.b->depth;
        }
        (void)rng;
    }

    void towers(BlockState& bl, Rng& rng)
    {
        auto kit = b_.kit_.buildingsWithRole("tower");
        if (kit.empty()) {
            microdistrict(bl, rng, "panel");
            return;
        }
        const Rect L = bl.lot;
        const KitBuilding& t = *kit[static_cast<std::size_t>(rng.below(static_cast<int>(kit.size())))];
        const float gap = std::max(s_.rowGapMin, 0.5f * t.height);
        const float w = L.w() - 2.0f * s_.setback, h = L.h() - 2.0f * s_.setback;
        const int nu = std::max(1, std::min(2, static_cast<int>((w + gap) / (t.length + gap))));
        const int nv = std::max(1, std::min(2, static_cast<int>((h + gap) / (t.depth + gap))));
        if (t.length > w || t.depth > h) {
            b_.plan.warnings.push_back("Block " + bl.label + ": too small for the tower " + t.name + ".");
            return;
        }
        for (int i = 0; i < nu; ++i)
            for (int j = 0; j < nv; ++j) {
                const float u = L.centre().x + (static_cast<float>(i) - (static_cast<float>(nu) - 1.0f) * 0.5f) * (t.length + gap);
                const float v = L.centre().y + (static_cast<float>(j) - (static_cast<float>(nv) - 1.0f) * 0.5f) * (t.depth + gap);
                const KitBuilding& tw = *kit[static_cast<std::size_t>((i + j + rng.below(2)) % static_cast<int>(kit.size()))];
                const glm::vec2 face{0.0f, 1.0f};
                b_.building(tw, bl.group, glm::vec2(u, v) - face * (tw.front - tw.depth * 0.5f), b_.yawZ(face));
                const Rect fp{u - tw.length * 0.5f, v - tw.depth * 0.5f, u + tw.length * 0.5f, v + tw.depth * 0.5f};
                b_.addSolid(fp);
                b_.rectLines(fp, kColHouse, 0.5f);
                bl.entrances.push_back({u, v + tw.depth * 0.5f});
            }
    }

    void garages(BlockState& bl, Rng& rng)
    {
        const auto kit = b_.kit_.buildingsWithRole("garage");
        if (kit.empty()) {
            b_.plan.warnings.push_back("Block " + bl.label + ": the kit has no garages.");
            return;
        }
        const KitBuilding& g = *kit.front();
        const Rect L = bl.lot;
        const float aisle = 8.0f, margin = 5.0f;
        const int perRow = static_cast<int>((L.w() - 2.0f * margin) / g.length);
        const int rows = static_cast<int>((L.h() - 2.0f * margin + aisle) / (g.depth + aisle));
        if (perRow < 1 || rows < 1)
            return;
        const float run = static_cast<float>(perRow) * g.length;
        const float depthAll = static_cast<float>(rows) * g.depth + static_cast<float>(rows - 1) * aisle;
        for (int r = 0; r < rows; ++r) {
            const float v = L.centre().y - depthAll * 0.5f + static_cast<float>(r) * (g.depth + aisle) + g.depth * 0.5f;
            const glm::vec2 face{0.0f, (r % 2) ? -1.0f : 1.0f};
            for (int i = 0; i < perRow; ++i) {
                const float u = L.centre().x - run * 0.5f + g.length * (static_cast<float>(i) + 0.5f);
                b_.building(g, bl.group, glm::vec2(u, v) - face * (g.front - g.depth * 0.5f), b_.yawZ(face));
            }
            const Rect fp{L.centre().x - run * 0.5f, v - g.depth * 0.5f, L.centre().x + run * 0.5f, v + g.depth * 0.5f};
            b_.addSolid(fp);
            b_.rectLines(fp, kColHouse, 0.5f);
            if (r + 1 < rows) // the aisle stays drivable
                b_.addClear({fp.u0 + 2.0f, v + g.depth * 0.5f + aisle * 0.5f}, {fp.u1 - 2.0f, v + g.depth * 0.5f + aisle * 0.5f}, 2.0f);
        }
        (void)rng;
    }

    // ------------------------------------------------------------ street edges
    // Kerbs, lamps, planted trees and the plants along each side of each street.
    void dressEdges(BlockState& bl)
    {
        Rng rng(mix(bl.seed, 77));
        const float kh = s_.kerbHeight;
        for (int e = 0; e < 4; ++e) {
            const Edge& E = bl.edges[static_cast<std::size_t>(e)];
            if (E.cls == StreetClass::None)
                continue;
            const StreetProfile& P = profile(E.cls);
            const float pw = P.pavement;
            // Pavement edge runs corner to corner of the pavement rectangle.
            const float ext0 = profile(bl.edges[static_cast<std::size_t>((e + 3) % 4)].cls).pavement;
            const float ext1 = profile(bl.edges[static_cast<std::size_t>((e + 1) % 4)].cls).pavement;
            const glm::vec2 kerbStart = E.start + E.n * pw - E.t * ext0;
            const float kerbLen = E.length + ext0 + ext1;
            // Kerbs: whole modules, the remainder split at both ends.
            if (const auto kerbs = b_.kit_.propsWithRole("kerb"); !kerbs.empty() && pw > 0.0f) {
                const float m = kerbs.front()->size > 0.0f ? kerbs.front()->size : 6.0f;
                const int n = static_cast<int>(kerbLen / m);
                const float off = (kerbLen - static_cast<float>(n) * m) * 0.5f;
                for (int i = 0; i < n; ++i) {
                    const KitProp& k = *kerbs[static_cast<std::size_t>(rng.below(static_cast<int>(kerbs.size())))];
                    b_.prop(k, "Kerb", kerbs_, kerbStart + E.t * (off + m * (static_cast<float>(i) + 0.5f)), 0.0f, b_.yawZ(E.n), rng,
                            {0.0f, 0.0f}, 1.0f);
                }
            }
            // Positions along the street in the street's own coordinate, so both sides
            // of a street line up across blocks: lamps staggered, trees opposite each other.
            const float base = E.alongU ? E.start.x : E.start.y;
            const float sign = E.alongU ? E.t.x : E.t.y;
            auto streetCoordToS = [&](float coord) { return (coord - base) * sign; };
            const bool firstSide = (e == 0 || e == 1);
            std::vector<float> lampS;
            if (P.lamps && pw >= 1.0f) {
                const float off = firstSide ? 0.0f : s_.lampSpacing * 0.5f;
                const float lo = std::min(base, base + sign * E.length), hi = std::max(base, base + sign * E.length);
                const float k0 = std::ceil((lo + 4.0f - off) / s_.lampSpacing);
                std::vector<glm::vec3> tops;
                for (float k = k0;; k += 1.0f) {
                    const float coord = k * s_.lampSpacing + off;
                    if (coord > hi - 4.0f)
                        break;
                    const float sPos = streetCoordToS(coord);
                    lampS.push_back(sPos);
                    const glm::vec2 p = E.start + E.t * sPos + E.n * (pw - 0.7f);
                    const KitProp* lamp = rng.chance(0.25f) ? b_.anyProp("lamp_leaning", rng) : nullptr;
                    if (!lamp)
                        lamp = b_.anyProp("lamp", rng);
                    if (!lamp)
                        break;
                    b_.prop(*lamp, "Lamp post", lamps_, p, kh - 0.02f, b_.yawX(E.n) + rng.range(-5.0f, 5.0f), rng);
                    b_.addSolid({p.x - 0.3f, p.y - 0.3f, p.x + 0.3f, p.y + 0.3f});
                    ++b_.plan.stats.lamps;
                    tops.push_back(b_.world(p, kh + 8.1f));
                }
                for (std::size_t i = 1; i < tops.size(); ++i)
                    if (rng.chance(s_.cableChance))
                        b_.cable(tops[i - 1], tops[i], lamps_);
            }
            // Planted row of one species per street; decades later some trees are gone.
            if (P.trees && pw >= 2.5f) {
                Rng species(mix(s_.seed, static_cast<std::uint64_t>(E.line) + 991));
                const KitProp* sp = b_.anyProp("tree", species);
                const float lo = std::min(base, base + sign * E.length), hi = std::max(base, base + sign * E.length);
                const float k0 = std::ceil((lo + 5.0f) / s_.treeSpacing);
                for (float k = k0; sp; k += 1.0f) {
                    const float coord = k * s_.treeSpacing + s_.treeSpacing * 0.5f;
                    if (coord > hi - 5.0f)
                        break;
                    const float sPos = streetCoordToS(coord);
                    if (std::any_of(lampS.begin(), lampS.end(), [&](float l) { return std::abs(l - sPos) < 3.0f; }))
                        continue;
                    const glm::vec2 p = E.start + E.t * sPos + E.n * (pw - 1.1f);
                    Rng tr(mix(bl.seed, static_cast<std::uint64_t>(e * 100000) + static_cast<std::uint64_t>(k + 50000.0f)));
                    if (tr.chance(s_.treeSurvival)) {
                        b_.tree(*sp, streetTrees_, p, tr, kh);
                        b_.line(p, p + E.t * 0.8f, kColTree, 0.7f);
                        const int n = static_cast<int>(tr.range(0.0f, 3.0f) * s_.overgrowth);
                        b_.seedlings(p, n, streetTrees_, tr, kh, &bl.pave);
                    } else if (tr.chance(0.35f)) {
                        if (const KitProp* snag = b_.anyProp("snag", tr))
                            b_.prop(*snag, "Dead tree", streetTrees_, p, kh, tr.range(0.0f, 360.0f), tr);
                    } else if (tr.chance(s_.overgrowth)) {
                        b_.plant({"sapling"}, "Seedling", streetTrees_, p, tr, kh); // a seedling took the empty pit
                    }
                }
            }
            // Low plants in the joint between the kerb and the road, and in pavement cracks.
            if (s_.overgrowth > 0.01f && pw > 0.0f) {
                const float step = 0.8f / s_.overgrowth;
                for (float sPos = rng.range(0.0f, step); sPos < kerbLen; sPos += step * rng.range(0.5f, 1.5f)) {
                    const glm::vec2 p = kerbStart + E.t * sPos + E.n * rng.range(0.1f, 0.55f);
                    if (rng.chance(0.2f * s_.overgrowth)) // a tuft spreading from the joint into the road
                        b_.plant({"meadow", "grass"}, "Kerb grass", streetPlants_, kerbStart + E.t * sPos + E.n * rng.range(0.7f, 1.3f), rng, 0.0f, 0.1f);
                    else
                        b_.plant({rng.chance(0.75f) ? "grass" : "weed", "grass"}, "Kerb grass", streetPlants_, p, rng, 0.0f, 0.1f);
                    if (rng.chance(0.3f * s_.overgrowth))
                        b_.plant({"grass", "weed"}, "Pavement grass", streetPlants_,
                                 kerbStart + E.t * (sPos + 0.4f) - E.n * rng.range(0.2f, std::max(0.3f, pw - 0.4f)), rng, kh, 0.1f);
                }
            }
            // Street wall: nettles and weeds at the foot, rubble that fell from cornices and balconies.
            if (bl.built[static_cast<std::size_t>(e)]) {
                if (s_.overgrowth > 0.01f) {
                    const float step = 2.6f / s_.overgrowth;
                    for (float sPos = rng.range(0.0f, step); sPos < E.length; sPos += step * rng.range(0.5f, 1.5f))
                        b_.plant({rng.chance(0.5f) ? "nettle" : "weed", "weed", "grass"}, "Wall weeds", bl.group,
                                 E.start + E.t * sPos + E.n * rng.range(0.25f, 0.8f), rng, kh, 0.05f);
                }
                const int nr = static_cast<int>(E.length / 100.0f * s_.rubblePer100m * rng.range(0.6f, 1.4f) + 0.5f);
                for (int i = 0; i < nr; ++i)
                    if (const KitProp* k = b_.anyProp("rubble", rng)) {
                        const glm::vec2 p = E.start + E.t * rng.range(2.0f, E.length - 2.0f) + E.n * rng.range(0.6f, std::max(0.7f, pw - 0.8f));
                        b_.prop(*k, "Rubble", bl.group, p, kh - 0.25f, rng.range(0.0f, 360.0f), rng, {0.0f, 0.0f}, rng.range(0.45f, 0.8f));
                    }
                // Bushes grow up in the gateways first.
                for (const glm::vec2 g : bl.gateways)
                    for (int i = 0; i < static_cast<int>(3.0f * s_.overgrowth); ++i)
                        b_.plant({"bush"}, "Bush", bl.group, g + E.n * rng.range(0.4f, 1.4f) + E.t * rng.range(-3.0f, 3.0f), rng, kh);
            }
        }
    }

    // ------------------------------------------------------------ street segments
    // Wrecks parked at the kerb and cables across, per stretch between two crossings.
    void streetSegments(const std::vector<BlockState>& blocks)
    {
        auto blockAt = [&](int r, int c) -> const BlockState* {
            if (r < 0 || c < 0 || r >= R_ || c >= C_)
                return nullptr;
            return &blocks[static_cast<std::size_t>(r * C_ + c)];
        };
        const float kh = s_.kerbHeight;
        // X lines (streets running north-south) per row, Z lines (east-west) per column.
        for (int pass = 0; pass < 2; ++pass) {
            const int lines = pass == 0 ? C_ + 1 : R_ + 1;
            const int segs = pass == 0 ? R_ : C_;
            for (int i = 0; i < lines; ++i) {
                const StreetClass cls = pass == 0 ? s_.streetsX[static_cast<std::size_t>(i)] : s_.streetsZ[static_cast<std::size_t>(i)];
                if (cls == StreetClass::None)
                    continue;
                const StreetProfile& P = profile(cls);
                const float axis = pass == 0 ? lx_[static_cast<std::size_t>(i)] : lz_[static_cast<std::size_t>(i)];
                for (int s = 0; s < segs; ++s) {
                    const BlockState* any = pass == 0 ? (blockAt(s, i) ? blockAt(s, i) : blockAt(s, i - 1)) : (blockAt(i, s) ? blockAt(i, s) : blockAt(i - 1, s));
                    if (!any)
                        continue;
                    const float a0 = pass == 0 ? any->lot.v0 : any->lot.u0, a1 = pass == 0 ? any->lot.v1 : any->lot.u1;
                    const glm::vec2 t = pass == 0 ? glm::vec2(0, 1) : glm::vec2(1, 0), n = pass == 0 ? glm::vec2(1, 0) : glm::vec2(0, 1);
                    auto at = [&](float along, float across) { return pass == 0 ? glm::vec2(axis + across, along) : glm::vec2(along, axis + across); };
                    Rng rng(mix(mix(s_.seed, static_cast<std::uint64_t>(pass * 1000 + i)), static_cast<std::uint64_t>(s) + 313));
                    const float len = a1 - a0;
                    // Wrecks: fewer on small streets.
                    const float density = s_.wrecksPer100m * (cls == StreetClass::Main ? 1.0f : cls == StreetClass::Side ? 0.7f : 0.3f);
                    const int count = static_cast<int>(len / 100.0f * density * rng.range(0.5f, 1.5f) + 0.5f);
                    std::vector<float> taken;
                    for (int w = 0, tries = 0; w < count && tries < count * 10; ++tries) {
                        const float along = rng.range(a0 + 6.0f, a1 - 6.0f);
                        if (std::any_of(taken.begin(), taken.end(), [&](float x) { return std::abs(x - along) < 10.0f; }))
                            continue;
                        const KitProp* car = b_.anyProp("car", rng);
                        if (!car || P.carriageway < 3.0f)
                            break;
                        taken.push_back(along);
                        ++w;
                        const float side = rng.chance(0.5f) ? 1.0f : -1.0f;
                        const glm::vec2 p = at(along, side * (P.carriageway * 0.5f - 1.15f));
                        const float yaw = b_.yawZ(t * (rng.chance(0.5f) ? 1.0f : -1.0f)) + rng.range(-7.0f, 7.0f);
                        b_.prop(*car, "Wreck", wrecks_, p, -0.05f, yaw, rng, {rng.range(-2.0f, 2.0f), rng.range(-2.5f, 2.5f)});
                        ++b_.plan.stats.wrecks;
                        // Decades in one place: grass packed around the wheels (an ellipse along the car).
                        for (int g = 0; g < static_cast<int>(8.0f * s_.overgrowth); ++g) {
                            const float ang = rng.range(0.0f, 2.0f * kPi), d = rng.range(1.0f, 1.25f);
                            b_.plant({"grass", "weed"}, "Grass", wrecks_, p + (t * std::cos(ang) * 2.5f + n * std::sin(ang) * 1.1f) * d, rng, 0.0f, 0.0f);
                        }
                        b_.addSolid({p.x - 0.8f, p.y - 0.8f, p.x + 0.8f, p.y + 0.8f});
                    }
                    // Cables across the street between two street walls.
                    if (P.crossCables) {
                        const BlockState* lo = pass == 0 ? blockAt(s, i - 1) : blockAt(i - 1, s);
                        const BlockState* hi = pass == 0 ? blockAt(s, i) : blockAt(i, s);
                        const bool walls = lo && hi && lo->built[pass == 0 ? 1u : 0u] && hi->built[pass == 0 ? 3u : 2u];
                        if (walls)
                            for (float along = a0 + 8.0f; along < a1 - 6.0f; along += s_.crossCableSpacing * rng.range(0.8f, 1.2f)) {
                                if (!rng.chance(0.75f))
                                    continue;
                                const float half = P.width() * 0.5f - 0.05f;
                                const float dz = rng.range(-3.0f, 3.0f);
                                b_.cable(b_.world(at(along, -half), kh + rng.range(7.0f, 11.0f)), b_.world(at(along + dz, half), kh + rng.range(7.0f, 11.0f)),
                                         lamps_);
                            }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ outskirts
    void outskirts(const Rect& all)
    {
        const float o = s_.outskirts;
        if (o < 1.0f)
            return;
        const std::string grp = s_.group + " outskirts";
        b_.group(grp, s_.group);
        const fs::path& ground = b_.kit_.yard.empty() ? b_.kit_.lot : b_.kit_.yard;
        const Rect ring[] = {{all.u0 - o, all.v0 - o, all.u1 + o, all.v0}, {all.u0 - o, all.v1, all.u1 + o, all.v1 + o},
                             {all.u0 - o, all.v0, all.u0, all.v1}, {all.u1, all.v0, all.u1 + o, all.v1}};
        if (!ground.empty())
            for (const Rect& r : ring)
                b_.box("Outskirts ground", groundGroup_, ground, r.centre(), -0.17f, {r.w(), 0.3f, r.h()});
        // The streets run on out of town as bare carriageways.
        Rng rng(mix(s_.seed, 5150));
        auto road = [&](glm::vec2 a, glm::vec2 b, float cw) {
            const Rect r{std::min(a.x, b.x) - (a.x == b.x ? cw * 0.5f : 0.0f), std::min(a.y, b.y) - (a.y == b.y ? cw * 0.5f : 0.0f),
                         std::max(a.x, b.x) + (a.x == b.x ? cw * 0.5f : 0.0f), std::max(a.y, b.y) + (a.y == b.y ? cw * 0.5f : 0.0f)};
            if (!b_.kit_.road.empty())
                b_.box("Road out of town", groundGroup_, b_.kit_.road, r.centre(), -0.1f, {r.w(), 0.2f, r.h()});
            b_.addClear(a, b, cw * 0.5f - 0.3f);
            // grass eats the edges of the old asphalt
            const glm::vec2 d = glm::normalize(b - a), n{-d.y, d.x};
            const float len = glm::length(b - a);
            for (float s = 0.0f; s < len && s_.overgrowth > 0.01f; s += 1.0f / s_.overgrowth) {
                const float side = rng.chance(0.5f) ? 1.0f : -1.0f;
                b_.plant({"meadow", "grass"}, "Verge grass", grp, a + d * s + n * side * (cw * 0.5f + rng.range(-0.8f, 0.6f)), rng, 0.0f, 0.0f);
            }
        };
        for (int c = 0; c <= C_; ++c)
            if (const StreetClass cls = s_.streetsX[static_cast<std::size_t>(c)]; cls != StreetClass::None) {
                const float u = lx_[static_cast<std::size_t>(c)], cw = profile(cls).carriageway;
                road({u, all.v0 - o}, {u, all.v0}, cw);
                road({u, all.v1}, {u, all.v1 + o}, cw);
            }
        for (int r = 0; r <= R_; ++r)
            if (const StreetClass cls = s_.streetsZ[static_cast<std::size_t>(r)]; cls != StreetClass::None) {
                const float v = lz_[static_cast<std::size_t>(r)], cw = profile(cls).carriageway;
                road({all.u0 - o, v}, {all.u0, v}, cw);
                road({all.u1, v}, {all.u1 + o, v}, cw);
            }
        // Young wood in groves over the waste ground, meadow between.
        BlockState pseudo;
        pseudo.group = grp;
        pseudo.groundY = -0.02f;
        for (const Rect& r : ring) {
            const Rect in{r.u0 + 3.0f, r.v0 + 3.0f, r.u1 - 3.0f, r.v1 - 3.0f};
            if (in.w() < 6.0f || in.h() < 6.0f)
                continue;
            std::vector<glm::vec2> centres;
            const int want = static_cast<int>(in.w() * in.h() / 900.0f * (0.3f + 0.7f * s_.overgrowth));
            for (int tries = 0; tries < want * 20 && static_cast<int>(centres.size()) < want; ++tries) {
                const glm::vec2 p{rng.range(in.u0, in.u1), rng.range(in.v0, in.v1)};
                if (std::any_of(centres.begin(), centres.end(), [&](glm::vec2 q) { return glm::length(q - p) < 24.0f; }))
                    continue;
                centres.push_back(p);
                grove(pseudo, p, 2 + rng.below(4), rng, in);
            }
            scatter(pseudo, in, 0.2f * s_.overgrowth, rng, {"sapling", "bush"}, "Young tree");
            scatter(pseudo, in, 5.0f * s_.overgrowth, rng, {"meadow", "grass"}, "Meadow");
        }
    }

    // ------------------------------------------------------------ greenery inside the blocks
    void grove(const BlockState& bl, glm::vec2 c, int trees, Rng& rng, const Rect& inside)
    {
        const float kh = bl.groundY;
        std::vector<glm::vec2> placed;
        for (int i = 0, tries = 0; i < trees && tries < trees * 12; ++tries) {
            const float a = rng.range(0.0f, 2.0f * kPi), d = rng.range(0.0f, 6.0f);
            const glm::vec2 p = c + glm::vec2(std::cos(a), std::sin(a)) * d;
            if (!inside.contains(p) || !b_.free(p, 1.5f))
                continue;
            if (std::any_of(placed.begin(), placed.end(), [&](glm::vec2 q) { return glm::length(q - p) < 4.0f; }))
                continue;
            if (const KitProp* k = b_.anyProp("tree", rng)) {
                b_.tree(*k, bl.group, p, rng, kh);
                placed.push_back(p);
                ++i;
            }
        }
        if (placed.empty())
            return;
        b_.line(c - glm::vec2(1, 0), c + glm::vec2(1, 0), kColTree, 0.7f);
        // Bushes on the rim, seedlings around (the grove spreads).
        const int bushes = static_cast<int>(rng.range(4.0f, 9.0f) * (0.3f + s_.overgrowth));
        for (int i = 0; i < bushes; ++i) {
            const float a = rng.range(0.0f, 2.0f * kPi), d = rng.range(5.5f, 9.0f);
            const glm::vec2 p = c + glm::vec2(std::cos(a), std::sin(a)) * d;
            if (inside.contains(p))
                b_.plant({"bush"}, "Bush", bl.group, p, rng, kh);
        }
        b_.seedlings(c, static_cast<int>(rng.range(2.0f, 7.0f) * s_.overgrowth), bl.group, rng, kh, &inside);
    }

    void scatter(const BlockState& bl, const Rect& area, float perHundredM2, Rng& rng, const std::vector<const char*>& roles, const char* name)
    {
        const int n = std::min(4000, static_cast<int>(area.w() * area.h() / 100.0f * perHundredM2));
        for (int i = 0; i < n; ++i)
            b_.plant(roles, name, bl.group, {rng.range(area.u0, area.u1), rng.range(area.v0, area.v1)}, rng, bl.groundY);
    }

    void green(BlockState& bl)
    {
        Rng rng(mix(bl.seed, 4242));
        const float g = s_.overgrowth;
        const float kh = s_.kerbHeight;
        const Rect in{bl.inner.u0 + 1.0f, bl.inner.v0 + 1.0f, bl.inner.u1 - 1.0f, bl.inner.v1 - 1.0f};
        if (in.w() < 3.0f || in.h() < 3.0f)
            return;
        switch (bl.kind) {
        case BlockKind::Perimeter: {
            // Groves in the corners of the yard, nettles at the back walls, grass where nobody walks.
            const glm::vec2 corners[] = {{in.u0 + 6, in.v0 + 6}, {in.u1 - 6, in.v0 + 6}, {in.u1 - 6, in.v1 - 6}, {in.u0 + 6, in.v1 - 6}};
            if (in.w() > 16.0f && in.h() > 16.0f)
                for (const auto& c : corners)
                    if (rng.chance(0.4f + 0.5f * g))
                        grove(bl, c, 1 + rng.below(3), rng, in);
            scatter(bl, in, 1.2f * g, rng, {"weed", "nettle", "grass"}, "Yard weeds");
            scatter(bl, in, 9.0f * g, rng, {"meadow", "grass"}, "Yard grass");
            break;
        }
        case BlockKind::Microdistrict:
        case BlockKind::Towers: {
            // Paths from the entrances straight to the nearest street, bushes under the balconies.
            for (const glm::vec2 e : bl.entrances) {
                const float dS = bl.lot.v1 - e.y, dE = bl.lot.u1 - e.x;
                const glm::vec2 to = dS <= dE ? glm::vec2(e.x, bl.lot.v1) : glm::vec2(bl.lot.u1, e.y);
                b_.addClear(e + glm::vec2(0.0f, 1.0f), to, 1.1f);
                for (int i = 0; i < static_cast<int>(6.0f * g); ++i)
                    b_.plant({"bush"}, "Bush", bl.group, e + glm::vec2(rng.range(-12.0f, 12.0f), rng.range(1.5f, 4.0f)), rng, kh);
            }
            const glm::vec2 corners[] = {{in.u0 + 7, in.v0 + 7}, {in.u1 - 7, in.v0 + 7}, {in.u1 - 7, in.v1 - 7}, {in.u0 + 7, in.v1 - 7}};
            for (const auto& c : corners)
                if (rng.chance(0.5f + 0.4f * g))
                    grove(bl, c, 2 + rng.below(4), rng, in);
            // Planted rows along the setback strip, decayed like the street rows.
            if (const KitProp* sp = b_.anyProp("tree", rng)) {
                for (float u = in.u0 + 4.0f; u < in.u1 - 4.0f; u += s_.treeSpacing * 1.5f)
                    for (const float v : {in.v0 + s_.setback * 0.5f, in.v1 - s_.setback * 0.5f})
                        if (rng.chance(s_.treeSurvival) && b_.free({u, v}, 2.0f)) {
                            b_.tree(*sp, bl.group, {u, v}, rng, kh);
                            b_.seedlings({u, v}, static_cast<int>(rng.range(0.0f, 3.0f) * g), bl.group, rng, kh, &in);
                        }
            }
            scatter(bl, in, 0.15f * g, rng, {"sapling", "bush"}, "Young tree");
            scatter(bl, in, 7.0f * g, rng, {"meadow", "grass"}, "Grass");
            scatter(bl, in, 1.0f * g, rng, {"grass", "weed"}, "Weeds");
            break;
        }
        case BlockKind::Park: {
            // Two trodden diagonals, groves between them.
            b_.addClear({in.u0, in.v0}, {in.u1, in.v1}, 1.2f);
            b_.addClear({in.u1, in.v0}, {in.u0, in.v1}, 1.2f);
            std::vector<glm::vec2> centres;
            for (int tries = 0; tries < 300; ++tries) {
                const glm::vec2 p{rng.range(in.u0 + 6, in.u1 - 6), rng.range(in.v0 + 6, in.v1 - 6)};
                if (std::any_of(centres.begin(), centres.end(), [&](glm::vec2 q) { return glm::length(q - p) < 22.0f; }))
                    continue;
                centres.push_back(p);
            }
            for (const auto& c : centres)
                grove(bl, c, 3 + rng.below(4), rng, in);
            scatter(bl, in, 0.25f * (0.4f + g), rng, {"sapling", "bush"}, "Young tree");
            scatter(bl, in, 8.0f * g, rng, {"meadow", "grass"}, "Grass");
            break;
        }
        case BlockKind::Ruin: {
            // The houses came down onto the street: heaps along the frontage, slabs, young birch wood behind.
            for (int e = 0; e < 4; ++e) {
                const Edge& E = bl.edges[static_cast<std::size_t>(e)];
                if (E.cls == StreetClass::None)
                    continue;
                for (float sPos = 4.0f; sPos < E.length - 4.0f; sPos += rng.range(6.0f, 10.0f)) {
                    const glm::vec2 p = E.start + E.t * sPos - E.n * rng.range(2.0f, 10.0f);
                    if (const KitProp* k = b_.anyProp(rng.chance(0.2f) ? "slab" : "rubble", rng))
                        b_.prop(*k, "Rubble heap", bl.group, p, kh - 0.25f, rng.range(0.0f, 360.0f), rng,
                                {rng.range(-8.0f, 8.0f), rng.range(-8.0f, 8.0f)}, rng.range(1.3f, 2.6f));
                    b_.addSolid({p.x - 2.0f, p.y - 2.0f, p.x + 2.0f, p.y + 2.0f});
                }
            }
            scatter(bl, in, 0.5f * (0.3f + g), rng, {"sapling", "bush"}, "Young tree");
            scatter(bl, in, 2.5f * g, rng, {"weed", "nettle", "grass"}, "Weeds");
            scatter(bl, in, 1.5f * g, rng, {"meadow", "grass"}, "Grass");
            break;
        }
        case BlockKind::Wasteland: {
            scatter(bl, in, 0.25f * (0.3f + g), rng, {"sapling", "bush"}, "Young tree");
            scatter(bl, in, 0.04f, rng, {"rubble"}, "Rubble");
            scatter(bl, in, 8.0f * g, rng, {"meadow", "grass"}, "Grass");
            scatter(bl, in, 2.0f * g, rng, {"weed", "nettle", "grass"}, "Weeds");
            if (rng.chance(0.7f))
                grove(bl, in.centre() + glm::vec2(rng.range(-10.0f, 10.0f), rng.range(-10.0f, 10.0f)), 2 + rng.below(3), rng, in);
            break;
        }
        case BlockKind::Garages: {
            scatter(bl, in, 1.5f * g, rng, {"weed", "grass"}, "Weeds");
            scatter(bl, in, 0.1f * g, rng, {"sapling", "bush"}, "Young tree");
            break;
        }
        case BlockKind::Empty:
            break;
        }
    }

    const Settings& s_;
    Builder b_;
    int C_ = 0, R_ = 0;
    std::vector<float> lx_, lz_;
    std::string groundGroup_, streetGroup_, kerbs_, lamps_, streetTrees_, wrecks_, streetPlants_;
};

} // namespace

Plan generate(const Settings& in, const Kit& kit)
{
    Settings s = in;
    s.normalize();
    CityLayout layout(s, kit);
    return layout.run();
}

// ============================================================================ objects
CityObjects buildObjects(const Plan& plan, const std::function<void(float)>& progress, const std::function<bool()>& cancelled)
{
    CityObjects out;
    for (const auto& g : plan.groups)
        out.groups.push_back({g.name, g.parent, {}});
    // Every model and LOD file once, in parallel.
    std::vector<fs::path> files;
    std::map<fs::path, std::size_t> index;
    for (const auto& p : plan.placements)
        if (p.kind == Placement::Kind::Model)
            for (const fs::path* f : {&p.path, &p.lodPath})
                if (!f->empty() && !index.count(*f)) {
                    index[*f] = files.size();
                    files.push_back(*f);
                }
    std::vector<std::shared_ptr<Model>> loaded(files.size());
    std::vector<std::string> errors(files.size());
    std::atomic<int> done{0};
    JobSystem::global().parallelFor(files.size(), 1, [&](std::size_t b, std::size_t e) {
        for (std::size_t i = b; i < e; ++i) {
            if (cancelled && cancelled())
                return;
            auto m = std::make_shared<Model>();
            if (loadModelFile(*m, files[i], &errors[i]))
                loaded[i] = std::move(m);
            if (progress)
                progress(0.8f * static_cast<float>(++done) / static_cast<float>(std::max<std::size_t>(files.size(), 1)));
        }
    });
    for (std::size_t i = 0; i < files.size(); ++i)
        if (!loaded[i])
            out.errors.push_back(files[i].string() + ": " + (errors[i].empty() ? "not loaded" : errors[i]));
    if (cancelled && cancelled())
        return out;
    std::map<fs::path, std::shared_ptr<Material>> materials;
    std::size_t k = 0;
    for (const auto& p : plan.placements) {
        if (progress && (++k % 512) == 0)
            progress(0.8f + 0.2f * static_cast<float>(k) / static_cast<float>(plan.placements.size()));
        if (p.kind == Placement::Kind::Box) {
            Box b;
            b.name = p.name;
            b.parent = p.group;
            b.position = p.position;
            b.size = p.size;
            b.rotation = p.rotation;
            if (!p.path.empty()) {
                auto it = materials.find(p.path);
                if (it == materials.end()) {
                    std::string err;
                    it = materials.emplace(p.path, loadMaterial(p.path, &err)).first;
                    if (!it->second)
                        out.errors.push_back(p.path.string() + ": " + err);
                }
                if (it->second) {
                    b.materialPath = p.path;
                    b.material = it->second;
                }
            }
            out.boxes.push_back(std::move(b));
            continue;
        }
        const auto& proto = loaded[index[p.path]];
        if (!proto)
            continue;
        Model m = *proto; // shares the mesh parts
        m.name = p.name;
        m.parent = p.group;
        m.position = p.position;
        m.rotation = p.rotation;
        m.scale = p.scale;
        m.cullDistance = p.cull;
        m.collision = p.collision;
        m.castShadows = p.shadows;
        m.sway = p.sway;
        m.impostorDistance = p.impostor;
        if (!p.lodPath.empty())
            if (const auto& lod = loaded[index[p.lodPath]]) {
                m.lod = lod;
                m.lodPath = p.lodPath;
                m.lodDistance = p.lodDistance;
            }
        out.models.push_back(std::move(m));
    }
    if (progress)
        progress(1.0f);
    return out;
}

int replaceCity(Scene& scene, const std::string& root, CityObjects objects)
{
    // Old generation: everything below the root group.
    int removed = 0;
    if (findGroup(scene, root) >= 0) {
        std::unordered_set<std::string> under; // groups below root (and root)
        for (const auto& g : scene.groups)
            if (isAncestor(scene, root, g.name))
                under.insert(g.name);
        auto inside = [&](const std::string& parent) {
            return !parent.empty() && (under.count(parent) || isAncestor(scene, root, parent));
        };
        const auto boxesBefore = scene.boxes.size(), modelsBefore = scene.models.size(), entitiesBefore = scene.entities.size();
        std::erase_if(scene.boxes, [&](const Box& b) { return inside(b.parent); });
        std::erase_if(scene.models, [&](const Model& m) { return inside(m.parent); });
        std::erase_if(scene.entities, [&](const Entity& e) { return inside(e.parent); });
        removed = static_cast<int>(boxesBefore - scene.boxes.size() + modelsBefore - scene.models.size() + entitiesBefore - scene.entities.size());
        std::erase_if(scene.groups, [&](const Group& g) { return under.count(g.name) > 0; });
    }
    // Names: unique against what stays in the scene.
    std::unordered_set<std::string> taken;
    for (const auto& g : scene.groups)
        taken.insert(g.name);
    for (const auto& b : scene.boxes)
        taken.insert(b.name);
    for (const auto& m : scene.models)
        taken.insert(m.name);
    for (const auto& e : scene.entities)
        taken.insert(e.name);
    std::map<std::string, std::string> renamedGroups;
    auto unique = [&](const std::string& base) {
        std::string n = base;
        for (int i = 2; taken.count(n); ++i)
            n = base + " #" + std::to_string(i);
        taken.insert(n);
        return n;
    };
    for (auto& g : objects.groups) {
        const std::string n = unique(g.name);
        if (n != g.name)
            renamedGroups[g.name] = n;
        g.name = n;
    }
    auto parentOf = [&](const std::string& p) {
        const auto it = renamedGroups.find(p);
        return it == renamedGroups.end() ? p : it->second;
    };
    for (auto& g : objects.groups) {
        g.parent = parentOf(g.parent);
        scene.groups.push_back(std::move(g));
    }
    for (auto& b : objects.boxes) {
        b.name = unique(b.name);
        b.parent = parentOf(b.parent);
        scene.boxes.push_back(std::move(b));
    }
    for (auto& m : objects.models) {
        m.name = unique(m.name);
        m.parent = parentOf(m.parent);
        scene.models.push_back(std::move(m));
    }
    assignObjectIds(scene);
    return removed;
}

} // namespace engine::city
