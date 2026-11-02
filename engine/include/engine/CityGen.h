#pragma once
#include "engine/CityGrammar.h"
#include "engine/Scene.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine::city {

// ---------------------------------------------------------------------------
// City generator: lays out streets, blocks and lots of a post-Soviet town by
// rules and builds them from a modular kit (docs/CITY_GENERATOR.md).
//
// It works like CityEngine or a Houdini city tool, not like a scatter brush:
//   1. The street grid is designed, not random: block sizes, the class of each
//      street (main, side, alley, none) and the kind of every block are settings
//      edited in the editor (City generator dock). The cross-section of a street
//      class is exact (carriageway + pavements, 1 unit = 1 m).
//   2. Every block kind has its own planning rules: perimeter blocks of pre-war
//      tenements close the street wall corner to corner (pinwheel corners, the
//      frontage filled exactly with kit houses and courtyard gateways); panel
//      micro-districts set slabs in rows spaced by their height; towers, garages,
//      parks, ruins and waste lots follow their own layouts.
//   3. Street furniture is placed by measure: kerbs in 6 m modules, lamp posts
//      at a fixed spacing staggered across the street, cables lamp to lamp and
//      facade to facade, planted tree rows of one species per street.
//   4. Nature and decay come last, by the rules of docs/LEVEL_ART_GUIDE.md: lost
//      trees in the planted rows, seedlings near the survivors, grass along the
//      kerbs, nettles at the foot of walls, groves in courtyard corners, wrecks
//      parked at the kerb, rubble under the facades. The middle of streets and the
//      paths from gateways stay clear.
// Randomness only picks among options the rules allow, from a seed per block, so
// editing one block does not reshuffle the others and the result is the same on
// every platform (own RNG, no std distributions).
//
// generate() is a pure function (call it on a worker thread). The editor turns
// the plan into ordinary boxes, models and groups (buildObjects), so everything
// can be moved, deleted or replaced by hand afterwards.
// ---------------------------------------------------------------------------

enum class StreetClass { None, Alley, Side, Main };
constexpr int kStreetClassCount = 4;
const char* streetClassName(StreetClass c);          // "none", "alley", "side", "main"
StreetClass streetClassFromName(const std::string& name);

enum class BlockKind { Perimeter, Microdistrict, Towers, Garages, Park, Ruin, Wasteland, Empty, Houses, Square };
constexpr int kBlockKindCount = 10;
const char* blockKindName(BlockKind k);              // "perimeter", ... (file keys)
const char* blockKindLabel(BlockKind k);             // "Perimeter block (tenements)", ... (editor)
BlockKind blockKindFromName(const std::string& name);

// Cross-section of a street class: facade to facade = carriageway + 2 * pavement.
struct StreetProfile {
    float carriageway = 9.0f;
    float pavement = 3.5f;
    bool lamps = true;        // lamp posts along it
    bool trees = false;       // planted tree rows on the pavements
    bool crossCables = false; // cables across the street between the facades
    float width() const { return carriageway + 2.0f * pavement; }
    bool operator==(const StreetProfile&) const = default;
};

// ------------------------------------------------------------------ kit
// A building module: a whole house as one model. Frame (as zonegen/tenement.py):
// length along local X centred on 0, street facade facing +Z at z = front, the
// back at z = front - depth, y = 0 the ground floor level.
struct KitBuilding {
    std::string name;
    std::filesystem::path path;      // absolute
    std::filesystem::path lodPath;   // absolute, empty = none
    float lodDistance = 70.0f;
    std::string role = "tenement";   // tenement, panel, tower, garage
    float length = 30.0f;            // nominal length (houses in a row are this far apart)
    float depth = 14.0f;             // facade to back
    float front = 7.0f;              // facade plane at local z = front
    int floors = 5;
    float height = 20.0f;            // to the roof (rules: row spacing, skyline)
    float weight = 1.0f;             // how often it is picked among the allowed ones
    float cull = 600.0f;
    bool operator==(const KitBuilding&) const = default;
};

// Props, vegetation and debris by role ("kerb", "lamp", "cable", "car", "tree",
// "sapling", "snag", "bush", "grass", "weed", "nettle", "rubble", "slab").
struct KitProp {
    std::string role;
    std::filesystem::path path;      // absolute
    std::filesystem::path lodPath;   // absolute, empty = none
    float lodDistance = 60.0f;
    float size = 0.0f;               // module length (kerb, cable span), 0 = not a module
    float cull = 0.0f;               // 0 = always drawn
    float sway = 0.0f;
    float impostor = 0.0f;
    float scaleMin = 1.0f, scaleMax = 1.0f;
    float yaw = 0.0f;                // degrees added when placed (models whose length runs along X)
    bool collision = true;
    bool shadows = true;
    bool operator==(const KitProp&) const = default;
};

// Ground kinds of the terrain the generator paints (terrain layer = index).
enum class Ground { Yard, Lot, Road, Pavement, Meadow, Path, Built, Verge };
constexpr int kGroundCount = 8;   // = Terrain::kLayers
const char* groundName(int g);                       // "yard", "lot", "road", "pavement", "meadow", "path", "built", "verge"
// verge: the dirt joint along the kerbs and at the foot of the walls, where grass and weeds take hold first.

// Ground cover grown through the terrain's foliage (instanced, dense): grass, weeds.
struct KitFoliage {
    std::string name;
    std::filesystem::path path;      // absolute
    std::filesystem::path lodPath;   // absolute, empty = none
    float lodDistance = 25.0f;
    float density = 0.1f;            // instances per m2 where it grows fully
    std::array<float, kGroundCount> on{1.0f, 0.3f, 0.02f, 0.05f, 1.0f, 0.0f, 0.0f, 1.0f}; // share by ground kind
    float cull = 60.0f;
    float farDensity = 0.4f;
    float sway = 1.0f;
    float scaleMin = 0.8f, scaleMax = 1.2f;
    bool collision = false;
    bool shadows = false;
    bool operator==(const KitFoliage&) const = default;
};

struct Kit {
    std::vector<KitBuilding> buildings;
    std::vector<KitProp> props;
    std::vector<FacadeStyle> styles;   // shape-grammar houses (organic layout, any parcel shape)
    std::vector<KitFoliage> foliage;   // terrain ground cover
    std::filesystem::path textures;    // zone texture sets the generated houses use (absolute)
    // Ground materials (absolute .material paths): road, pavement, yard (courtyards
    // and green), lot (bare ground of ruins and waste lots), gate (rusty metal), pillar (brick).
    std::filesystem::path road, pavement, yard, lot, gate, pillar;
    std::vector<const KitBuilding*> buildingsWithRole(const std::string& role) const;
    std::vector<const KitProp*> propsWithRole(const std::string& role) const;
    std::vector<const FacadeStyle*> stylesWithRole(const std::string& role) const;
    bool operator==(const Kit&) const = default;
};

// .citykit text file (paths relative to it). Empty error = fine.
bool loadKit(Kit& kit, const std::filesystem::path& file, std::string* error = nullptr);
bool saveKit(const Kit& kit, const std::filesystem::path& file, std::string* error = nullptr);
const std::vector<std::string>& propRoles(); // the roles the generator uses, for the editor

// ------------------------------------------------------------------ settings
struct BlockSettings {
    BlockKind kind = BlockKind::Perimeter;
    std::uint32_t seed = 0;          // added to the city seed for this block: re-roll one block only
    bool operator==(const BlockSettings&) const = default;
};

enum class Layout { Grid, Organic };

struct Settings {
    std::uint32_t seed = 1957;
    // Grid: designed street grid below. Organic: streets grown from a tensor field
    // (CityRoads.h), blocks cut into parcels, zoning by distance from the centre,
    // houses built by the shape grammar (CityGrammar.h).
    Layout layout = Layout::Grid;
    glm::vec3 origin{0.0f};          // centre of the city, y = road level
    float rotation = 0.0f;           // degrees around Y
    std::string group = "City";      // root group of the generated objects
    std::filesystem::path kit;       // absolute .citykit

    // Grid: lot sizes (facade line to facade line) across X (columns) and Z (rows).
    std::vector<float> columns{110.0f, 130.0f, 110.0f};
    std::vector<float> rows{90.0f, 120.0f};
    // Street on each grid line: columns.size() + 1 lines across X (west to east),
    // rows.size() + 1 across Z (north to south).
    std::vector<StreetClass> streetsX{StreetClass::Side, StreetClass::Main, StreetClass::Main, StreetClass::Side};
    std::vector<StreetClass> streetsZ{StreetClass::Side, StreetClass::Main, StreetClass::Side};
    std::vector<BlockSettings> blocks; // rows * columns, row-major; missing = perimeter
    std::array<StreetProfile, kStreetClassCount> profiles{
        StreetProfile{0.0f, 0.0f, false, false, false},  // none
        StreetProfile{4.0f, 1.5f, false, false, false},  // alley
        StreetProfile{7.0f, 2.5f, true, false, false},   // side
        StreetProfile{9.0f, 3.5f, true, true, true}};    // main: 16 m between the facades
    float kerbHeight = 0.15f;        // pavements and lots above the road

    // Perimeter blocks.
    float gatewayMin = 4.0f;         // courtyard gateway between two houses (m)
    float gatewayMax = 9.0f;
    float gatewayEvery = 70.0f;      // about one gateway per this much frontage
    int maxFloorStep = 3;            // neighbours differ by at most this many floors
    bool tallCorners = true;         // corner houses are the tallest that fit (accents)
    float overlap = 0.1f;            // party walls overlap: no sunlit slit between houses
    bool courtyardGarages = true;    // a garage row in big courtyards

    // Micro-districts and towers.
    float setback = 12.0f;           // green strip between the street and the slabs
    float rowGapFactor = 1.0f;       // gap between slab rows = factor * slab height ...
    float rowGapMin = 24.0f;         // ... but at least this
    float slabGap = 18.0f;           // between slabs in a row

    // Street dressing.
    float lampSpacing = 30.0f;
    float cableChance = 0.7f;        // lamp to lamp
    float crossCableSpacing = 17.0f;
    float treeSpacing = 8.0f;        // planted rows
    float treeSurvival = 0.6f;       // share of the planted trees still standing
    float wrecksPer100m = 1.2f;
    float rubblePer100m = 2.5f;

    // Nature taking the town back: 0 = kept, 1 = decades of nobody.
    float overgrowth = 0.7f;
    // Wild ground around the town (m, 0 = none): the roads run on into it, young wood grows over it.
    float outskirts = 60.0f;

    // Ground: the terrain under the town (dense grass through its foliage, hills around).
    bool terrainGround = false;      // grid layout: terrain instead of ground boxes (organic: always)
    float groundSpacing = 0.5f;      // terrain sample spacing (m)
    float hills = 4.0f;              // height of the rolling land around the town (m)
    float foliageDensity = 1.0f;     // multiplies the kit's ground cover

    // Organic layout: the street network (CityRoads.h NetworkSettings).
    float townSize = 640.0f;         // side of the town square (m)
    float roadAngle = 0.0f;          // heading of the main grid (degrees)
    float districtTurn = 30.0f;      // other districts turn up to this much
    int districts = 3;
    float radial = 0.35f;            // ring roads around the centre
    float curvature = 0.25f;         // streets bending with the land
    float noiseScale = 260.0f;
    float mainSpacing = 150.0f;
    float sideSpacing = 75.0f;
    StreetClass edgeRoad = StreetClass::Side; // road round the town
    // Zoning (organic): distance from the centre as a share of the town radius.
    float centreZone = 0.3f;         // inside: tenement perimeter blocks
    float innerZone = 0.65f;         // to here: tenements, panel micro-districts and towers; beyond: outskirts
    float ruinShare = 0.15f;         // blocks left in ruins
    float parkShare = 0.08f;         // blocks left green
    float houseShare = 0.3f;         // outer blocks of private houses (cut into parcels by recursive subdivision)
    bool square = true;              // the block nearest the centre is the town square (the goal of the map)
    std::filesystem::path cache;     // generated houses (.glb), absolute; empty = next to the .city

    int blockCount() const { return static_cast<int>(rows.size() * columns.size()); }
    BlockSettings block(int row, int column) const;
    void setBlock(int row, int column, const BlockSettings& b);
    void normalize(); // street and block lists sized to the grid, sane ranges
    bool operator==(const Settings&) const = default;
};

// .city text file (kit path relative to it).
bool loadSettings(Settings& settings, const std::filesystem::path& file, std::string* error = nullptr);
bool saveSettings(const Settings& settings, const std::filesystem::path& file, std::string* error = nullptr);

// ------------------------------------------------------------------ plan
struct Placement {
    enum class Kind { Model, Box } kind = Kind::Model;
    std::string name;
    std::string group;
    std::filesystem::path path;      // model file or box material (absolute)
    std::filesystem::path lodPath;
    float lodDistance = 70.0f;
    glm::vec3 position{0.0f};        // model origin / box centre (world)
    glm::vec3 rotation{0.0f};        // degrees (model: tilt x, yaw y, tilt z)
    glm::vec3 size{1.0f};            // box size
    float scale = 1.0f;
    float cull = 0.0f;
    float sway = 0.0f;
    float impostor = 0.0f;
    bool collision = true;
    bool shadows = true;
};

struct PlanGroup {
    std::string name, parent;
};

struct PlanStats {
    int buildings = 0, gateways = 0, lamps = 0, trees = 0, wrecks = 0, plants = 0, boxes = 0;
    int generated = 0, parcels = 0, blocks = 0, roads = 0;
    float frontage = 0.0f;           // m of street wall built
    float worstFill = 0.0f;          // largest frontage mismatch (m); 0 = exact
};

// A house the grammar builds (written to `file` / `lodFile` by buildObjects).
struct GeneratedBuilding {
    BuildingSpec spec;
    std::filesystem::path file, lodFile;
};

// The terrain under the town, painted from shapes (world x/z).
struct GroundShape {
    int layer = 0;                   // Ground
    float height = 0.0f;
    bool setHeight = true;
    float halfWidth = 0.0f;          // > 0: a stroke along `points` (roads), else a polygon
    std::vector<glm::vec2> points;
};
struct GroundPlan {
    bool enabled = false;
    glm::vec2 origin{0.0f};          // min x/z corner
    float size = 0.0f;
    float spacing = 0.5f;
    int baseLayer = static_cast<int>(Ground::Meadow);
    std::vector<GroundShape> shapes; // painted in order, later over earlier
    std::vector<glm::vec2> town;     // outline of the town (world): the hills rise outside it
    float hills = 0.0f;
    float hillFalloff = 60.0f;       // m from the town to full height
    float level = 0.0f;              // road level of the town (world y)
    float kerb = 0.15f;              // lots above the road
    std::uint32_t seed = 0;
    std::array<std::filesystem::path, kGroundCount> materials;
    std::vector<KitFoliage> foliage; // densities already scaled
};

// A named place shown on the PDA map (map_marker entity), e.g. the town square.
struct PlanMarker {
    std::string name, group;
    glm::vec3 position{0.0f};
};

struct Plan {
    std::vector<PlanGroup> groups;   // parents before children
    std::vector<PlanMarker> markers;
    std::vector<Placement> placements;
    std::vector<GeneratedBuilding> generated;
    std::filesystem::path textures;  // texture sets of the generated houses
    GroundPlan ground;
    // Layout preview (world lines, 2 vertices each, one colour per line):
    // street axes, lot outlines, building footprints, gateways, clear paths.
    std::vector<glm::vec3> lines;
    std::vector<glm::vec4> lineColors;
    std::vector<std::string> warnings;
    PlanStats stats;
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};
};

// Lays the whole city out. Pure: same settings and kit, same plan.
Plan generate(const Settings& settings, const Kit& kit);

// The terrain of a ground plan: heights, splat layers and the ground cover scattered
// over them (heavy: run on a worker).
std::shared_ptr<Terrain> buildTerrain(const GroundPlan& ground, std::vector<std::string>* errors = nullptr,
                                      const std::function<void(float)>& progress = {});

// The objects of a plan, models read once per file (in parallel on the job
// system) and shared between copies. `cancelled` (optional) is polled.
struct CityObjects {
    std::vector<Group> groups;
    std::vector<Box> boxes;
    std::vector<Model> models;
    std::vector<Entity> entities;     // map markers
    std::shared_ptr<Terrain> terrain; // the ground, when the plan has one
    std::vector<std::string> errors;
    int housesWritten = 0;            // generated houses written now (others were cached)
};
CityObjects buildObjects(const Plan& plan, const std::function<void(float)>& progress = {},
                         const std::function<bool()>& cancelled = {});

// Removes the root group, everything below it, and inserts the new objects
// under `root` (the previous generation is replaced; hand-placed objects outside
// it stay). Object names are made unique against the rest of the scene. A
// terrain in `objects` replaces the scene's terrain.
// Returns the number of removed objects.
int replaceCity(Scene& scene, const std::string& root, CityObjects objects);

} // namespace engine::city
