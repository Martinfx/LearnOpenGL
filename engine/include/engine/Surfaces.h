#pragma once
#include <glm/vec3.hpp>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

struct Material;

// What a surface is made of, for sounds and effects: footsteps, bullet impacts
// and bullet holes. Materials name their surface (`surface concrete` in the
// .material, editor: Material editor > Surface); without one it is guessed
// from the material's name and file (e.g. "rusty_metal_02" -> metal).
struct SurfaceType {
    std::string name = "concrete";
    std::filesystem::path footstep; // absolute .sound, empty = the player's default step
    std::filesystem::path impact;   // absolute .sound, empty = the weapon's impact sound
    std::filesystem::path decal;    // absolute image (atlas) of the bullet hole, empty = none
    int decalColumns = 4, decalRows = 4; // atlas layout
    int decalRow = -1;              // atlas row of this surface, -1 = any
    float decalSize = 0.12f;        // m, bullet hole diameter
    std::filesystem::path effect;   // absolute .particle played at a bullet impact (dust, sparks, splinters)
    std::filesystem::path casing;   // absolute .sound of a spent case landing on it
    float stepVolume = 1.0f;
    // Ricochets: a bullet hitting at a shallow angle may glance off and fly on.
    float ricochet = 0.0f;           // chance (0..1) for a shot that just grazes the surface
    float ricochetAngle = 20.0f;     // degrees between shot and surface; steeper hits never glance off
    std::filesystem::path ricochetSound;  // absolute .sound (whine), empty = none
    std::filesystem::path ricochetEffect; // absolute .particle (sparks along the new path), empty = none
};

// Chance (0..1) that a shot along `dir` glances off a surface with `normal`: the
// surface's `ricochet` for a grazing hit, falling to 0 at `ricochetAngle`.
float ricochetChance(const SurfaceType& surface, glm::vec3 dir, glm::vec3 normal);
// Mirror of `dir` on the surface, tilted away from it a little (glancing bullets
// leave flatter than they came; `liftDegrees` keeps them off the surface).
glm::vec3 ricochetDirection(glm::vec3 dir, glm::vec3 normal, float liftDegrees = 3.0f);

// The project's surface list, stored as assets/surfaces.cfg and edited in the
// editor (Tools > Surfaces). Lines:
//   surface <name> step "<file>" impact "<file>" effect "<file>" decal "<file>" decal_grid <cols> <rows>
//           decal_row <row> decal_size <m> step_volume <v> ricochet <chance> <max angle> ricochet_sound "<file>"
//           ricochet_effect "<file>"
// (keys after the name are optional; paths are relative to the file).
struct SurfaceTable {
    std::vector<SurfaceType> types;
    std::string fallback = "concrete"; // surfaces not in the list play as this one (`default <name>`)

    const SurfaceType* find(std::string_view name) const; // exact name, else the fallback, else null
};

bool loadSurfaces(SurfaceTable& table, const std::filesystem::path& file, std::string* error = nullptr);
bool saveSurfaces(const SurfaceTable& table, const std::filesystem::path& file, std::string* error = nullptr);

// Names offered in the editor (the built-in guesses): concrete, metal, wood, dirt,
// grass, gravel, water, glass, flesh.
const std::vector<std::string>& standardSurfaceNames();
// Surface of a free text (material / file / glTF material name), empty if nothing matches.
std::string guessSurface(std::string_view text);
// A material's surface: its own setting, else guessed from its name and file, else `fallbackText` guessed.
std::string surfaceOf(const Material* material, std::string_view fallbackText = {});

} // namespace engine
