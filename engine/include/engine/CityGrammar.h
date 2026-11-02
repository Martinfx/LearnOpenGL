#pragma once
#include "engine/CityRoads.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::city {

// ---------------------------------------------------------------------------
// Buildings from a footprint by rules (a small CGA shape grammar, after Mueller,
// Wonka, Haegler, Ulmer, Van Gool: "Procedural Modeling of Buildings", 2006):
//
//   Lot      -> extrude(floors)                 footprint of any shape (the parcel)
//   Mass     -> comp(faces): Facade per edge, Roof on top
//   Facade   -> street | yard | party wall      (the edge's role on the parcel)
//   Facade   -> split(y): Plinth GroundFloor Floor* Cornice
//   Floor    -> split(x): Pier Tile{bay}* Pier  (bays of the style's width, fitted)
//   Tile     -> Window | Loggia | Balcony | Door | ShopWindow | Wall
//   Window   -> Reveal Frame (Glass | Broken | Boarded | Empty) Sill Streak
//   Roof     -> flat with a parapet (and a lift room) | hipped sheet roof
// Decay is part of the rules: broken and boarded windows, rain streaks under the
// sills and the roof edge, fallen loggia parapets, and ruins whose upper storeys
// are gone column by column. Randomness comes from the building's seed only.
//
// The result is written as a .glb (and a _lod1.glb: the mass with dark window
// insets) that references the shared zone texture sets, so a generated house is
// an ordinary model in the level, cached by a hash of its parameters.
// ---------------------------------------------------------------------------

struct FacadeStyle {
    std::string name = "Tenement";
    std::string role = "tenement";   // tenement, panel, khrushchyovka, house (what the zoning asks for)
    int floorsMin = 4, floorsMax = 6;
    float groundStorey = 4.0f;       // ground floor height (m)
    float storey = 3.3f;             // upper floors
    float bay = 3.4f;                // window axis spacing (fitted to each facade)
    float windowWidth = 1.3f, windowHeight = 1.9f, sill = 0.9f;
    float depthMin = 12.0f, depthMax = 14.0f;   // building depth behind the street line
    float widthMin = 14.0f, widthMax = 30.0f;   // parcel widths along the street
    std::string wall = "brick_red", plinth = "concrete", trim = "plaster", roof = "roof_tar", firewall = "brick";
    float cornice = 0.45f;           // projection of the cornice (0 = none)
    float bands = 1.0f;              // string course over the ground floor (0/1)
    float loggias = 0.0f;            // share of window columns that are loggia stacks
    float balconies = 0.15f;         // chance of a balcony column
    float shops = 0.3f;              // chance of shop windows on the street ground floor
    bool pitched = true;             // hipped sheet roof (else flat with a parapet)
    float weight = 1.0f;
    bool operator==(const FacadeStyle&) const = default;
};

// Materials the grammar knows (zone texture sets), for the editor's combo boxes.
const std::vector<std::string>& grammarMaterials();

enum class EdgeRole : std::uint8_t { Street, Yard, Party };

struct BuildingSpec {
    FacadeStyle style;
    Poly footprint;                  // local (x, z) metres, counter-clockwise in (x, z), around the model origin
    std::vector<EdgeRole> edges;     // role of edge i (footprint[i] -> footprint[i + 1])
    int floors = 5;
    std::uint32_t seed = 0;
    bool ruin = false;               // upper storeys gone, open to the sky
};

float buildingHeight(const BuildingSpec& b); // to the top of the wall (m)
std::string buildingKey(const BuildingSpec& b); // stable hash (file name)

struct GrammarStats {
    int triangles = 0;
    int windows = 0;
};

// Writes the .glb; `textureDir` is where the zone texture sets are (made relative to the file).
bool writeBuildingGlb(const BuildingSpec& b, const std::filesystem::path& file, int lod, const std::filesystem::path& textureDir,
                      std::string* error = nullptr, GrammarStats* stats = nullptr);

} // namespace engine::city
