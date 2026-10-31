#pragma once
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
    std::filesystem::path decal;    // absolute image of the bullet hole, empty = none
    float decalSize = 0.12f;        // m, bullet hole diameter
    float stepVolume = 1.0f;
};

// The project's surface list, stored as assets/surfaces.cfg and edited in the
// editor (Tools > Surfaces). Lines:
//   surface <name> step "<file>" impact "<file>" decal "<file>" decal_size <m> step_volume <v>
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
