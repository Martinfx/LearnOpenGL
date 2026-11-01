#pragma once
#include <engine/AssetRegistry.h>

#include <filesystem>
#include <string>
#include <vector>

namespace engine {

// Credits and licences of the shipped assets. Every model, texture, sound and font
// needs a CREDITS.md in its folder or a parent folder that names its author and
// licence (the game's Credits screen shows them; CC-BY asks for that attribution).
// Folders called "source" (Blender files, generators, raw data) do not ship and are skipped.

// Files whose credits are checked: .glb .gltf .png .jpg .jpeg .tga .hdr .exr .ktx2 .dds
// .wav .ogg .mp3 .flac .ttf .otf
bool needsCredits(const std::filesystem::path& file);

// The CREDITS.md that covers `file` (nearest folder upwards, at most up to `root`); empty if none.
std::filesystem::path creditsFileFor(const std::filesystem::path& file, const std::filesystem::path& root);

// Licences a CREDITS.md names, as short ids: "CC0", "CC-BY", "CC-BY-SA", "public domain",
// "OFL", "Bitstream Vera", "ODbL", "MIT", "Apache-2.0" and the unusable ones "NC" / "ND" / "GPL".
std::vector<std::string> licencesIn(const std::string& creditsText);

// Errors: a media file no CREDITS.md covers, a CREDITS.md that names no licence, a licence
// that does not allow a commercial game (NonCommercial, NoDerivatives).
// Warnings: share-alike or GPL licences (they bind what is made from the asset).
std::vector<AssetIssue> checkAssetCredits(const std::filesystem::path& root);

} // namespace engine
