#include "engine/CityGen.h"

#include "engine/CityRoads.h"

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
constexpr const char* kBlockNames[] = {"perimeter", "microdistrict", "towers", "garages", "park", "ruin", "wasteland", "empty", "houses", "square", "industrial"};
constexpr const char* kBlockLabels[] = {"Perimeter block (tenements)", "Micro-district (panel slabs)", "Towers",
                                        "Garages", "Park", "Ruin", "Waste lot", "Empty lot", "Private houses", "Town square", "Industrial (factory halls)"};
constexpr const char* kGroundNames[] = {"yard", "lot", "road", "pavement", "meadow", "path", "built", "verge"};
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

const char* groundName(int g) { return g >= 0 && g < kGroundCount ? kGroundNames[g] : "yard"; }

const std::vector<std::string>& propRoles()
{
    static const std::vector<std::string> roles{"kerb",  "lamp",  "lamp_leaning", "cable", "car",    "tree",  "sapling",
                                                "snag",  "bush",  "grass",        "weed",  "nettle", "rubble", "slab",  "meadow",
                                                "monument", "fountain", "bench", "kiosk", "ivy", "roof_tree", "road_crack", "road_crack_cross", "road_net", "pothole", "fern", "hanging"};
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

std::vector<const FacadeStyle*> Kit::stylesWithRole(const std::string& role) const
{
    std::vector<const FacadeStyle*> out;
    for (const auto& st : styles)
        if (st.role == role)
            out.push_back(&st);
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
    roadDecay = std::clamp(roadDecay, 0.0f, 1.0f);
    outskirts = std::clamp(outskirts, 0.0f, 2000.0f);
    treeSurvival = std::clamp(treeSurvival, 0.0f, 1.0f);
    cableChance = std::clamp(cableChance, 0.0f, 1.0f);
    wrecksPer100m = std::clamp(wrecksPer100m, 0.0f, 20.0f);
    rubblePer100m = std::clamp(rubblePer100m, 0.0f, 50.0f);
    groundSpacing = std::clamp(groundSpacing, 0.25f, 4.0f);
    hills = std::clamp(hills, 0.0f, 100.0f);
    foliageDensity = std::clamp(foliageDensity, 0.0f, 4.0f);
    townSize = std::clamp(townSize, 100.0f, 3000.0f);
    districts = std::clamp(districts, 1, 8);
    districtTurn = std::clamp(districtTurn, 0.0f, 90.0f);
    radial = std::clamp(radial, 0.0f, 2.0f);
    curvature = std::clamp(curvature, 0.0f, 2.0f);
    noiseScale = std::clamp(noiseScale, 20.0f, 5000.0f);
    mainSpacing = std::clamp(mainSpacing, 40.0f, 1000.0f);
    sideSpacing = std::clamp(sideSpacing, 25.0f, mainSpacing);
    centreZone = std::clamp(centreZone, 0.0f, 1.0f);
    innerZone = std::clamp(innerZone, centreZone, 1.5f);
    ruinShare = std::clamp(ruinShare, 0.0f, 1.0f);
    skeletonShare = std::clamp(skeletonShare, 0.0f, 1.0f);
    landmarkFloors = std::clamp(landmarkFloors, 5.0f, 60.0f);
    parkShare = std::clamp(parkShare, 0.0f, 1.0f);
    houseShare = std::clamp(houseShare, 0.0f, 1.0f);
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
        } else if (t[0] == "textures" && t.size() >= 2) {
            kit.textures = resolve(dir, t[1]);
        } else if (t[0] == "style" && t.size() >= 2) {
            FacadeStyle st;
            st.name = t[1];
            const auto kv = pairs(t, 2, nullptr, nullptr);
            auto get = [&](const char* k, float d) { const auto it = kv.find(k); return it == kv.end() ? d : toFloat(it->second, d); };
            auto str = [&](const char* k, const std::string& d) { const auto it = kv.find(k); return it == kv.end() ? d : it->second; };
            st.role = str("role", st.role);
            st.floorsMin = static_cast<int>(get("floors_min", static_cast<float>(st.floorsMin)));
            st.floorsMax = std::max(st.floorsMin, static_cast<int>(get("floors_max", static_cast<float>(st.floorsMax))));
            st.groundStorey = get("ground", st.groundStorey);
            st.storey = get("storey", st.storey);
            st.bay = std::max(1.0f, get("bay", st.bay));
            st.windowWidth = get("window_w", st.windowWidth);
            st.windowHeight = get("window_h", st.windowHeight);
            st.sill = get("sill", st.sill);
            st.depthMin = get("depth_min", st.depthMin);
            st.depthMax = std::max(st.depthMin, get("depth_max", st.depthMax));
            st.widthMin = get("width_min", st.widthMin);
            st.widthMax = std::max(st.widthMin, get("width_max", st.widthMax));
            st.wall = str("wall", st.wall);
            st.plinth = str("plinth", st.plinth);
            st.trim = str("trim", st.trim);
            st.roof = str("roof", st.roof);
            st.firewall = str("firewall", st.firewall);
            st.arcade = std::clamp(get("arcade", st.arcade), 0.0f, 1.0f);
            st.fireEscapes = std::clamp(get("fire_escape", st.fireEscapes), 0.0f, 1.0f);
            st.cornice = get("cornice", st.cornice);
            st.bands = get("bands", st.bands);
            st.loggias = get("loggias", st.loggias);
            st.balconies = get("balconies", st.balconies);
            st.shops = get("shops", st.shops);
            st.pitched = get("pitched", st.pitched ? 1.0f : 0.0f) != 0.0f;
            st.weight = get("weight", st.weight);
            kit.styles.push_back(std::move(st));
        } else if (t[0] == "foliage" && t.size() >= 3) {
            KitFoliage f;
            f.name = t[1];
            f.path = resolve(dir, t[2]);
            std::string lod;
            const auto kv = pairs(t, 3, &lod, &f.lodDistance);
            f.lodPath = resolve(dir, lod);
            auto get = [&](const char* k, float d) { const auto it = kv.find(k); return it == kv.end() ? d : toFloat(it->second, d); };
            f.density = get("density", f.density);
            for (int g = 0; g < kGroundCount; ++g)
                f.on[static_cast<std::size_t>(g)] = std::max(0.0f, get(kGroundNames[g], f.on[static_cast<std::size_t>(g)]));
            f.cull = get("cull", f.cull);
            f.farDensity = get("far", f.farDensity);
            f.sway = get("sway", f.sway);
            f.scaleMin = get("scale_min", f.scaleMin);
            f.scaleMax = get("scale_max", std::max(f.scaleMin, f.scaleMax));
            f.collision = get("collision", 0.0f) != 0.0f;
            f.shadows = get("shadows", 0.0f) != 0.0f;
            kit.foliage.push_back(std::move(f));
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
    if (!kit.textures.empty())
        out << "\ntextures " << q(relPath(kit.textures, dir)) << '\n';
    if (!kit.styles.empty())
        out << "\n# style \"name\" role <tenement|panel|khrushchyovka|house> floors_min floors_max ground storey bay window_w window_h sill\n"
               "#   depth_min depth_max width_min width_max wall plinth trim roof firewall (grammar materials) cornice bands loggias balconies shops pitched weight [arcade] [fire_escape]\n";
    for (const auto& st : kit.styles) {
        out << "style " << q(st.name) << " role " << st.role << " floors_min " << st.floorsMin << " floors_max " << st.floorsMax << " ground "
            << st.groundStorey << " storey " << st.storey << " bay " << st.bay << " window_w " << st.windowWidth << " window_h " << st.windowHeight
            << " sill " << st.sill << " depth_min " << st.depthMin << " depth_max " << st.depthMax << " width_min " << st.widthMin << " width_max "
            << st.widthMax << " wall " << st.wall << " plinth " << st.plinth << " trim " << st.trim << " roof " << st.roof << " firewall "
            << st.firewall << " cornice " << st.cornice << " bands " << st.bands << " loggias " << st.loggias << " balconies " << st.balconies
            << " shops " << st.shops << " pitched " << (st.pitched ? 1 : 0) << " weight " << st.weight;
        if (st.arcade > 0.0f)
            out << " arcade " << st.arcade;
        if (st.fireEscapes > 0.0f)
            out << " fire_escape " << st.fireEscapes;
        out << '\n';
    }
    if (!kit.foliage.empty())
        out << "\n# foliage \"name\" \"model\" density per_m2 <ground> share ... cull far sway scale_min scale_max collision shadows [lod \"model\" m]\n";
    for (const auto& f : kit.foliage) {
        out << "foliage " << q(f.name) << ' ' << q(relPath(f.path, dir)) << " density " << f.density;
        for (int g = 0; g < kGroundCount; ++g)
            out << ' ' << kGroundNames[g] << ' ' << f.on[static_cast<std::size_t>(g)];
        out << " cull " << f.cull << " far " << f.farDensity << " sway " << f.sway << " scale_min " << f.scaleMin << " scale_max " << f.scaleMax
            << " collision " << (f.collision ? 1 : 0) << " shadows " << (f.shadows ? 1 : 0);
        if (!f.lodPath.empty())
            out << " lod " << q(relPath(f.lodPath, dir)) << ' ' << f.lodDistance;
        out << '\n';
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
    {"road_decay", &Settings::roadDecay},
    {"outskirts", &Settings::outskirts},
    {"terrain_ground", nullptr, nullptr, &Settings::terrainGround},
    {"ground_spacing", &Settings::groundSpacing},
    {"hills", &Settings::hills},
    {"foliage_density", &Settings::foliageDensity},
    {"town_size", &Settings::townSize},
    {"road_angle", &Settings::roadAngle},
    {"district_turn", &Settings::districtTurn},
    {"districts", nullptr, &Settings::districts},
    {"radial", &Settings::radial},
    {"curvature", &Settings::curvature},
    {"noise_scale", &Settings::noiseScale},
    {"main_spacing", &Settings::mainSpacing},
    {"side_spacing", &Settings::sideSpacing},
    {"centre_zone", &Settings::centreZone},
    {"inner_zone", &Settings::innerZone},
    {"ruin_share", &Settings::ruinShare},
    {"skeleton_share", &Settings::skeletonShare},
    {"park_share", &Settings::parkShare},
    {"house_share", &Settings::houseShare},
    {"industry_share", &Settings::industryShare},
    {"square", nullptr, nullptr, &Settings::square},
    {"street_names", nullptr, nullptr, &Settings::streetNames},
    {"landmark", nullptr, nullptr, &Settings::landmark},
    {"landmark_floors", &Settings::landmarkFloors},
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
        else if (k == "layout" && t.size() > 1) s.layout = t[1] == "organic" ? Layout::Organic : Layout::Grid;
        else if (k == "edge_road" && t.size() > 1) s.edgeRoad = streetClassFromName(t[1]);
        else if (k == "cache" && t.size() > 1) s.cache = resolve(dir, t[1]);
        else if (k == "square_name" && t.size() > 1) s.squareName = t[1];
        else if (k == "landmark_name" && t.size() > 1) s.landmarkName = t[1];
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
    if (s.cache.empty())
        s.cache = (dir / "generated").lexically_normal();
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
    out << "layout " << (s.layout == Layout::Organic ? "organic" : "grid") << '\n';
    out << "edge_road " << streetClassName(s.edgeRoad) << '\n';
    if (!s.cache.empty())
        out << "cache " << q(relPath(s.cache, dir)) << '\n';
    out << "square_name " << q(s.squareName) << '\n';
    out << "landmark_name " << q(s.landmarkName) << '\n';
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
constexpr float kVergeWidth = 0.45f; // kerb joint / wall foot strip where plants take hold (m)
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
        if (c == kColHouse) {
            groundRect(Ground::Built, r, s_.kerbHeight);
            wallFoot({{r.u0, r.v0}, {r.u1, r.v0}, {r.u1, r.v1}, {r.u0, r.v1}});
        }
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
        for (std::size_t i = 0; i < solidPolys_.size(); ++i)
            if (solidPolyBox_[i].contains(p, margin) && (pointInPolygon(solidPolys_[i], p) || distanceToPolygonEdge(solidPolys_[i], p) < margin))
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
    void addSolidPoly(const Poly& p)
    {
        Rect bb{1e9f, 1e9f, -1e9f, -1e9f};
        for (const auto& q : p) {
            bb.u0 = std::min(bb.u0, q.x);
            bb.v0 = std::min(bb.v0, q.y);
            bb.u1 = std::max(bb.u1, q.x);
            bb.v1 = std::max(bb.v1, q.y);
        }
        solidPolys_.push_back(p);
        solidPolyBox_.push_back(bb);
    }
    void polyLines(const Poly& p, const glm::vec4& c, float y = 0.4f)
    {
        for (std::size_t i = 0; i < p.size(); ++i)
            line(p[i], p[(i + 1) % p.size()], c, y);
    }
    // Ground shapes (local coordinates in, world out).
    std::vector<glm::vec2> worldXZ(const std::vector<glm::vec2>& pts) const
    {
        std::vector<glm::vec2> out;
        out.reserve(pts.size());
        for (const auto& q : pts) {
            const glm::vec3 w = world(q, 0.0f);
            out.push_back({w.x, w.z});
        }
        return out;
    }
    void ground(Ground g, const std::vector<glm::vec2>& pts, float height, float halfWidth = 0.0f, bool setHeight = true)
    {
        if (!plan.ground.enabled || pts.size() < (halfWidth > 0.0f ? 2u : 3u))
            return;
        GroundShape sh;
        sh.layer = static_cast<int>(g);
        sh.height = height + s_.origin.y;
        sh.setHeight = setHeight;
        sh.halfWidth = halfWidth;
        sh.points = worldXZ(pts);
        plan.ground.shapes.push_back(std::move(sh));
    }
    // The foot of a wall: painted after everything else (yards and lots would cover it).
    void wallFoot(const std::vector<glm::vec2>& footprint)
    {
        if (!plan.ground.enabled || footprint.size() < 3)
            return;
        std::vector<glm::vec2> ring = footprint;
        ring.push_back(footprint.front());
        GroundShape sh;
        sh.layer = static_cast<int>(Ground::Verge);
        sh.setHeight = false;
        sh.halfWidth = kVergeWidth * 1.6f;
        sh.points = worldXZ(ring);
        feet.push_back(std::move(sh));
    }
    void flushWallFeet()
    {
        for (auto& f : feet)
            plan.ground.shapes.push_back(std::move(f));
        feet.clear();
    }
    std::vector<GroundShape> feet;
    void groundRect(Ground g, const Rect& r, float height)
    {
        ground(g, {{r.u0, r.v0}, {r.u1, r.v0}, {r.u1, r.v1}, {r.u0, r.v1}}, height);
    }
    bool terrain() const { return plan.ground.enabled; }
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
    std::vector<Poly> solidPolys_;
    std::vector<Rect> solidPolyBox_;
    std::vector<Capsule> clear_;
    std::vector<glm::vec2> trees_;
};

// The terrain under the town: covers `extent` (local), the town itself is `town`.
void setupGround(Builder& b, const Settings& s, const Rect& extent, const Rect& town)
{
    GroundPlan& G = b.plan.ground;
    G.enabled = true;
    glm::vec2 lo{1e30f}, hi{-1e30f};
    for (const glm::vec2 c : {glm::vec2{extent.u0, extent.v0}, glm::vec2{extent.u1, extent.v0}, glm::vec2{extent.u1, extent.v1},
                              glm::vec2{extent.u0, extent.v1}}) {
        const glm::vec3 w = b.world(c, 0.0f);
        lo = glm::min(lo, glm::vec2(w.x, w.z));
        hi = glm::max(hi, glm::vec2(w.x, w.z));
    }
    G.size = std::max(hi.x - lo.x, hi.y - lo.y);
    G.origin = (lo + hi) * 0.5f - glm::vec2(G.size * 0.5f);
    G.spacing = std::max(s.groundSpacing, G.size / 4096.0f);
    G.town = b.worldXZ({{town.u0, town.v0}, {town.u1, town.v0}, {town.u1, town.v1}, {town.u0, town.v1}});
    G.hills = s.hills;
    G.hillFalloff = std::max(20.0f, s.outskirts * 0.6f);
    G.level = s.origin.y;
    G.kerb = s.kerbHeight;
    G.seed = s.seed;
    G.baseLayer = static_cast<int>(Ground::Meadow);
    const Kit& k = b.kit_;
    G.materials = {k.yard, k.lot.empty() ? k.yard : k.lot, k.road, k.pavement, k.yard, k.lot.empty() ? k.yard : k.lot,
                   k.lot.empty() ? k.pavement : k.lot, k.lot.empty() ? k.yard : k.lot};
    G.foliage = k.foliage;
    for (auto& f : G.foliage)
        f.density *= s.foliageDensity * (0.25f + 0.75f * s.overgrowth);
}

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
        if (s_.terrainGround) {
            const float o = std::max(s_.outskirts, 10.0f);
            setupGround(b_, s_, {all.u0 - o, all.v0 - o, all.u1 + o, all.v1 + o}, all);
            b_.groundRect(Ground::Road, all, 0.0f);
        } else if (!b_.kit_.road.empty())
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
        b_.flushWallFeet();
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
        const bool bare = bl.kind == BlockKind::Ruin || bl.kind == BlockKind::Garages || bl.kind == BlockKind::Wasteland ||
                          bl.kind == BlockKind::Industrial; // factory halls only in the organic layout: a waste lot here
        const fs::path& ground = bare && !b_.kit_.lot.empty() ? b_.kit_.lot : b_.kit_.yard;
        if (b_.terrain()) {
            b_.groundRect(Ground::Pavement, bl.pave, kh);
            b_.groundRect(bare ? Ground::Lot : Ground::Yard, bl.lot, kh);
        } else if (!b_.kit_.pavement.empty())
            b_.box("Pavement " + bl.label, groundGroup_, b_.kit_.pavement, bl.pave.centre(), kh - 0.15f, {bl.pave.w(), 0.3f, bl.pave.h()});
        if (!ground.empty() && !b_.terrain())
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
        b_.ground(Ground::Path, {c + E.n * 0.2f, c - E.n * (D + 3.0f)}, kh, std::min(1.6f, g * 0.35f));
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
        if (!ground.empty() && !b_.terrain())
            for (const Rect& r : ring)
                b_.box("Outskirts ground", groundGroup_, ground, r.centre(), -0.17f, {r.w(), 0.3f, r.h()});
        // The streets run on out of town as bare carriageways.
        Rng rng(mix(s_.seed, 5150));
        auto road = [&](glm::vec2 a, glm::vec2 b, float cw) {
            const Rect r{std::min(a.x, b.x) - (a.x == b.x ? cw * 0.5f : 0.0f), std::min(a.y, b.y) - (a.y == b.y ? cw * 0.5f : 0.0f),
                         std::max(a.x, b.x) + (a.x == b.x ? cw * 0.5f : 0.0f), std::max(a.y, b.y) + (a.y == b.y ? cw * 0.5f : 0.0f)};
            if (b_.terrain())
                b_.ground(Ground::Road, {a, b}, 0.0f, cw * 0.5f, false);
            else if (!b_.kit_.road.empty())
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
        if (b_.terrain() && !roles.empty() && std::string(roles.front()) == "meadow")
            return; // the terrain's foliage grows the grass
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
                b_.ground(Ground::Path, {e, to}, kh, 1.0f);
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
        case BlockKind::Industrial: // a grid block set to Industrial gets no halls (organic layout only): a waste lot
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
        case BlockKind::Houses: // organic layout only
        case BlockKind::Square:
            break;
        }
    }

    const Settings& s_;
    Builder b_;
    int C_ = 0, R_ = 0;
    std::vector<float> lx_, lz_;
    std::string groundGroup_, streetGroup_, kerbs_, lamps_, streetTrees_, wrecks_, streetPlants_;
};

// ============================================================================ organic layout
// Streets grown from a tensor field, blocks cut into lots and parcels, zoning by
// distance from the centre, houses by the shape grammar (docs/CITY_GENERATOR.md).
class OrganicLayout {
public:
    OrganicLayout(const Settings& s, const Kit& k) : s_(s), b_(s, k) {}

    Plan run()
    {
        const std::string root = s_.group;
        b_.group(root, "");
        streetGroup_ = root + " streets";
        b_.group(streetGroup_, root);
        kerbs_ = root + " kerbs";
        lamps_ = root + " lamps and cables";
        streetTrees_ = root + " street trees";
        wrecks_ = root + " wrecks";
        streetPlants_ = root + " street plants";
        for (const auto* g : {&kerbs_, &lamps_, &streetTrees_, &wrecks_, &streetPlants_})
            b_.group(*g, streetGroup_);

        NetworkSettings ns;
        ns.size = s_.townSize;
        ns.angle = s_.roadAngle;
        ns.districtTurn = s_.districtTurn;
        ns.districts = s_.districts;
        ns.radial = s_.radial;
        ns.curvature = s_.curvature;
        ns.noiseScale = s_.noiseScale;
        ns.majorSpacing = s_.mainSpacing;
        ns.minorSpacing = s_.sideSpacing;
        ns.majorClass = static_cast<int>(StreetClass::Main);
        ns.minorClass = static_cast<int>(StreetClass::Side);
        ns.boundaryClass = static_cast<int>(s_.edgeRoad);
        ns.seed = s_.seed;
        net_ = buildRoadNetwork(ns);
        half_ = s_.townSize * 0.5f;
        b_.plan.textures = b_.kit_.textures;
        if (b_.kit_.textures.empty())
            b_.plan.warnings.push_back("The kit names no texture folder (line textures) for the generated houses.");
        b_.plan.stats.roads = static_cast<int>(net_.roads.size());

        const float o = std::max(s_.outskirts, 10.0f);
        const Rect town{-half_, -half_, half_, half_};
        setupGround(b_, s_, {-half_ - o, -half_ - o, half_ + o, half_ + o}, town);
        const float kh = s_.kerbHeight;
        // Pavement under the whole town first (closes every gap), then the pavements
        // along the streets, the carriageways, and the lots on top.
        b_.groundRect(Ground::Pavement, town, kh);
        for (const auto& r : net_.roads)
            if (const StreetProfile& P = profile(cls(r.cls)); P.carriageway > 0.0f)
                b_.ground(Ground::Pavement, r.points, kh, P.width() * 0.5f);
        for (const auto& r : net_.roads) // the kerb joint: a strip left by the carriageway painted over it
            if (const StreetProfile& P = profile(cls(r.cls)); P.carriageway > 0.0f)
                b_.ground(Ground::Verge, r.points, kh, P.carriageway * 0.5f + kVergeWidth, false);
        for (const auto& r : net_.roads)
            if (const StreetProfile& P = profile(cls(r.cls)); P.carriageway > 0.0f) {
                b_.ground(Ground::Road, r.points, 0.0f, P.carriageway * 0.5f);
                for (std::size_t i = 1; i < r.points.size(); ++i)
                    b_.line(r.points[i - 1], r.points[i], kColStreet, 0.2f);
            }
        exitRoads();

        squareBlock_ = -1;
        if (s_.square) { // the roomy block nearest the centre
            float best = 1e9f;
            for (int i = 0; i < static_cast<int>(net_.blocks.size()); ++i) {
                const Poly& o = net_.blocks[static_cast<std::size_t>(i)].outline;
                const float A = signedArea(o), d = glm::length(centroid(o));
                glm::vec2 c, ax, hs;
                minAreaRect(o, c, ax, hs);
                if (A > 3000.0f && A < 30000.0f && std::min(hs.x, hs.y) > 25.0f && d < best) {
                    best = d;
                    squareBlock_ = i;
                }
            }
        }
        landmarkBlock_ = -1;
        if (s_.landmark) { // a roomy block north of the centre, on the line of sight from the south
            float best = 1e9f;
            const glm::vec2 want{0.0f, -half_ * 0.45f};
            for (int i = 0; i < static_cast<int>(net_.blocks.size()); ++i) {
                if (i == squareBlock_)
                    continue;
                const Poly& o = net_.blocks[static_cast<std::size_t>(i)].outline;
                glm::vec2 c, ax, hs;
                minAreaRect(o, c, ax, hs);
                const float d = glm::length(centroid(o) - want);
                if (std::min(hs.x, hs.y) > 22.0f && d < best) {
                    best = d;
                    landmarkBlock_ = i;
                }
            }
        }
        for (int i = 0; i < static_cast<int>(net_.blocks.size()); ++i)
            layBlock(i);
        for (const auto& r : net_.roads)
            dressRoad(r);
        if (s_.streetNames)
            nameStreets();
        for (auto& L : lots_)
            green(L);
        outskirts();

        auto& p = b_.plan;
        p.boundsMin = glm::vec3(1e9f);
        p.boundsMax = glm::vec3(-1e9f);
        for (const glm::vec2 corner : {glm::vec2{-half_ - o, -half_ - o}, glm::vec2{half_ + o, -half_ - o}, glm::vec2{-half_ - o, half_ + o},
                                       glm::vec2{half_ + o, half_ + o}}) {
            const glm::vec3 w = b_.world(corner, 0.0f);
            p.boundsMin = glm::min(p.boundsMin, w);
            p.boundsMax = glm::max(p.boundsMax, w + glm::vec3(0.0f, 1.0f, 0.0f));
        }
        p.stats.blocks = static_cast<int>(lots_.size());
        b_.flushWallFeet();
        return std::move(b_.plan);
    }

private:
    struct Lot {
        Poly poly;                    // facade line (lot outline), counter-clockwise
        std::vector<int> cls;         // street class on edge i
        Poly yard;                    // courtyard / free ground for greenery
        BlockKind kind = BlockKind::Park;
        bool ruined = false;
        std::string group, label;
        std::uint64_t seed = 0;
        float zone = 0.0f;            // distance from the centre / town radius
        std::vector<glm::vec2> entrances;
    };

    static StreetClass cls(int c) { return static_cast<StreetClass>(std::clamp(c, 0, kStreetClassCount - 1)); }
    const StreetProfile& profile(StreetClass c) const { return s_.profiles[static_cast<std::size_t>(c)]; }
    float width(int c) const { return profile(cls(c)).width(); }

    // ------------------------------------------------------------ roads out of town
    void exitRoads()
    {
        // Main roads that reach the town edge run on across the fields.
        for (const auto& r : net_.roads) {
            if (cls(r.cls) != StreetClass::Main || r.points.size() < 2)
                continue;
            for (int end = 0; end < 2; ++end) {
                const glm::vec2 p = end == 0 ? r.points.front() : r.points.back();
                const glm::vec2 q = end == 0 ? r.points[1] : r.points[r.points.size() - 2];
                if (std::max(std::abs(p.x), std::abs(p.y)) < half_ - 0.5f)
                    continue;
                glm::vec2 d = p - q;
                if (glm::length(d) < 1e-3f)
                    continue;
                d = glm::normalize(d);
                const glm::vec2 far = p + d * (std::max(s_.outskirts, 10.0f) + 20.0f);
                b_.ground(Ground::Road, {p, far}, 0.0f, profile(StreetClass::Main).carriageway * 0.5f, false);
                b_.addClear(p, far, profile(StreetClass::Main).carriageway * 0.5f - 0.3f);
            }
        }
    }

    // ------------------------------------------------------------ blocks
    void layBlock(int index)
    {
        const Block& B = net_.blocks[static_cast<std::size_t>(index)];
        // Simplify the street axes, keeping the points where the street class changes.
        std::vector<bool> keep(B.outline.size(), false);
        for (std::size_t i = 0; i < B.outline.size(); ++i)
            keep[i] = B.edgeClass[i] != B.edgeClass[(i + B.outline.size() - 1) % B.outline.size()];
        std::vector<int> kept;
        const Poly axes = simplifyPolygon(B.outline, 1.2f, &keep, &kept);
        std::vector<int> axCls;
        for (int k : kept)
            axCls.push_back(B.edgeClass[static_cast<std::size_t>(k)]);
        std::vector<float> dist;
        for (int c : axCls)
            dist.push_back(width(c) * 0.5f);
        Lot L;
        std::vector<int> src;
        if (!insetPolygon(axes, dist, L.poly, &src))
            return;
        for (int e : src)
            L.cls.push_back(axCls[static_cast<std::size_t>(e)]);
        mergeShortEdges(L.poly, L.cls, 6.0f);
        const float A = signedArea(L.poly);
        if (L.poly.size() < 3 || A < 250.0f)
            return;
        L.seed = mix(mix(s_.seed, 0xB10CBull), static_cast<std::uint64_t>(index));
        L.label = std::to_string(index + 1);
        L.group = s_.group + " block " + L.label;
        b_.group(L.group, s_.group);
        b_.polyLines(L.poly, kColLot);
        zoning(L, A);
        if (index == squareBlock_) {
            L.kind = BlockKind::Square;
            L.ruined = false;
        }
        const bool landmarkHere = index == landmarkBlock_ && !b_.kit_.stylesWithRole("skeleton_tower").empty();
        if (landmarkHere) {
            L.kind = BlockKind::Wasteland; // cleared round the tower, gone to birch and weeds
            L.ruined = false;
        }
        const bool bare = L.kind == BlockKind::Ruin || L.kind == BlockKind::Garages || L.kind == BlockKind::Wasteland || L.kind == BlockKind::Industrial;
        b_.ground(bare ? Ground::Lot : Ground::Yard, L.poly, s_.kerbHeight);
        L.yard = L.poly;
        Rng rng(L.seed);
        switch (L.kind) {
        case BlockKind::Perimeter: perimeter(L, rng); break;
        case BlockKind::Microdistrict: slabs(L, rng, L.zone < s_.innerZone ? "panel" : "khrushchyovka"); break;
        case BlockKind::Towers: towers(L, rng); break;
        case BlockKind::Garages: garages(L, rng); break;
        case BlockKind::Houses: houses(L, rng); break;
        case BlockKind::Square: square(L, rng); break;
        case BlockKind::Industrial: industrial(L, rng); break;
        default: break;
        }
        if (landmarkHere)
            landmarkTower(L, rng);
        lots_.push_back(std::move(L));
    }

    static void mergeShortEdges(Poly& p, std::vector<int>& c, float minLen)
    {
        for (int guard = 0; guard < 200 && p.size() > 3; ++guard) {
            std::size_t worst = p.size();
            float wl = minLen;
            for (std::size_t i = 0; i < p.size(); ++i) {
                const float l = glm::length(p[(i + 1) % p.size()] - p[i]);
                if (l < wl) {
                    wl = l;
                    worst = i;
                }
            }
            // Nearly straight joints go too (they would only make slivers of parcels).
            if (worst == p.size()) {
                for (std::size_t i = 0; i < p.size(); ++i) {
                    const glm::vec2 a = p[(i + p.size() - 1) % p.size()], b = p[i], d = p[(i + 1) % p.size()];
                    const glm::vec2 t0 = glm::normalize(b - a), t1 = glm::normalize(d - b);
                    if (glm::dot(t0, t1) > 0.9985f && c[(i + p.size() - 1) % p.size()] == c[i]) {
                        p.erase(p.begin() + static_cast<std::ptrdiff_t>(i));
                        c.erase(c.begin() + static_cast<std::ptrdiff_t>(i));
                        worst = p.size() + 1;
                        break;
                    }
                }
                if (worst == p.size())
                    return;
                continue;
            }
            // Drop the short edge: its end point goes, the neighbours meet there.
            const std::size_t j = (worst + 1) % p.size();
            const glm::vec2 m = (p[worst] + p[j]) * 0.5f;
            p[worst] = m;
            p.erase(p.begin() + static_cast<std::ptrdiff_t>(j));
            c.erase(c.begin() + static_cast<std::ptrdiff_t>(worst));
        }
    }

    // Which kind of block, by the distance from the centre (the town grew outwards).
    void zoning(Lot& L, float area)
    {
        glm::vec2 c, ax, hs;
        minAreaRect(L.poly, c, ax, hs);
        L.zone = glm::length(centroid(L.poly)) / std::max(1.0f, half_);
        Rng z(mix(L.seed, 0x2011EULL));
        const float narrow = std::min(hs.x, hs.y);
        if (narrow < 12.0f || area < 1400.0f) {
            L.kind = z.chance(0.5f) ? BlockKind::Park : BlockKind::Wasteland;
            return;
        }
        auto pick = [&](std::initializer_list<std::pair<BlockKind, float>> w) {
            float sum = 0.0f;
            for (const auto& [k, x] : w)
                sum += x;
            float r = z.uniform() * sum;
            for (const auto& [k, x] : w) {
                if (r < x)
                    return k;
                r -= x;
            }
            return w.begin()->first;
        };
        const bool roomy = narrow > 40.0f && area > 7000.0f;
        if (L.zone < s_.centreZone)
            L.kind = BlockKind::Perimeter;
        else if (L.zone < s_.innerZone)
            L.kind = pick({{BlockKind::Perimeter, 0.45f}, {BlockKind::Microdistrict, roomy ? 0.35f : 0.05f}, {BlockKind::Towers, roomy ? 0.2f : 0.0f}});
        else
            L.kind = pick({{BlockKind::Houses, s_.houseShare}, {BlockKind::Microdistrict, roomy ? 0.3f : 0.0f}, {BlockKind::Garages, 0.12f},
                           {BlockKind::Wasteland, 0.1f}, {BlockKind::Park, 0.06f}, {BlockKind::Perimeter, 0.08f},
                           {BlockKind::Industrial, b_.kit_.stylesWithRole("hall").empty() ? 0.0f : s_.industryShare}});
        if (z.chance(s_.parkShare))
            L.kind = BlockKind::Park;
        else if (L.kind != BlockKind::Park && L.kind != BlockKind::Wasteland && z.chance(s_.ruinShare))
            L.ruined = true;
        if (L.ruined && z.chance(0.25f))
            L.kind = BlockKind::Ruin; // came down altogether: heaps and birch wood
    }

    const FacadeStyle* style(const std::string& role, Rng& rng, const FacadeStyle* avoid = nullptr) const
    {
        auto v = b_.kit_.stylesWithRole(role);
        if (v.empty())
            return nullptr;
        if (v.size() > 1 && avoid)
            v.erase(std::remove(v.begin(), v.end(), avoid), v.end());
        float sum = 0.0f;
        for (const auto* st : v)
            sum += std::max(0.01f, st->weight);
        float r = rng.uniform() * sum;
        for (const auto* st : v) {
            r -= std::max(0.01f, st->weight);
            if (r <= 0.0f)
                return st;
        }
        return v.back();
    }

    // A house by the grammar on a footprint (local plan coordinates).
    void house(const Lot& L, const FacadeStyle& style0, const Poly& fp, const std::vector<EdgeRole>& roles, int floors, std::uint32_t seed, bool ruin,
               const char* what)
    {
        // Some big houses were never finished or burnt out to the bare frame: a concrete skeleton
        // on the same footprint. Decided per house (by its place), so the rest of the town stays.
        const FacadeStyle* sk = nullptr;
        const glm::vec2 c0 = centroid(fp);
        if (style0.role == "panel" || style0.role == "khrushchyovka" || (style0.role == "tenement" && !style0.pitched)) {
            Rng pick(mix(mix(s_.seed, 0x5CE1u), static_cast<std::uint64_t>(std::lround(c0.x * 10.0f) * 100003 + std::lround(c0.y * 10.0f))));
            if (pick.chance(s_.skeletonShare * (ruin ? 2.0f : 1.0f)))
                sk = style("skeleton", pick);
        }
        if (sk) {
            what = "Concrete skeleton";
            ruin = false; // the skeleton collapses by itself
        }
        const FacadeStyle& st = sk ? *sk : style0;
        const bool frame = isSkeleton(st);
        GeneratedBuilding g;
        const glm::vec2 c = centroid(fp);
        g.spec.style = st;
        for (const auto& p : fp)
            g.spec.footprint.push_back(p - c);
        g.spec.edges = roles;
        g.spec.floors = std::clamp(floors, 1, 40);
        g.spec.seed = seed;
        g.spec.ruin = ruin;
        const std::string key = buildingKey(g.spec);
        const fs::path dir = s_.cache.empty() ? fs::temp_directory_path() / "fps_city" : s_.cache;
        g.file = dir / ("house_" + key + ".glb");
        g.lodFile = dir / ("house_" + key + "_lod1.glb");
        Placement p;
        p.name = b_.label(std::string(ruin ? "Ruin " : "") + what);
        p.group = L.group;
        p.path = g.file;
        p.lodPath = g.lodFile;
        const float h = buildingHeight(g.spec);
        if (!frame && s_.overgrowth > 0.01f && (st.balconies > 0.0f || st.arcade > 0.0f))
            greenBalconies(L, c, balconySpots(g.spec), seed);
        p.lodDistance = std::clamp(40.0f + h * 2.0f, 70.0f, 160.0f);
        p.cull = std::clamp(400.0f + h * 18.0f, 600.0f, 1600.0f);
        p.position = b_.world(c, s_.kerbHeight);
        p.rotation = {0.0f, s_.rotation, 0.0f};
        b_.plan.placements.push_back(std::move(p));
        b_.plan.generated.push_back(std::move(g));
        ++b_.plan.stats.buildings;
        ++b_.plan.stats.generated;
        b_.addSolidPoly(fp);
        b_.polyLines(fp, kColHouse, 0.5f);
        // Grass and saplings grow up through a skeleton's ground slab.
        b_.ground(frame ? Ground::Lot : Ground::Built, fp, s_.kerbHeight);
        b_.wallFoot(fp);
        overgrow(L, fp, roles, h, st.pitched, ruin || frame, seed);
        if (frame)
            frameDebris(L, fp, seed);
    }

    // Balconies gone green: a bush or a young tree in the old flower boxes, vines hanging over
    // the railing (wall ivy turned upside down), long planters of weeds on an arcade's slab.
    void greenBalconies(const Lot& L, glm::vec2 c, const std::vector<BalconySpot>& spots, std::uint32_t seed)
    {
        const float g = s_.overgrowth, kh = s_.kerbHeight;
        Rng rng(mix(mix(s_.seed, 0xBA1C0u), static_cast<std::uint64_t>(seed) * 977u + static_cast<std::uint64_t>(std::lround(c.x * 3.0f + c.y * 5.0f))));
        for (const auto& sp : spots) {
            const glm::vec2 p = c + glm::vec2(sp.position.x, sp.position.z);
            const glm::vec2 n{sp.normal.x, sp.normal.z};
            if (sp.width > 6.0f) { // an arcade's slab: keep street trees and lamps out from under it; weeds on top
                const glm::vec2 t{-n.y, n.x};
                b_.addSolidPoly(rectPoly(p, t, n, sp.width, 2.6f));
                for (float x = -sp.width * 0.5f + 1.0f; x < sp.width * 0.5f - 1.0f; x += rng.range(2.0f, 5.0f))
                    if (rng.chance(0.5f * g)) {
                        const glm::vec2 t{-n.y, n.x};
                        if (const KitProp* k = b_.anyProp(rng.chance(0.15f) ? "sapling" : rng.chance(0.5f) ? "bush" : "weed", rng))
                            b_.prop(*k, "Arcade bush", L.group, p + t * x + n * rng.range(-0.7f, 0.7f), kh + sp.position.y, rng.range(0.0f, 360.0f),
                                    rng, {0.0f, 0.0f}, k->role == "weed" ? rng.range(0.9f, 1.3f) : rng.range(0.4f, 0.7f));
                    }
                continue;
            }
            const float r = rng.uniform();
            if (r < 0.3f * g) { // small: it grows in a flower box
                if (const KitProp* k = b_.anyProp(rng.chance(0.3f) ? "sapling" : rng.chance(0.5f) ? "bush" : "weed", rng))
                    b_.prop(*k, "Balcony bush", L.group, p, kh + sp.position.y, rng.range(0.0f, 360.0f), rng, {0.0f, 0.0f},
                            k->role == "weed" ? rng.range(0.8f, 1.2f) : rng.range(0.35f, 0.55f));
            } else if (r < 0.5f * g) {
                if (const KitProp* h = b_.anyProp("hanging", rng)) {
                    const float sc = std::min(rng.range(h->scaleMin, h->scaleMax), (sp.position.y - 1.0f) / std::max(h->size, 1.0f) * 0.6f);
                    if (sc > 0.3f)
                        b_.prop(*h, "Balcony vine", L.group, p + n * 0.55f, kh + sp.position.y + 0.9f, b_.yawZ(n), rng, {180.0f, 0.0f}, sc);
                }
            }
        }
    }

    // Round a concrete skeleton: heaps of what fell off the frame, saplings and bushes in them,
    // weeds in the open ground storey (the ground there is Lot, so the terrain grass grows too).
    void frameDebris(const Lot& L, const Poly& fp, std::uint32_t seed)
    {
        Rng rng(mix(mix(s_.seed, 0xDEB815u), static_cast<std::uint64_t>(seed) * 7919u + fp.size()));
        const float kh = s_.kerbHeight;
        const glm::vec2 c = centroid(fp);
        for (std::size_t e = 0; e < fp.size(); ++e) {
            const glm::vec2 a = fp[e], b = fp[(e + 1) % fp.size()];
            const float len = glm::length(b - a);
            if (len < 1.0f)
                continue;
            const glm::vec2 t = (b - a) / len;
            glm::vec2 n{t.y, -t.x};
            if (glm::dot(n, (a + b) * 0.5f - c) < 0.0f)
                n = -n;
            for (float sp = rng.range(2.0f, 6.0f); sp < len - 2.0f; sp += rng.range(7.0f, 14.0f)) {
                const glm::vec2 p = a + t * sp + n * rng.range(-1.0f, 3.0f);
                if (const KitProp* k = b_.anyProp(rng.chance(0.3f) ? "slab" : "rubble", rng))
                    b_.prop(*k, "Rubble heap", L.group, p, kh - 0.2f, rng.range(0.0f, 360.0f), rng,
                            {rng.range(-6.0f, 6.0f), rng.range(-6.0f, 6.0f)}, rng.range(1.0f, 2.2f));
                if (rng.chance(0.5f * s_.overgrowth))
                    if (const KitProp* k = b_.anyProp(rng.chance(0.5f) ? "sapling" : "bush", rng))
                        b_.prop(*k, "Young tree", L.group, p + t * rng.range(-2.0f, 2.0f) + n * 1.5f, kh, rng.range(0.0f, 360.0f), rng);
            }
        }
    }

    // Nature takes the houses back: ivy and wild vine up the fronts from the pavement and
    // the yard (mostly low patches, the tall ones only where the wall is high enough), and
    // self-seeded birches on flat roofs. Own random numbers: the rest of the town stays.
    void overgrow(const Lot& L, const Poly& fp, const std::vector<EdgeRole>& roles, float height, bool pitched, bool ruin,
                  std::uint32_t seed)
    {
        const float g = s_.overgrowth;
        if (g <= 0.0f || fp.size() < 3)
            return;
        Rng rng(mix(seed, 0x1A7B00ull));
        const auto ivy = b_.kit_.propsWithRole("ivy");
        const float sign = signedArea(fp) >= 0.0f ? 1.0f : -1.0f;
        const float kh = s_.kerbHeight;
        if (!ivy.empty())
            for (std::size_t i = 0; i < fp.size(); ++i) {
                if (i < roles.size() && roles[i] == EdgeRole::Party)
                    continue;
                const glm::vec2 a = fp[i], b = fp[(i + 1) % fp.size()];
                const float len = glm::length(b - a);
                if (len < 4.0f)
                    continue;
                const glm::vec2 t = (b - a) / len, n = glm::vec2(t.y, -t.x) * sign; // outward
                const int count = static_cast<int>(len / 14.0f * g * rng.range(0.3f, 1.4f) + rng.uniform() * g);
                for (int k = 0; k < count; ++k) {
                    // Low patches are the most common; a tall one needs the wall for it.
                    const float r = rng.uniform();
                    const float want = r < 0.45f ? 3.0f : r < 0.75f ? 6.5f : r < 0.92f ? 10.0f : 16.5f;
                    const KitProp* best = nullptr;
                    for (const KitProp* v : ivy)
                        if ((v->size <= 0.0f || v->size <= std::min(want, height * 0.9f)) && (!best || v->size > best->size))
                            best = v;
                    if (!best)
                        continue;
                    const glm::vec2 at = a + t * rng.range(1.5f, len - 1.5f) + n * 0.06f;
                    b_.prop(*best, "Ivy", L.group, at, kh, b_.yawZ(n), rng);
                    ++b_.plan.stats.plants;
                }
            }
        // Birches on flat roofs (in the roof gravel and the gutters), now and then.
        if (!pitched && !ruin && rng.chance(0.3f * g))
            for (int k = 0, n = 1 + rng.below(3); k < n; ++k) {
                glm::vec2 p;
                if (!randomIn(fp, rng, p) || distanceToPolygonEdge(fp, p) < 1.2f)
                    continue;
                if (const KitProp* t = b_.anyProp("roof_tree", rng)) {
                    b_.prop(*t, "Roof birch", L.group, p, kh + height - 0.05f, rng.range(0.0f, 360.0f), rng);
                    ++b_.plan.stats.plants;
                }
            }
    }

    // ------------------------------------------------------------ perimeter blocks of any shape
    void perimeter(Lot& L, Rng& rng)
    {
        const FacadeStyle* base = style("tenement", rng);
        if (!base) {
            b_.plan.warnings.push_back("Block " + L.label + ": the kit has no tenement styles.");
            L.kind = BlockKind::Park;
            return;
        }
        const std::size_t n = L.poly.size();
        float D = rng.range(base->depthMin, base->depthMax);
        Poly Q;
        bool ok = false;
        for (int tries = 0; tries < 3 && !ok; ++tries, D *= 0.8f)
            ok = D >= 8.0f && mitreInset(L.poly, std::vector<float>(n, D), Q);
        if (!ok) {
            // Too small for a courtyard: one house on the whole lot.
            Poly fp;
            if (signedArea(L.poly) < 3200.0f && insetPolygon(L.poly, std::vector<float>(n, 0.2f), fp)) {
                std::vector<EdgeRole> roles(fp.size(), EdgeRole::Street);
                house(L, *base, fp, roles, base->floorsMax, static_cast<std::uint32_t>(rng.next()), L.ruined && rng.chance(0.6f), "Tenement");
            } else {
                L.kind = BlockKind::Park;
            }
            return;
        }
        L.yard = Q;
        b_.polyLines(Q, kColLot, 0.45f);
        const int centreBonus = L.zone < s_.centreZone ? 1 : 0;
        int prevFloors = 0;
        const FacadeStyle* prevStyle = nullptr;
        int parcel = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t j = (i + 1) % n;
            const glm::vec2 P0 = L.poly[i], P1 = L.poly[j], Q0 = Q[i], Q1 = Q[j];
            const float len = glm::length(P1 - P0);
            const bool street = cls(L.cls[i]) != StreetClass::None;
            // Gateways into the yard, spread along long frontages.
            std::vector<std::pair<float, float>> gaps;
            if (len > 40.0f) {
                const int k = std::max(street ? 1 : 0, static_cast<int>(std::lround(len / s_.gatewayEvery)));
                for (int g = 0; g < k; ++g) {
                    const float w = rng.range(s_.gatewayMin, s_.gatewayMax);
                    const float at = len * static_cast<float>(g + 1) / static_cast<float>(k + 1) + rng.range(-4.0f, 4.0f);
                    gaps.push_back({std::clamp(at - w * 0.5f, 12.0f, len - 12.0f - w), w});
                }
            }
            // Runs between the gateways, filled with parcels of the style's widths.
            std::vector<std::pair<float, float>> runs;
            float s = 0.0f;
            for (const auto& [g0, w] : gaps) {
                if (g0 - s > 4.0f)
                    runs.push_back({s, g0});
                s = g0 + w;
                gateway(L, P0, P1, Q0, Q1, g0, w);
            }
            runs.push_back({s, len});
            for (const auto& [r0, r1] : runs) {
                const float run = r1 - r0;
                const float avg = (base->widthMin + base->widthMax) * 0.5f;
                const int count = std::max(1, static_cast<int>(std::lround(run / avg)));
                std::vector<float> w(static_cast<std::size_t>(count));
                float sum = 0.0f;
                for (float& x : w)
                    sum += x = rng.range(0.75f, 1.25f);
                float at = r0;
                for (int k = 0; k < count; ++k) {
                    const float sa = at, sb = k + 1 == count ? r1 : at + run * w[static_cast<std::size_t>(k)] / sum;
                    at = sb;
                    const float fa = sa / len, fb = sb / len;
                    const glm::vec2 a = P0 + (P1 - P0) * fa, b = P0 + (P1 - P0) * fb;
                    const glm::vec2 qa = Q0 + (Q1 - Q0) * fa, qb = Q0 + (Q1 - Q0) * fb;
                    const Poly fp{a, b, qb, qa};
                    if (signedArea(fp) < 30.0f || !isSimple(fp))
                        continue;
                    const FacadeStyle* st = style("tenement", rng, prevStyle);
                    prevStyle = st;
                    const bool corner = (k == 0 && r0 < 0.01f) || (k + 1 == count && r1 > len - 0.01f);
                    int f = prevFloors > 0 ? prevFloors + rng.below(2 * s_.maxFloorStep + 1) - s_.maxFloorStep
                                           : st->floorsMin + rng.below(st->floorsMax - st->floorsMin + 1);
                    f = std::clamp(f, st->floorsMin, st->floorsMax) + centreBonus;
                    if (corner && s_.tallCorners)
                        f = std::max(f, st->floorsMax + centreBonus);
                    prevFloors = f;
                    const std::vector<EdgeRole> roles{street ? EdgeRole::Street : EdgeRole::Yard, EdgeRole::Party, EdgeRole::Yard, EdgeRole::Party};
                    const bool ruin = L.ruined ? rng.chance(0.65f) : rng.chance(0.04f);
                    house(L, *st, fp, roles, f, static_cast<std::uint32_t>(mix(L.seed, static_cast<std::uint64_t>(parcel++)) & 0xFFFFFFu), ruin,
                          "Tenement");
                    ++b_.plan.stats.parcels;
                    b_.plan.stats.frontage += sb - sa;
                }
            }
            // Wall weeds and rubble at the foot of the street wall.
            if (street)
                wallFoot(L, P0, P1, rng);
        }
    }

    void gateway(Lot& L, glm::vec2 P0, glm::vec2 P1, glm::vec2 Q0, glm::vec2 Q1, float s, float w)
    {
        ++b_.plan.stats.gateways;
        const float len = glm::length(P1 - P0);
        const float f = (s + w * 0.5f) / len;
        const glm::vec2 out = P0 + (P1 - P0) * f, in = Q0 + (Q1 - Q0) * f;
        b_.line(P0 + (P1 - P0) * (s / len), P0 + (P1 - P0) * ((s + w) / len), kColGate, 0.6f);
        b_.addClear(out, in + (in - out) * 0.2f, std::min(1.4f, w * 0.3f));
        b_.ground(Ground::Path, {out, in + glm::normalize(in - out) * 4.0f}, s_.kerbHeight, std::min(1.6f, w * 0.35f));
        L.entrances.push_back(out);
    }

    void wallFoot(const Lot& L, glm::vec2 P0, glm::vec2 P1, Rng& rng)
    {
        const float len = glm::length(P1 - P0);
        if (len < 1.0f)
            return;
        const glm::vec2 t = (P1 - P0) / len, nOut{t.y, -t.x};
        const float kh = s_.kerbHeight;
        if (s_.overgrowth > 0.01f) {
            const float step = 2.6f / s_.overgrowth;
            for (float x = rng.range(0.0f, step); x < len; x += step * rng.range(0.5f, 1.5f))
                b_.plant({rng.chance(0.5f) ? "nettle" : "weed", "weed", "grass"}, "Wall weeds", L.group, P0 + t * x + nOut * rng.range(0.25f, 0.8f),
                         rng, kh, 0.05f);
        }
        const int nr = static_cast<int>(len / 100.0f * s_.rubblePer100m * (L.ruined ? 3.0f : 1.0f) * rng.range(0.6f, 1.4f) + 0.5f);
        for (int i = 0; i < nr; ++i)
            if (const KitProp* k = b_.anyProp("rubble", rng)) {
                const glm::vec2 p = P0 + t * rng.range(2.0f, std::max(2.5f, len - 2.0f)) + nOut * rng.range(0.6f, 2.2f);
                b_.prop(*k, "Rubble", L.group, p, kh - 0.25f, rng.range(0.0f, 360.0f), rng, {0.0f, 0.0f}, rng.range(0.45f, 0.9f));
            }
    }

    // ------------------------------------------------------------ rows of slabs and towers
    // Axis of a lot: along its longest street edge.
    glm::vec2 axisOf(const Lot& L) const
    {
        float best = -1.0f;
        glm::vec2 ax{1.0f, 0.0f};
        for (std::size_t i = 0; i < L.poly.size(); ++i) {
            const glm::vec2 e = L.poly[(i + 1) % L.poly.size()] - L.poly[i];
            const float l = glm::length(e) * (cls(L.cls[i]) == StreetClass::Main ? 1.3f : 1.0f);
            if (l > best) {
                best = l;
                ax = glm::normalize(e);
            }
        }
        return ax;
    }

    // Span of the polygon along `u` on the line through c + v * t (min, max of the crossings).
    static bool chord(const Poly& p, glm::vec2 u, glm::vec2 v, float t, float& a, float& b)
    {
        a = 1e30f;
        b = -1e30f;
        for (std::size_t i = 0; i < p.size(); ++i) {
            const glm::vec2 P = p[i], Q = p[(i + 1) % p.size()];
            const float pv = glm::dot(P, v) - t, qv = glm::dot(Q, v) - t;
            if ((pv > 0.0f) == (qv > 0.0f))
                continue;
            const glm::vec2 x = P + (Q - P) * (pv / (pv - qv));
            const float s = glm::dot(x, u);
            a = std::min(a, s);
            b = std::max(b, s);
        }
        return b > a;
    }

    struct Slot {
        glm::vec2 c;      // centre
        float length;     // along u
    };

    // Rows of rectangles depth D, spaced gap, as long as fits (lengths from `lengthFor`).
    std::vector<Slot> rows(const Poly& region, glm::vec2 u, glm::vec2 v, float D, float rowGap, float gapAlong, float minLen, float maxLen,
                           float step)
    {
        std::vector<Slot> out;
        float v0 = 1e30f, v1 = -1e30f;
        for (const auto& p : region) {
            v0 = std::min(v0, glm::dot(p, v));
            v1 = std::max(v1, glm::dot(p, v));
        }
        const float total = v1 - v0;
        const int nRows = std::max(0, static_cast<int>((total + rowGap) / (D + rowGap)));
        if (nRows == 0)
            return out;
        const float used = static_cast<float>(nRows) * D + static_cast<float>(nRows - 1) * rowGap;
        for (int r = 0; r < nRows; ++r) {
            const float vc = v0 + (total - used) * 0.5f + static_cast<float>(r) * (D + rowGap) + D * 0.5f;
            float a0, a1, b0, b1;
            if (!chord(region, u, v, vc - D * 0.5f, a0, a1) || !chord(region, u, v, vc + D * 0.5f, b0, b1))
                continue;
            const float lo = std::max(a0, b0), hi = std::min(a1, b1);
            float avail = hi - lo;
            // Lengths: as long as allowed, the rest split.
            std::vector<float> lens;
            while (avail >= minLen) {
                float l = std::min(maxLen, avail);
                l = std::floor(l / step) * step;
                if (l < minLen)
                    break;
                lens.push_back(l);
                avail -= l + gapAlong;
            }
            float usedU = -gapAlong;
            for (float l : lens)
                usedU += l + gapAlong;
            float at = lo + (hi - lo - usedU) * 0.5f;
            for (float l : lens) {
                const glm::vec2 c = u * (at + l * 0.5f) + v * vc;
                // all four corners inside (the region may not be convex)
                const glm::vec2 hu = u * (l * 0.5f), hv = v * (D * 0.5f);
                if (pointInPolygon(region, c - hu - hv) && pointInPolygon(region, c + hu - hv) && pointInPolygon(region, c + hu + hv) &&
                    pointInPolygon(region, c - hu + hv))
                    out.push_back({c, l});
                at += l + gapAlong;
            }
        }
        return out;
    }

    static Poly rectPoly(glm::vec2 c, glm::vec2 u, glm::vec2 v, float L, float D)
    {
        const glm::vec2 hu = u * (L * 0.5f), hv = v * (D * 0.5f);
        return {c - hu - hv, c + hu - hv, c + hu + hv, c - hu + hv};
    }

    void slabs(Lot& L, Rng& rng, const char* role)
    {
        Poly region;
        if (!insetPolygon(L.poly, std::vector<float>(L.poly.size(), s_.setback), region)) {
            L.kind = BlockKind::Park;
            return;
        }
        const FacadeStyle* st = style(role, rng);
        if (!st)
            st = style("panel", rng);
        if (!st) {
            b_.plan.warnings.push_back(std::string("Block ") + L.label + ": the kit has no " + role + " styles.");
            L.kind = BlockKind::Park;
            return;
        }
        const glm::vec2 u = axisOf(L), v{-u.y, u.x};
        const int floors = st->floorsMin + rng.below(st->floorsMax - st->floorsMin + 1);
        BuildingSpec probe;
        probe.style = *st;
        probe.floors = floors;
        const float H = buildingHeight(probe);
        const float D = std::round(rng.range(st->depthMin, st->depthMax));
        const float gap = std::max(s_.rowGapMin, s_.rowGapFactor * H);
        const auto slots = rows(region, u, v, D, gap, s_.slabGap, std::max(st->bay * 6.0f, 18.0f), st->widthMax, st->bay);
        for (const auto& sl : slots) {
            const Poly fp = rectPoly(sl.c, u, v, sl.length, D);
            const std::vector<EdgeRole> roles{EdgeRole::Street, EdgeRole::Yard, EdgeRole::Yard, EdgeRole::Yard};
            // Few variants per series: identical slabs repeat (as they did) and share one file.
            house(L, *st, fp, roles, floors, static_cast<std::uint32_t>(1 + rng.below(3)), L.ruined && rng.chance(0.5f),
                  st->role == "khrushchyovka" ? "Walk-up" : "Housing project");
            L.entrances.push_back(sl.c - v * (D * 0.5f));
            ++b_.plan.stats.parcels;
        }
        if (slots.empty())
            L.kind = BlockKind::Park; // nothing fitted: a green square
        L.yard = region;
    }

    void towers(Lot& L, Rng& rng)
    {
        const auto kit = b_.kit_.buildingsWithRole("tower");
        if (kit.empty()) {
            slabs(L, rng, "panel");
            return;
        }
        Poly region;
        if (!insetPolygon(L.poly, std::vector<float>(L.poly.size(), s_.setback), region)) {
            L.kind = BlockKind::Park;
            return;
        }
        const glm::vec2 u = axisOf(L), v{-u.y, u.x};
        const KitBuilding& t = *kit[static_cast<std::size_t>(rng.below(static_cast<int>(kit.size())))];
        const float gap = std::max(s_.rowGapMin, 0.5f * t.height);
        const auto slots = rows(region, u, v, t.depth, gap, gap, t.length, t.length, 0.01f);
        int i = 0;
        for (const auto& sl : slots) {
            const KitBuilding& tw = *kit[static_cast<std::size_t>((i++ + rng.below(2)) % static_cast<int>(kit.size()))];
            const glm::vec2 face = -v;
            // A tower left as a tall bare frame: the ruins seen over the roofs from afar.
            if (const FacadeStyle* sk = rng.chance(s_.skeletonShare * 1.5f) ? style("skeleton_tower", rng) : nullptr) {
                const Poly fp = rectPoly(sl.c, u, v, tw.length, tw.depth);
                const std::vector<EdgeRole> roles{EdgeRole::Street, EdgeRole::Yard, EdgeRole::Yard, EdgeRole::Yard};
                house(L, *sk, fp, roles, sk->floorsMin + rng.below(sk->floorsMax - sk->floorsMin + 1), static_cast<std::uint32_t>(rng.below(1000)), false,
                      "Skeleton tower");
                L.entrances.push_back(sl.c + face * (tw.depth * 0.5f));
                continue;
            }
            b_.building(tw, L.group, sl.c - face * (tw.front - tw.depth * 0.5f), b_.yawZ(face));
            const Poly fp = rectPoly(sl.c, u, v, tw.length, tw.depth);
            b_.addSolidPoly(fp);
            b_.polyLines(fp, kColHouse, 0.5f);
            b_.ground(Ground::Built, fp, s_.kerbHeight);
            b_.wallFoot(fp);
            L.entrances.push_back(sl.c + face * (tw.depth * 0.5f));
        }
        L.yard = region;
    }

    // The landmark: one very tall bare frame in the middle of its block, half of it come down
    // onto the waste ground round it; on the map by name.
    void landmarkTower(Lot& L, Rng& rng)
    {
        const FacadeStyle* st = style("skeleton_tower", rng);
        if (!st)
            return;
        glm::vec2 c, ax, hs;
        minAreaRect(L.poly, c, ax, hs);
        const glm::vec2 v{-ax.y, ax.x};
        const float w = std::clamp(st->widthMax, 18.0f, std::min(hs.x, hs.y) * 2.0f - 12.0f), d = std::clamp(st->depthMax, 14.0f, w);
        const Poly fp = rectPoly(c, ax, v, w, d);
        const std::vector<EdgeRole> roles{EdgeRole::Street, EdgeRole::Yard, EdgeRole::Yard, EdgeRole::Yard};
        house(L, *st, fp, roles, static_cast<int>(s_.landmarkFloors), 7u + s_.seed % 97u, false, "Landmark tower");
        b_.plan.markers.push_back({s_.landmarkName.empty() ? std::string("Tower") : s_.landmarkName, L.group, b_.world(c, s_.kerbHeight)});
    }

    // Factory halls in rows across the block, the yard between them gone to weeds and birch.
    void industrial(Lot& L, Rng& rng)
    {
        const FacadeStyle* st = style("hall", rng);
        Poly region;
        if (!st || !insetPolygon(L.poly, std::vector<float>(L.poly.size(), std::max(6.0f, s_.setback)), region)) {
            L.kind = BlockKind::Wasteland;
            return;
        }
        const glm::vec2 u = axisOf(L), v{-u.y, u.x};
        const float D = std::round(rng.range(st->depthMin, st->depthMax));
        const auto slots = rows(region, u, v, D, 14.0f, 10.0f, std::max(st->widthMin, 24.0f), st->widthMax, std::max(st->bay, 3.0f));
        int n = 0;
        for (const auto& sl : slots) {
            if (n >= 3)
                break;
            const Poly fp = rectPoly(sl.c, u, v, sl.length, D);
            const std::vector<EdgeRole> roles{EdgeRole::Street, EdgeRole::Yard, EdgeRole::Yard, EdgeRole::Yard};
            const std::uint32_t seed = static_cast<std::uint32_t>(rng.below(1000));
            house(L, *st, fp, roles, 1, seed, false, "Factory hall");
            hallInside(L, *st, sl.c, u, v, sl.length, D, seed);
            L.entrances.push_back(sl.c - v * (D * 0.5f));
            ++n;
        }
        if (!n)
            L.kind = BlockKind::Wasteland;
        L.yard = region;
    }

    // Nature inside a hall: ferns and weeds on the floor, saplings where the roof fell in, vines
    // hanging from the trusses (wall ivy turned upside down under the bottom chord).
    void hallInside(const Lot& L, const FacadeStyle& st, glm::vec2 c, glm::vec2 u, glm::vec2 v, float length, float depth, std::uint32_t seed)
    {
        Rng rng(mix(mix(s_.seed, 0x4A11u), static_cast<std::uint64_t>(seed) * 131u + static_cast<std::uint64_t>(std::lround(c.x * 7.0f + c.y * 13.0f))));
        const float g = s_.overgrowth, kh = s_.kerbHeight;
        const float He = std::max(5.0f, st.groundStorey);
        const float A = length * depth;
        auto inside = [&](float margin) {
            return c + u * rng.range(-length * 0.5f + margin, length * 0.5f - margin) + v * rng.range(-depth * 0.5f + margin, depth * 0.5f - margin);
        };
        for (int i = 0; i < static_cast<int>(A / 16.0f * g); ++i)
            if (const KitProp* k = b_.anyProp(rng.chance(0.6f) ? "fern" : "weed", rng))
                b_.prop(*k, "Fern", L.group, inside(1.0f), kh + 0.03f, rng.range(0.0f, 360.0f), rng);
        for (int i = 0; i < static_cast<int>(A / 220.0f * g); ++i)
            b_.plant({"sapling", "bush"}, "Young tree", L.group, inside(2.0f), rng, kh + 0.03f, 1.0f);
        const float bay = std::clamp(st.bay, 4.0f, 12.0f);
        const int frames = std::max(2, static_cast<int>(std::round(length / bay)));
        for (int k = 1; k < frames; ++k) {
            const float uu = -length * 0.5f + length * static_cast<float>(k) / static_cast<float>(frames);
            for (int j = 0; j < 3; ++j) {
                if (!rng.chance(0.8f * g))
                    continue;
                const KitProp* h = b_.anyProp("hanging", rng);
                if (!h)
                    break;
                const glm::vec2 p = c + u * uu + v * rng.range(-depth * 0.4f, depth * 0.4f);
                const float top = He - 0.3f;
                const float sc = std::min(rng.range(h->scaleMin, h->scaleMax), (top - 1.0f) / std::max(h->size, 1.0f));
                b_.prop(*h, "Hanging vine", L.group, p, kh + top, b_.yawZ(u) + rng.range(-15.0f, 15.0f), rng, {180.0f, 0.0f}, sc);
            }
        }
    }

    void garages(Lot& L, Rng& rng)
    {
        const auto kit = b_.kit_.buildingsWithRole("garage");
        if (kit.empty()) {
            L.kind = BlockKind::Wasteland;
            return;
        }
        Poly region;
        if (!insetPolygon(L.poly, std::vector<float>(L.poly.size(), 5.0f), region))
            return;
        const KitBuilding& g = *kit.front();
        const glm::vec2 u = axisOf(L), v{-u.y, u.x};
        const auto slots = rows(region, u, v, g.depth, 8.0f, 6.0f, g.length * 3.0f, g.length * 14.0f, g.length);
        int r = 0;
        for (const auto& sl : slots) {
            const glm::vec2 face = (r++ % 2) ? v : -v;
            const int count = static_cast<int>(std::lround(sl.length / g.length));
            for (int i = 0; i < count; ++i) {
                const glm::vec2 c = sl.c + u * ((static_cast<float>(i) - (static_cast<float>(count) - 1.0f) * 0.5f) * g.length);
                b_.building(g, L.group, c - face * (g.front - g.depth * 0.5f), b_.yawZ(face));
            }
            const Poly fp = rectPoly(sl.c, u, v, sl.length, g.depth);
            b_.addSolidPoly(fp);
            b_.polyLines(fp, kColHouse, 0.5f);
            b_.ground(Ground::Built, fp, s_.kerbHeight);
            b_.wallFoot(fp);
        }
        (void)rng;
    }

    // ------------------------------------------------------------ private houses: recursive subdivision
    void subdivide(const Poly& p, float target, float minWidth, Rng& rng, std::vector<Poly>& out, int depth = 0)
    {
        const float A = signedArea(p);
        glm::vec2 c, ax, hs;
        minAreaRect(p, c, ax, hs);
        if (depth > 12 || A < target * 1.5f || hs.x * 2.0f < minWidth * 2.0f) {
            out.push_back(p);
            return;
        }
        // Cut across the long side, near the middle.
        const glm::vec2 at = c + ax * (hs.x * rng.range(-0.15f, 0.15f));
        Poly a, b;
        splitPolygon(p, at, glm::vec2(-ax.y, ax.x), a, b);
        if (a.empty() || b.empty()) {
            out.push_back(p);
            return;
        }
        subdivide(a, target, minWidth, rng, out, depth + 1);
        subdivide(b, target, minWidth, rng, out, depth + 1);
    }

    void houses(Lot& L, Rng& rng)
    {
        const FacadeStyle* st = style("house", rng);
        if (!st) {
            L.kind = BlockKind::Wasteland;
            return;
        }
        std::vector<Poly> parcels;
        subdivide(L.poly, rng.range(450.0f, 800.0f), 16.0f, rng, parcels);
        for (const Poly& P : parcels) {
            b_.polyLines(P, kColLot, 0.45f);
            // The street side: the longest edge on the lot's outline facing a street.
            float best = 0.0f;
            int front = -1;
            for (std::size_t i = 0; i < P.size(); ++i) {
                const glm::vec2 a = P[i], b = P[(i + 1) % P.size()];
                const float l = glm::length(b - a);
                const glm::vec2 m = (a + b) * 0.5f;
                if (l > best && l > 9.0f && distanceToPolygonEdge(L.poly, m) < 0.3f) {
                    best = l;
                    front = static_cast<int>(i);
                }
            }
            if (front < 0)
                continue; // a garden without a way to the street stays a garden
            const glm::vec2 a = P[static_cast<std::size_t>(front)], b = P[(static_cast<std::size_t>(front) + 1) % P.size()];
            const glm::vec2 t = glm::normalize(b - a), nIn{-t.y, t.x};
            const float w = std::min(rng.range(8.0f, 11.0f), best - 3.0f), d = rng.range(7.0f, 9.5f), back = rng.range(3.0f, 6.0f);
            const glm::vec2 c = (a + b) * 0.5f + t * rng.range(-0.15f, 0.15f) * (best - w) + nIn * (back + d * 0.5f);
            const Poly fp = rectPoly(c, t, nIn, w, d);
            bool inside = true;
            for (const auto& q : fp)
                inside = inside && pointInPolygon(P, q) && distanceToPolygonEdge(P, q) > 1.0f;
            if (!inside)
                continue;
            const int floors = st->floorsMin + rng.below(st->floorsMax - st->floorsMin + 1);
            house(L, *st, fp, {EdgeRole::Street, EdgeRole::Yard, EdgeRole::Yard, EdgeRole::Yard}, floors,
                  static_cast<std::uint32_t>(1 + rng.below(6)), L.ruined ? rng.chance(0.5f) : rng.chance(0.12f), "House");
            ++b_.plan.stats.parcels;
            // An old fruit tree or two behind the house.
            for (int k = 0; k < 1 + rng.below(2); ++k) {
                const glm::vec2 q = c + nIn * (d * 0.5f + rng.range(3.0f, 9.0f)) + t * rng.range(-6.0f, 6.0f);
                if (pointInPolygon(P, q) && b_.free(q, 1.5f))
                    if (const KitProp* tr = b_.anyProp("tree", rng))
                        b_.tree(*tr, L.group, q, rng, s_.kerbHeight);
            }
        }
    }

    // ------------------------------------------------------------ the streets
    // Decades without repair: the carriageway breaks up. Painted into the terrain, no extra draws:
    //   cracks  -> zig-zag verge strokes (grass grows in them): along the wheel tracks, across
    //              every few metres, crazed patches; most where the road meets the kerb
    //   potholes-> dirt hollows sunk into the height map (rain collects in them as puddles)
    //   pavement-> slab joints with grass, sunken and lifted slabs (dirt, a few cm down)
    template <class At>
    void crackRoad(const Road& r, const StreetProfile& P, float L, const At& at)
    {
        const float d = s_.roadDecay;
        if (d <= 0.01f || !b_.plan.ground.enabled)
            return;
        Rng rng(mix(mix(s_.seed, 0xC4AC5u), static_cast<std::uint64_t>(std::lround(r.points.front().x * 10.0f) * 92821 + std::lround(r.points.back().y * 10.0f))));
        const float cw = P.carriageway * 0.5f, kh = s_.kerbHeight;
        auto point = [&](float s, float off) {
            glm::vec2 p, t;
            at(std::clamp(s, 0.0f, L), p, t);
            return p + glm::vec2(-t.y, t.x) * std::clamp(off, -cw + 0.1f, cw - 0.1f);
        };
        auto crack = [&](float s0, float o0, float s1, float o1, float w) { // a jagged line from (s0, o0) to (s1, o1)
            std::vector<glm::vec2> pts;
            const int n = std::max(2, static_cast<int>(std::hypot(s1 - s0, o1 - o0) / 0.9f));
            for (int k = 0; k <= n; ++k) {
                const float f = static_cast<float>(k) / static_cast<float>(n);
                const float j = (k == 0 || k == n) ? 0.0f : rng.range(-0.35f, 0.35f);
                pts.push_back(point(s0 + (s1 - s0) * f + j * 0.5f, o0 + (o1 - o0) * f + j));
            }
            b_.ground(Ground::Verge, pts, 0.0f, w, false);
        };
        auto decal = [&](const char* role, float s, float off, float yawOff) {
            const KitProp* k = b_.anyProp(role, rng);
            if (!k)
                return false;
            glm::vec2 p, t;
            at(std::clamp(s, 0.0f, L), p, t);
            const glm::vec2 q = p + glm::vec2(-t.y, t.x) * std::clamp(off, -cw + 0.6f, cw - 0.6f);
            b_.prop(*k, "Road crack", streetPlants_, q, 0.0f, b_.yawX(t) + yawOff + rng.range(-12.0f, 12.0f), rng);
            return true;
        };
        // Long cracks in the wheel tracks and down the middle (decals); along the kerb the joint
        // opened up and grass took it (verge strokes).
        for (const float off : {-cw * 0.55f, cw * 0.55f, 0.0f})
            for (float s = rng.range(0.0f, 8.0f); s < L - 2.0f; s += rng.range(4.0f, 12.0f) * (1.5f - d))
                if (rng.chance(0.4f + 0.5f * d))
                    decal("road_crack", s, off + rng.range(-0.5f, 0.5f), rng.chance(0.5f) ? 0.0f : 180.0f);
        for (const float off : {-cw + 0.45f, cw - 0.45f})
            for (float s = rng.range(0.0f, 8.0f); s < L - 2.0f;) {
                const float len = rng.range(4.0f, 16.0f) * (0.5f + d);
                if (rng.chance(0.3f + 0.5f * d))
                    crack(s, off + rng.range(-0.2f, 0.2f), std::min(L, s + len), off + rng.range(-0.3f, 0.3f), 0.12f);
                s += len + rng.range(4.0f, 16.0f) * (1.4f - d);
            }
        // Transverse frost cracks every few metres.
        for (float s = rng.range(2.0f, 6.0f); s < L - 1.0f; s += rng.range(5.0f, 14.0f) * (1.4f - d))
            decal("road_crack_cross", s, rng.range(-cw * 0.4f, cw * 0.4f), 90.0f);
        // Crazed patches and old tar repairs.
        for (float s = rng.range(5.0f, 25.0f); s < L - 3.0f; s += rng.range(12.0f, 35.0f) * (1.3f - d))
            decal("road_net", s, rng.range(-cw * 0.6f, cw * 0.6f), rng.range(0.0f, 360.0f));
        // Potholes: a broken rim (decal) round a hollow sunk into the height map (rain collects in
        // it as a puddle), grass at the edge.
        const int holes = static_cast<int>(L / 100.0f * (1.5f + 7.0f * d) * rng.range(0.5f, 1.5f));
        for (int k = 0; k < holes; ++k) {
            const float s = rng.range(2.0f, L - 2.0f), o = rng.range(-cw * 0.7f, cw * 0.7f);
            const KitProp* rim = b_.anyProp("pothole", rng);
            const glm::vec2 c = point(s, o);
            const float sc = rim ? rng.range(rim->scaleMin, rim->scaleMax) : 1.0f;
            if (rim)
                b_.prop(*rim, "Pothole", streetPlants_, c, 0.0f, rng.range(0.0f, 360.0f), rng, {0.0f, 0.0f}, sc);
            const float rx = 0.62f * sc;
            Poly hole;
            for (int i = 0; i < 10; ++i) {
                const float t = 6.2831853f * static_cast<float>(i) / 10.0f;
                hole.push_back(c + glm::vec2(std::cos(t), std::sin(t)) * rx * rng.range(0.85f, 1.1f));
            }
            b_.ground(Ground::Lot, hole, -rng.range(0.05f, 0.1f));
            if (rng.chance(0.5f * s_.overgrowth))
                b_.plant({"grass", "weed"}, "Pothole grass", streetPlants_, c + glm::vec2(rx * 0.9f, 0.0f), rng, 0.0f, 0.1f);
        }
        // Pavements: slab joints with grass, sunken and heaved slabs.
        if (P.pavement >= 1.0f) {
            for (const float side : {1.0f, -1.0f}) {
                const float o0 = side * (cw + 0.3f), o1 = side * (cw + P.pavement - 0.2f);
                auto pave = [&](float s, float off) {
                    glm::vec2 p, t;
                    at(std::clamp(s, 0.0f, L), p, t);
                    return p + glm::vec2(-t.y, t.x) * off;
                };
                for (float s = rng.range(0.0f, 2.0f); s < L; s += rng.range(1.5f, 3.0f))
                    if (rng.chance(0.25f + 0.5f * d))
                        b_.ground(Ground::Verge, {pave(s, o0), pave(s + rng.range(-0.1f, 0.1f), o1)}, kh, 0.08f, false);
                for (float s = rng.range(3.0f, 12.0f); s < L - 1.0f; s += rng.range(6.0f, 20.0f) * (1.3f - d)) {
                    const float a0 = rng.range(0.0f, 1.0f) * (P.pavement - 1.6f), w = rng.range(0.8f, 1.6f), l = rng.range(0.9f, 2.0f);
                    const float in0 = side * (cw + 0.3f + a0), in1 = side * (cw + 0.3f + std::min(a0 + w, P.pavement - 0.3f));
                    const Poly slab{pave(s, in0), pave(s + l, in0), pave(s + l, in1), pave(s, in1)};
                    const bool sunk = rng.chance(0.7f);
                    b_.ground(sunk ? Ground::Lot : Ground::Pavement, slab, kh + (sunk ? -rng.range(0.04f, 0.1f) : rng.range(0.03f, 0.07f)));
                    Poly ring = slab;
                    ring.push_back(slab.front());
                    b_.ground(Ground::Verge, ring, kh, 0.1f, false);
                }
            }
        }
    }

    void dressRoad(const Road& r)
    {
        const StreetProfile& P = profile(cls(r.cls));
        if (P.carriageway <= 0.0f || r.points.size() < 2)
            return;
        std::vector<float> acc{0.0f};
        for (std::size_t i = 1; i < r.points.size(); ++i)
            acc.push_back(acc.back() + glm::length(r.points[i] - r.points[i - 1]));
        const float L = acc.back();
        auto at = [&](float s, glm::vec2& p, glm::vec2& t) {
            const std::size_t i = static_cast<std::size_t>(std::upper_bound(acc.begin(), acc.end(), s) - acc.begin());
            const std::size_t k = std::clamp<std::size_t>(i, 1, r.points.size() - 1);
            const float seg = acc[k] - acc[k - 1];
            const float f = seg > 0.0f ? (s - acc[k - 1]) / seg : 0.0f;
            p = r.points[k - 1] + (r.points[k] - r.points[k - 1]) * std::clamp(f, 0.0f, 1.0f);
            t = seg > 0.0f ? (r.points[k] - r.points[k - 1]) / seg : glm::vec2(1, 0);
        };
        Rng rng(mix(s_.seed, static_cast<std::uint64_t>(std::lround(r.points.front().x * 10.0f) * 73856093 ^ std::lround(r.points.front().y * 10.0f) * 19349663)));
        const float kh = s_.kerbHeight;
        const float jc = 11.0f; // keep junctions clear
        if (L < 2.0f * jc)
            return;
        const float cw = P.carriageway * 0.5f;
        crackRoad(r, P, L, at);
        // Kerbs in whole modules on both sides.
        if (const auto kerbs = b_.kit_.propsWithRole("kerb"); !kerbs.empty() && P.pavement > 0.0f) {
            const float m = kerbs.front()->size > 0.0f ? kerbs.front()->size : 6.0f;
            for (const float side : {1.0f, -1.0f})
                for (float s = jc * 0.7f + m * 0.5f; s < L - jc * 0.7f - m * 0.5f; s += m) {
                    glm::vec2 p, t;
                    at(s, p, t);
                    const glm::vec2 n{-t.y, t.x};
                    const glm::vec2 k = p + n * side * cw;
                    b_.prop(*kerbs[static_cast<std::size_t>(rng.below(static_cast<int>(kerbs.size())))], "Kerb", kerbs_, k, 0.0f, b_.yawZ(-n * side), rng,
                            {0.0f, 0.0f}, 1.0f);
                }
        }
        // Lamps staggered across the street, cables lamp to lamp.
        std::vector<float> lampS;
        if (P.lamps && P.pavement >= 1.0f)
            for (const float side : {1.0f, -1.0f}) {
                std::vector<glm::vec3> tops;
                for (float s = jc + (side > 0.0f ? 0.0f : s_.lampSpacing * 0.5f); s < L - jc; s += s_.lampSpacing) {
                    glm::vec2 p, t;
                    at(s, p, t);
                    const glm::vec2 n{-t.y, t.x};
                    const glm::vec2 q = p + n * side * (cw + 0.6f);
                    if (!b_.free(q, 0.6f))
                        continue;
                    const KitProp* lamp = rng.chance(0.25f) ? b_.anyProp("lamp_leaning", rng) : nullptr;
                    if (!lamp)
                        lamp = b_.anyProp("lamp", rng);
                    if (!lamp)
                        break;
                    b_.prop(*lamp, "Lamp post", lamps_, q, kh - 0.02f, b_.yawX(-n * side) + rng.range(-5.0f, 5.0f), rng);
                    b_.addSolid({q.x - 0.3f, q.y - 0.3f, q.x + 0.3f, q.y + 0.3f});
                    ++b_.plan.stats.lamps;
                    lampS.push_back(s);
                    tops.push_back(b_.world(q, kh + 8.1f));
                }
                for (std::size_t i = 1; i < tops.size(); ++i)
                    if (rng.chance(s_.cableChance))
                        b_.cable(tops[i - 1], tops[i], lamps_);
            }
        // Planted rows: one species per street, some lost, seedlings in the gaps.
        if (P.trees && P.pavement >= 2.5f) {
            const KitProp* sp = b_.anyProp("tree", rng);
            for (const float side : {1.0f, -1.0f})
                for (float s = jc + 3.0f; sp && s < L - jc - 3.0f; s += s_.treeSpacing) {
                    if (std::any_of(lampS.begin(), lampS.end(), [&](float l) { return std::abs(l - s) < 3.0f; }))
                        continue;
                    glm::vec2 p, t;
                    at(s, p, t);
                    const glm::vec2 n{-t.y, t.x};
                    const glm::vec2 q = p + n * side * (cw + P.pavement - 1.1f);
                    if (!b_.free(q, 1.0f))
                        continue;
                    if (rng.chance(s_.treeSurvival)) {
                        b_.tree(*sp, streetTrees_, q, rng, kh);
                        b_.seedlings(q, static_cast<int>(rng.range(0.0f, 3.0f) * s_.overgrowth), streetTrees_, rng, kh);
                    } else if (rng.chance(0.35f)) {
                        if (const KitProp* snag = b_.anyProp("snag", rng))
                            b_.prop(*snag, "Dead tree", streetTrees_, q, kh, rng.range(0.0f, 360.0f), rng);
                    } else if (rng.chance(s_.overgrowth)) {
                        b_.plant({"sapling"}, "Seedling", streetTrees_, q, rng, kh);
                    }
                }
        }
        // Weeds in the joint between kerb and road.
        if (s_.overgrowth > 0.01f && P.pavement > 0.0f) {
            const float step = 3.0f / s_.overgrowth;
            for (const float side : {1.0f, -1.0f})
                for (float s = rng.range(0.0f, step); s < L; s += step * rng.range(0.5f, 1.5f)) {
                    glm::vec2 p, t;
                    at(s, p, t);
                    const glm::vec2 n{-t.y, t.x};
                    b_.plant({rng.chance(0.75f) ? "grass" : "weed", "grass"}, "Kerb grass", streetPlants_, p + n * side * (cw - rng.range(0.1f, 0.5f)),
                             rng, 0.0f, 0.1f);
                }
        }
        // Wrecks parked at the kerb, decades in one place.
        const float density = s_.wrecksPer100m * (cls(r.cls) == StreetClass::Main ? 1.0f : 0.7f);
        const int count = static_cast<int>(L / 100.0f * density * rng.range(0.5f, 1.5f) + 0.5f);
        std::vector<float> taken;
        for (int w = 0, tries = 0; w < count && tries < count * 10 && P.carriageway >= 3.0f; ++tries) {
            const float s = rng.range(jc, L - jc);
            if (std::any_of(taken.begin(), taken.end(), [&](float x) { return std::abs(x - s) < 10.0f; }))
                continue;
            const KitProp* car = b_.anyProp("car", rng);
            if (!car)
                break;
            taken.push_back(s);
            ++w;
            glm::vec2 p, t;
            at(s, p, t);
            const glm::vec2 n{-t.y, t.x};
            const float side = rng.chance(0.5f) ? 1.0f : -1.0f;
            const glm::vec2 q = p + n * side * (cw - 1.15f);
            b_.prop(*car, "Wreck", wrecks_, q, -0.05f, b_.yawZ(t * (rng.chance(0.5f) ? 1.0f : -1.0f)) + rng.range(-7.0f, 7.0f), rng,
                    {rng.range(-2.0f, 2.0f), rng.range(-2.5f, 2.5f)});
            ++b_.plan.stats.wrecks;
            for (int g = 0; g < static_cast<int>(8.0f * s_.overgrowth); ++g) {
                const float ang = rng.range(0.0f, 2.0f * kPi), d = rng.range(1.0f, 1.25f);
                b_.plant({"grass", "weed"}, "Grass", wrecks_, q + (t * std::cos(ang) * 2.5f + n * std::sin(ang) * 1.1f) * d, rng, 0.0f, 0.0f);
            }
            b_.addSolid({q.x - 0.8f, q.y - 0.8f, q.x + 0.8f, q.y + 0.8f});
        }
    }

    // ------------------------------------------------------------ greenery
    bool randomIn(const Poly& p, Rng& rng, glm::vec2& out) const
    {
        glm::vec2 lo{1e30f}, hi{-1e30f};
        for (const auto& q : p) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
        for (int i = 0; i < 20; ++i) {
            const glm::vec2 q{rng.range(lo.x, hi.x), rng.range(lo.y, hi.y)};
            if (pointInPolygon(p, q)) {
                out = q;
                return true;
            }
        }
        return false;
    }

    void grove(const Lot& L, const Poly& area, glm::vec2 c, int trees, Rng& rng)
    {
        const float kh = s_.kerbHeight;
        std::vector<glm::vec2> placed;
        for (int i = 0, tries = 0; i < trees && tries < trees * 12; ++tries) {
            const float a = rng.range(0.0f, 2.0f * kPi), d = rng.range(0.0f, 6.0f);
            const glm::vec2 p = c + glm::vec2(std::cos(a), std::sin(a)) * d;
            if (!pointInPolygon(area, p) || !b_.free(p, 1.5f))
                continue;
            if (std::any_of(placed.begin(), placed.end(), [&](glm::vec2 q) { return glm::length(q - p) < 4.0f; }))
                continue;
            if (const KitProp* k = b_.anyProp("tree", rng)) {
                b_.tree(*k, L.group, p, rng, kh);
                placed.push_back(p);
                ++i;
            }
        }
        if (placed.empty())
            return;
        const int bushes = static_cast<int>(rng.range(4.0f, 9.0f) * (0.3f + s_.overgrowth));
        for (int i = 0; i < bushes; ++i) {
            const float a = rng.range(0.0f, 2.0f * kPi), d = rng.range(5.5f, 9.0f);
            const glm::vec2 p = c + glm::vec2(std::cos(a), std::sin(a)) * d;
            if (pointInPolygon(area, p))
                b_.plant({"bush"}, "Bush", L.group, p, rng, kh);
        }
        for (int i = 0; i < static_cast<int>(rng.range(2.0f, 7.0f) * s_.overgrowth); ++i) {
            const float a = rng.range(0.0f, 2.0f * kPi), d = rng.range(1.5f, 7.0f);
            const glm::vec2 p = c + glm::vec2(std::cos(a), std::sin(a)) * d;
            if (pointInPolygon(area, p))
                b_.plant({rng.chance(0.6f) ? "sapling" : "bush", "bush"}, "Seedling", L.group, p, rng, kh);
        }
    }

    void scatterIn(const Lot& L, const Poly& area, float perHundredM2, Rng& rng, const std::vector<const char*>& roles, const char* name)
    {
        const int n = std::min(3000, static_cast<int>(signedArea(area) / 100.0f * perHundredM2));
        for (int i = 0; i < n; ++i) {
            glm::vec2 p;
            if (randomIn(area, rng, p))
                b_.plant(roles, name, L.group, p, rng, s_.kerbHeight);
        }
    }

    // ------------------------------------------------------------ town square
    // Paved in slabs (grass in the joints), a memorial in the middle on the long axis,
    // a dry fountain in front of it when there is room, a ring of lime trees along the
    // edge, benches facing the memorial, lamps at the corners, a kiosk by the street.
    void square(Lot& L, Rng& rng)
    {
        const float kh = s_.kerbHeight;
        b_.ground(Ground::Pavement, L.poly, kh);
        glm::vec2 c, ax, hs;
        minAreaRect(L.poly, c, ax, hs);
        const glm::vec2 ay(-ax.y, ax.x);
        // Slab joints: a grid of verge strokes every 6 m along the square's axes.
        const float R = std::max(hs.x, hs.y) + 2.0f;
        for (float t = -R; t <= R; t += 6.0f) {
            b_.ground(Ground::Verge, {c + ax * t - ay * R, c + ax * t + ay * R}, kh, 0.12f, false);
            b_.ground(Ground::Verge, {c + ay * t - ax * R, c + ay * t + ax * R}, kh, 0.12f, false);
        }
        auto place = [&](const char* role, const char* name, glm::vec2 at, float yaw, float solid) -> bool {
            const KitProp* k = b_.anyProp(role, rng);
            if (!k)
                return false;
            b_.prop(*k, name, L.group, at, kh, yaw, rng, {0.0f, 0.0f}, 1.0f);
            if (solid > 0.0f)
                b_.addSolid({at.x - solid, at.y - solid, at.x + solid, at.y + solid});
            return true;
        };
        // Memorial in the middle, a fountain on the long axis in front of it.
        const bool fountainRoom = hs.x > 38.0f;
        const glm::vec2 mon = fountainRoom ? c - ax * (hs.x * 0.3f) : c;
        place("monument", "War memorial", mon, b_.yawZ(ax), 4.0f);
        b_.line(mon - ax * 3.6f, mon + ax * 3.6f, kColHouse, 0.5f);
        if (fountainRoom) {
            const glm::vec2 f = c + ax * (hs.x * 0.3f);
            place("fountain", "Fountain", f, rng.range(0.0f, 360.0f), 5.2f);
            b_.line(f - ax * 5.0f, f + ax * 5.0f, kColHouse, 0.5f);
        }
        b_.addClear(mon + ax * 3.8f, c + ax * hs.x, 1.5f);
        // Benches on a ring around the memorial, facing it.
        const float br = 9.5f;
        for (int i = 0; i < 8; ++i) {
            const float a = (static_cast<float>(i) + 0.5f) * kPi / 4.0f;
            const glm::vec2 d = ax * std::cos(a) + ay * std::sin(a);
            const glm::vec2 p = mon + d * br;
            if (!pointInPolygon(L.poly, p) || distanceToPolygonEdge(L.poly, p) < 3.0f || !rng.chance(0.8f))
                continue;
            place("bench", "Bench", p, b_.yawZ(-d), 0.0f); // front (+Z of the model) toward the memorial
        }
        // Lime trees along the edge, 3.5 m in, every 8 m; a few gone, seedlings around the rest.
        Poly ring;
        if (insetPolygon(L.poly, std::vector<float>(L.poly.size(), 3.5f), ring)) {
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const glm::vec2 a = ring[i], b = ring[(i + 1) % ring.size()];
                const float len = glm::length(b - a);
                const int n = std::max(1, static_cast<int>(len / 8.0f));
                for (int k = 0; k <= n; ++k) {
                    if (k == n && i + 1 < ring.size())
                        continue;
                    const glm::vec2 p = a + (b - a) * (static_cast<float>(k) / static_cast<float>(n));
                    if (!b_.free(p, 1.0f))
                        continue;
                    if (rng.chance(s_.treeSurvival)) {
                        if (const KitProp* t = b_.anyProp("tree", rng)) {
                            b_.tree(*t, L.group, p, rng, kh);
                            b_.seedlings(p, static_cast<int>(s_.overgrowth * 2.0f + rng.uniform()), L.group, rng, kh);
                        }
                    } else if (const KitProp* t = b_.anyProp(rng.chance(0.5f) ? "snag" : "sapling", rng)) {
                        b_.prop(*t, "Dead tree", L.group, p, kh, rng.range(0.0f, 360.0f), rng);
                    }
                }
            }
            // Lamps at the corners of the ring.
            for (const glm::vec2 q : ring)
                if (const KitProp* k = b_.anyProp(rng.chance(0.2f) ? "lamp_leaning" : "lamp", rng))
                    b_.prop(*k, "Lamp", L.group, q + glm::normalize(c - q) * 1.5f, kh, b_.yawZ(c - q) - 90.0f, rng, {0.0f, 0.0f}, 1.0f);
        }
        // Kiosk by the busiest street edge, its front toward the square.
        int best = 0;
        for (std::size_t i = 0; i < L.poly.size(); ++i)
            if (L.cls[i] > L.cls[static_cast<std::size_t>(best)] ||
                (L.cls[i] == L.cls[static_cast<std::size_t>(best)] &&
                 glm::length(L.poly[(i + 1) % L.poly.size()] - L.poly[i]) > glm::length(L.poly[(static_cast<std::size_t>(best) + 1) % L.poly.size()] - L.poly[static_cast<std::size_t>(best)])))
                best = static_cast<int>(i);
        {
            const glm::vec2 a = L.poly[static_cast<std::size_t>(best)], b = L.poly[(static_cast<std::size_t>(best) + 1) % L.poly.size()];
            const glm::vec2 t = glm::normalize(b - a), n(-t.y, t.x); // inward for a counter-clockwise outline
            const glm::vec2 p = a + (b - a) * rng.range(0.25f, 0.4f) + n * 6.5f;
            if (b_.free(p, 2.0f))
                place("kiosk", "Newsstand", p, b_.yawZ(n), 2.2f);
        }
        // A few cracks have let saplings through; rubble from the memorial's cornice.
        for (int i = 0; i < static_cast<int>(signedArea(L.poly) / 900.0f * s_.overgrowth); ++i) {
            glm::vec2 p;
            if (randomIn(L.poly, rng, p))
                b_.plant({"sapling", "bush"}, "Seedling", L.group, p, rng, kh, 1.0f);
        }
        b_.plan.markers.push_back({s_.squareName.empty() ? std::string("Square") : s_.squareName, L.group, b_.world(c, kh)});
        L.yard.clear(); // nothing else grows here (green() keeps off)
    }

    // Street names on the map, the New York way: the streets along the town's main direction are
    // avenues (named from west to east), the ones across are numbered streets (south to north).
    // Segments on one line share a name; each name is labelled once, on its longest segment.
    void nameStreets()
    {
        static const char* kAvenues[] = {"12th Ave", "11th Ave", "10th Ave", "Amsterdam Ave", "Columbus Ave", "Broadway", "8th Ave", "7th Ave",
                                         "6th Ave",  "5th Ave",  "Madison Ave", "Park Ave", "Lexington Ave", "3rd Ave", "2nd Ave", "1st Ave",
                                         "York Ave", "East End Ave"};
        constexpr int kAvenueCount = static_cast<int>(sizeof(kAvenues) / sizeof(kAvenues[0]));
        // The main direction: length-weighted average of the main streets' directions (doubled angle).
        glm::vec2 acc{0.0f};
        for (const auto& r : net_.roads)
            for (std::size_t i = 1; i < r.points.size(); ++i) {
                const glm::vec2 d = r.points[i] - r.points[i - 1];
                const float w = (r.cls == static_cast<int>(StreetClass::Main) ? 3.0f : 1.0f);
                acc += glm::vec2(d.x * d.x - d.y * d.y, 2.0f * d.x * d.y) * w / std::max(glm::length(d), 1e-3f);
            }
        const float ang = 0.5f * std::atan2(acc.y, acc.x);
        glm::vec2 A{std::cos(ang), std::sin(ang)};
        if (std::abs(A.y) > std::abs(A.x) ? A.y > 0.0f : A.x < 0.0f) // avenues run "north" (towards -z)
            A = -A;
        const glm::vec2 P{-A.y, A.x};
        struct Seg { float length; glm::vec2 mid; };
        std::map<int, Seg> avenues, streets; // key: quantised offset across the line
        const float q = std::max(20.0f, s_.sideSpacing * 0.6f);
        for (const auto& r : net_.roads) {
            if (r.points.size() < 2 || cls(r.cls) == StreetClass::None)
                continue;
            float L = 0.0f;
            for (std::size_t i = 1; i < r.points.size(); ++i)
                L += glm::length(r.points[i] - r.points[i - 1]);
            const glm::vec2 d = r.points.back() - r.points.front();
            if (L < 30.0f || glm::length(d) < 20.0f)
                continue;
            const glm::vec2 mid = r.points[r.points.size() / 2] * 0.5f + r.points[(r.points.size() - 1) / 2] * 0.5f;
            if (std::abs(mid.x) > half_ - 5.0f || std::abs(mid.y) > half_ - 5.0f)
                continue;
            const bool avenue = std::abs(glm::dot(glm::normalize(d), A)) > 0.7071f;
            auto& m = avenue ? avenues : streets;
            const int key = static_cast<int>(std::lround(glm::dot(mid, avenue ? P : A) / q));
            auto it = m.find(key);
            if (it == m.end() || it->second.length < L)
                m[key] = {L, mid};
        }
        const float kh = s_.kerbHeight;
        int i = std::max(0, kAvenueCount / 2 - static_cast<int>(avenues.size()) / 2);
        for (const auto& [key, sg] : avenues) {
            (void)key;
            if (i >= kAvenueCount)
                break;
            b_.plan.markers.push_back({kAvenues[i++], s_.group, b_.world(sg.mid, kh)});
        }
        int n = 14 + static_cast<int>(s_.seed % 20u);
        for (const auto& [key, sg] : streets) {
            (void)key;
            const int t = n % 100;
            const char* suf = (t >= 11 && t <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
            b_.plan.markers.push_back({"W " + std::to_string(n) + suf + " St", s_.group, b_.world(sg.mid, kh)});
            ++n;
        }
    }

    void green(Lot& L)
    {
        Rng rng(mix(L.seed, 4242));
        const float g = s_.overgrowth;
        const Poly& area = L.yard;
        if (area.size() < 3 || signedArea(area) < 30.0f)
            return;
        // Paths from the entrances: nothing grows on them.
        for (const glm::vec2 e : L.entrances) {
            const glm::vec2 c = centroid(area);
            if (glm::length(c - e) > 1.0f) {
                const glm::vec2 to = e + glm::normalize(c - e) * std::min(12.0f, glm::length(c - e));
                b_.addClear(e, to, 1.1f);
                b_.ground(Ground::Path, {e, to}, s_.kerbHeight, 0.9f);
            }
        }
        float grovesPer = 0.0f, young = 0.0f, weeds = 0.0f;
        switch (L.kind) {
        // (grass and most weeds grow through the terrain; these are the bigger plants)
        case BlockKind::Perimeter: grovesPer = 1.0f / 700.0f; young = 0.08f; weeds = 0.2f; break;
        case BlockKind::Microdistrict:
        case BlockKind::Towers: grovesPer = 1.0f / 1000.0f; young = 0.12f; weeds = 0.1f; break;
        case BlockKind::Park: grovesPer = 1.0f / 380.0f; young = 0.25f; weeds = 0.1f; break;
        case BlockKind::Ruin: grovesPer = 1.0f / 900.0f; young = 0.5f; weeds = 0.8f; break;
        case BlockKind::Wasteland: grovesPer = 1.0f / 1500.0f; young = 0.25f; weeds = 0.6f; break;
        case BlockKind::Houses: grovesPer = 1.0f / 2500.0f; young = 0.1f; weeds = 0.1f; break;
        case BlockKind::Garages: young = 0.1f; weeds = 0.5f; break;
        case BlockKind::Industrial: grovesPer = 1.0f / 2000.0f; young = 0.3f; weeds = 0.7f; break;
        default: break;
        }
        const float A = signedArea(area);
        const int groves = static_cast<int>(A * grovesPer * (0.4f + 0.8f * g) + rng.uniform());
        std::vector<glm::vec2> centres;
        for (int tries = 0; tries < groves * 20 && static_cast<int>(centres.size()) < groves; ++tries) {
            glm::vec2 p;
            if (!randomIn(area, rng, p) || distanceToPolygonEdge(area, p) < 4.0f)
                continue;
            if (std::any_of(centres.begin(), centres.end(), [&](glm::vec2 q) { return glm::length(q - p) < 20.0f; }))
                continue;
            centres.push_back(p);
            grove(L, area, p, 2 + rng.below(4), rng);
        }
        if (L.kind == BlockKind::Ruin) // heaps where the houses stood
            for (int i = 0; i < static_cast<int>(A / 250.0f); ++i) {
                glm::vec2 p;
                if (!randomIn(area, rng, p))
                    continue;
                if (const KitProp* k = b_.anyProp(rng.chance(0.2f) ? "slab" : "rubble", rng)) {
                    b_.prop(*k, "Rubble heap", L.group, p, s_.kerbHeight - 0.25f, rng.range(0.0f, 360.0f), rng,
                            {rng.range(-8.0f, 8.0f), rng.range(-8.0f, 8.0f)}, rng.range(1.3f, 2.6f));
                    b_.addSolid({p.x - 2.0f, p.y - 2.0f, p.x + 2.0f, p.y + 2.0f});
                }
            }
        scatterIn(L, area, young * (0.3f + g), rng, {"sapling", "bush"}, "Young tree");
        scatterIn(L, area, weeds * g, rng, {"weed", "nettle", "grass"}, "Weeds");
    }

    void outskirts()
    {
        const float o = s_.outskirts;
        if (o < 1.0f)
            return;
        const std::string grp = s_.group + " outskirts";
        b_.group(grp, s_.group);
        Rng rng(mix(s_.seed, 5150));
        Lot pseudo;
        pseudo.group = grp;
        const float h = half_, H = half_ + o - 3.0f;
        const Poly ring[] = {{{-H, -H}, {H, -H}, {H, -h - 3}, {-H, -h - 3}}, {{-H, h + 3}, {H, h + 3}, {H, H}, {-H, H}},
                             {{-H, -h - 3}, {-h - 3, -h - 3}, {-h - 3, h + 3}, {-H, h + 3}}, {{h + 3, -h - 3}, {H, -h - 3}, {H, h + 3}, {h + 3, h + 3}}};
        for (const Poly& r : ring) {
            if (signedArea(r) < 100.0f)
                continue;
            const int want = static_cast<int>(signedArea(r) / 900.0f * (0.3f + 0.7f * s_.overgrowth));
            std::vector<glm::vec2> centres;
            for (int tries = 0; tries < want * 20 && static_cast<int>(centres.size()) < want; ++tries) {
                glm::vec2 p;
                if (!randomIn(r, rng, p))
                    continue;
                if (std::any_of(centres.begin(), centres.end(), [&](glm::vec2 q) { return glm::length(q - p) < 24.0f; }))
                    continue;
                centres.push_back(p);
                grove(pseudo, r, p, 2 + rng.below(4), rng);
            }
            scatterIn(pseudo, r, 0.2f * s_.overgrowth, rng, {"sapling", "bush"}, "Young tree");
        }
    }

    const Settings& s_;
    Builder b_;
    RoadNetwork net_;
    float half_ = 300.0f;
    int squareBlock_ = -1, landmarkBlock_ = -1;
    std::vector<Lot> lots_;
    std::string streetGroup_, kerbs_, lamps_, streetTrees_, wrecks_, streetPlants_;
};

} // namespace

Plan generate(const Settings& in, const Kit& kit)
{
    Settings s = in;
    s.normalize();
    if (s.layout == Layout::Organic) {
        OrganicLayout layout(s, kit);
        return layout.run();
    }
    CityLayout layout(s, kit);
    return layout.run();
}

// ============================================================================ objects
// ============================================================================ terrain
namespace {

float hash2(int x, int y, std::uint32_t seed)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8da6b343U ^ static_cast<std::uint32_t>(y) * 0xd8163841U ^ seed * 0xcb1ab31fU;
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}
float noise2(glm::vec2 p, std::uint32_t seed)
{
    const float fx = std::floor(p.x), fy = std::floor(p.y);
    const int x = static_cast<int>(fx), y = static_cast<int>(fy);
    float u = p.x - fx, v = p.y - fy;
    u = u * u * (3.0f - 2.0f * u);
    v = v * v * (3.0f - 2.0f * v);
    const float a = hash2(x, y, seed), b = hash2(x + 1, y, seed), c = hash2(x, y + 1, seed), d = hash2(x + 1, y + 1, seed);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}

// Calls fn(ix, iz) for grid points origin + (i + offset) * cell inside the polygon (scanlines).
template <class F>
void rasterPolygon(const std::vector<glm::vec2>& poly, glm::vec2 origin, float cell, float offset, int res, F&& fn)
{
    if (poly.size() < 3)
        return;
    float z0 = 1e30f, z1 = -1e30f;
    for (const auto& p : poly) {
        z0 = std::min(z0, p.y);
        z1 = std::max(z1, p.y);
    }
    const int iz0 = std::max(0, static_cast<int>(std::ceil((z0 - origin.y) / cell - offset)));
    const int iz1 = std::min(res - 1, static_cast<int>(std::floor((z1 - origin.y) / cell - offset)));
    std::vector<float> xs;
    for (int iz = iz0; iz <= iz1; ++iz) {
        const float z = origin.y + (static_cast<float>(iz) + offset) * cell;
        xs.clear();
        for (std::size_t i = 0, n = poly.size(); i < n; ++i) {
            const glm::vec2 a = poly[i], b = poly[(i + 1) % n];
            if ((a.y > z) != (b.y > z))
                xs.push_back(a.x + (z - a.y) * (b.x - a.x) / (b.y - a.y));
        }
        std::sort(xs.begin(), xs.end());
        for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
            const int ix0 = std::max(0, static_cast<int>(std::ceil((xs[k] - origin.x) / cell - offset)));
            const int ix1 = std::min(res - 1, static_cast<int>(std::floor((xs[k + 1] - origin.x) / cell - offset)));
            for (int ix = ix0; ix <= ix1; ++ix)
                fn(ix, iz);
        }
    }
}

template <class F>
void rasterStroke(const std::vector<glm::vec2>& pts, float hw, glm::vec2 origin, float cell, float offset, int res, F&& fn)
{
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const glm::vec2 a = pts[i], b = pts[i + 1];
        const glm::vec2 lo = glm::min(a, b) - glm::vec2(hw), hi = glm::max(a, b) + glm::vec2(hw);
        const int ix0 = std::max(0, static_cast<int>(std::ceil((lo.x - origin.x) / cell - offset)));
        const int ix1 = std::min(res - 1, static_cast<int>(std::floor((hi.x - origin.x) / cell - offset)));
        const int iz0 = std::max(0, static_cast<int>(std::ceil((lo.y - origin.y) / cell - offset)));
        const int iz1 = std::min(res - 1, static_cast<int>(std::floor((hi.y - origin.y) / cell - offset)));
        const glm::vec2 ab = b - a;
        const float L2 = std::max(glm::dot(ab, ab), 1e-8f);
        for (int iz = iz0; iz <= iz1; ++iz)
            for (int ix = ix0; ix <= ix1; ++ix) {
                const glm::vec2 p = origin + (glm::vec2(static_cast<float>(ix), static_cast<float>(iz)) + offset) * cell;
                const float t = std::clamp(glm::dot(p - a, ab) / L2, 0.0f, 1.0f);
                const glm::vec2 d = p - (a + ab * t);
                if (glm::dot(d, d) <= hw * hw)
                    fn(ix, iz);
            }
    }
}

float distanceOutside(const std::vector<glm::vec2>& poly, glm::vec2 p)
{
    if (poly.size() < 3 || pointInPolygon(poly, p))
        return 0.0f;
    return distanceToPolygonEdge(poly, p);
}

} // namespace

std::shared_ptr<Terrain> buildTerrain(const GroundPlan& G, std::vector<std::string>* errors, const std::function<void(float)>& progress)
{
    if (!G.enabled || G.size <= 1.0f)
        return nullptr;
    auto T = std::make_shared<Terrain>();
    const int res = std::clamp(static_cast<int>(std::ceil(G.size / std::max(0.1f, G.spacing))) + 1, 2, 4097);
    const int splatRes = std::clamp(static_cast<int>(std::ceil(G.size / 0.4f)), 256, 2048);
    T->create(G.origin, G.size, res, splatRes);
    const float cell = T->spacing();
    // Rolling land outside the town: low hills that rise from the town edge.
    JobSystem::global().parallelFor(static_cast<std::size_t>(res), 16, [&](std::size_t b, std::size_t e) {
        for (std::size_t iz = b; iz < e; ++iz)
            for (int ix = 0; ix < res; ++ix) {
                const glm::vec2 p = G.origin + glm::vec2(static_cast<float>(ix), static_cast<float>(iz)) * cell;
                const float out = distanceOutside(G.town, p);
                const float f = std::clamp(out / std::max(1.0f, G.hillFalloff), 0.0f, 1.0f);
                const float k = f * f * (3.0f - 2.0f * f);
                const float hills = G.hills * (0.65f * noise2(p / 170.0f, G.seed) + 0.35f * noise2(p / 61.0f, G.seed + 7)) - G.hills * 0.15f;
                const float micro = 0.25f * (noise2(p / 9.0f, G.seed + 3) - 0.5f);
                T->heights[iz * static_cast<std::size_t>(res) + static_cast<std::size_t>(ix)] = G.level - 0.02f + k * (hills + micro) + (1.0f - k) * 0.0f;
            }
    });
    if (progress)
        progress(0.2f);
    // Splat: the base layer, then every shape over it.
    for (std::size_t i = 0; i < T->splat.size(); i += Terrain::kLayers)
        for (int k = 0; k < Terrain::kLayers; ++k)
            T->splat[i + static_cast<std::size_t>(k)] = k == G.baseLayer ? 255 : 0;
    const float px = G.size / static_cast<float>(splatRes);
    for (const auto& sh : G.shapes) {
        auto paint = [&](int ix, int iz) {
            std::uint8_t* p = &T->splat[(static_cast<std::size_t>(iz) * static_cast<std::size_t>(splatRes) + static_cast<std::size_t>(ix)) * Terrain::kLayers];
            for (int k = 0; k < Terrain::kLayers; ++k)
                p[k] = k == sh.layer ? 255 : 0;
        };
        auto lift = [&](int ix, int iz) { T->heights[static_cast<std::size_t>(iz) * static_cast<std::size_t>(res) + static_cast<std::size_t>(ix)] = sh.height; };
        if (sh.halfWidth > 0.0f) {
            rasterStroke(sh.points, sh.halfWidth, G.origin, px, 0.5f, splatRes, paint);
            if (sh.setHeight)
                rasterStroke(sh.points, sh.halfWidth, G.origin, cell, 0.0f, res, lift);
        } else {
            rasterPolygon(sh.points, G.origin, px, 0.5f, splatRes, paint);
            if (sh.setHeight)
                rasterPolygon(sh.points, G.origin, cell, 0.0f, res, lift);
        }
    }
    if (progress)
        progress(0.5f);
    for (int i = 0; i < kGroundCount && i < Terrain::kLayers; ++i) {
        TerrainLayer& l = T->layers[static_cast<std::size_t>(i)];
        l.materialPath = G.materials[static_cast<std::size_t>(i)];
        l.tiling = i == static_cast<int>(Ground::Road) ? 0.2f : 0.25f;
        if (!l.materialPath.empty()) {
            std::string err;
            l.material = loadMaterial(l.materialPath, &err);
            if (!l.material && errors)
                errors->push_back(l.materialPath.string() + ": " + err);
        }
    }
    // Ground cover.
    for (const auto& f : G.foliage) {
        FoliageType ft;
        ft.modelPath = f.path;
        auto m = std::make_shared<Model>();
        std::string err;
        if (!loadModelFile(*m, f.path, &err)) {
            if (errors)
                errors->push_back(f.path.string() + ": " + err);
            continue;
        }
        ft.model = std::move(m);
        if (!f.lodPath.empty()) {
            FoliageType::Lod lod;
            lod.modelPath = f.lodPath;
            auto lm = std::make_shared<Model>();
            if (loadModelFile(*lm, f.lodPath, nullptr)) {
                lod.model = std::move(lm);
                lod.distance = f.lodDistance;
                ft.lods.push_back(std::move(lod));
            }
        }
        ft.density = f.density;
        ft.minScale = f.scaleMin;
        ft.maxScale = f.scaleMax;
        ft.cullDistance = f.cull;
        ft.collision = f.collision;
        ft.alignToSlope = !f.collision;
        ft.castShadows = f.shadows;
        ft.farDensity = f.farDensity;
        ft.sway = f.sway;
        for (int i = 0; i < FoliageType::kLayerSlots; ++i)
            ft.layerDensity[static_cast<std::size_t>(i)] = i < kGroundCount ? f.on[static_cast<std::size_t>(i)] : 0.0f;
        T->foliageTypes.push_back(std::move(ft));
    }
    std::vector<std::vector<FoliageInstance>> inst(T->foliageTypes.size());
    JobSystem::global().parallelFor(inst.size(), 1, [&](std::size_t b, std::size_t e) {
        for (std::size_t i = b; i < e; ++i)
            inst[i] = T->scatterFoliage(static_cast<int>(i), G.seed * 31u + static_cast<std::uint32_t>(i));
    });
    for (auto& v : inst)
        T->foliage.insert(T->foliage.end(), v.begin(), v.end());
    T->foliageVersion = T->splatVersion + 1; // changed
    if (progress)
        progress(1.0f);
    return T;
}

CityObjects buildObjects(const Plan& plan, const std::function<void(float)>& progress, const std::function<bool()>& cancelled)
{
    CityObjects out;
    // Houses of the grammar: written once per distinct house (the file name is a hash of it).
    {
        std::vector<const GeneratedBuilding*> todo;
        std::set<fs::path> seen;
        for (const auto& g : plan.generated)
            if (seen.insert(g.file).second && (!fs::exists(g.file) || !fs::exists(g.lodFile)))
                todo.push_back(&g);
        std::vector<std::string> errs(todo.size());
        std::atomic<int> done{0};
        JobSystem::global().parallelFor(todo.size(), 1, [&](std::size_t b, std::size_t e) {
            for (std::size_t i = b; i < e; ++i) {
                if (cancelled && cancelled())
                    return;
                const GeneratedBuilding& g = *todo[i];
                const fs::path tex = plan.textures;
                if (!writeBuildingGlb(g.spec, g.file, 0, tex, &errs[i]) || !writeBuildingGlb(g.spec, g.lodFile, 1, tex, &errs[i]))
                    if (errs[i].empty())
                        errs[i] = "cannot write " + g.file.string();
                if (progress)
                    progress(0.25f * static_cast<float>(++done) / static_cast<float>(std::max<std::size_t>(todo.size(), 1)));
            }
        });
        for (const auto& e : errs)
            if (!e.empty())
                out.errors.push_back(e);
        out.housesWritten = static_cast<int>(todo.size());
    }
    if (cancelled && cancelled())
        return out;
    if (plan.ground.enabled) {
        out.terrain = buildTerrain(plan.ground, &out.errors, [&](float f) {
            if (progress)
                progress(0.25f + 0.25f * f);
        });
    }
    auto sub = [&](float f) {
        if (progress)
            progress(0.5f + 0.5f * f);
    };
    for (const auto& g : plan.groups)
        out.groups.push_back({g.name, g.parent, {}});
    for (const auto& m : plan.markers) {
        Entity e;
        e.name = m.name;
        e.parent = m.group;
        e.type = "map_marker";
        e.position = m.position;
        e.size = {1.0f, 1.0f, 1.0f};
        e.color = {0.85f, 0.3f, 0.2f};
        out.entities.push_back(std::move(e));
    }
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
            sub(0.8f * static_cast<float>(++done) / static_cast<float>(std::max<std::size_t>(files.size(), 1)));
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
        if ((++k % 512) == 0)
            sub(0.8f + 0.2f * static_cast<float>(k) / static_cast<float>(plan.placements.size()));
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
        // Out of town the land rolls: stand on it.
        if (out.terrain && !pointInPolygon(plan.ground.town, {p.position.x, p.position.z}))
            m.position.y = out.terrain->heightAt(p.position.x, p.position.z) +
                           std::max(-0.3f, p.position.y - (plan.ground.level + plan.ground.kerb));
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
    for (auto& e : objects.entities) {
        e.name = unique(e.name);
        e.parent = parentOf(e.parent);
        scene.entities.push_back(std::move(e));
    }
    if (objects.terrain)
        scene.terrain = std::move(objects.terrain);
    assignObjectIds(scene);
    return removed;
}

} // namespace engine::city
