#pragma once
#include <glm/vec3.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace engine {

// Ambience set (text file .ambience): the sound of a place in layers. Looping
// layers play all the time (wind, distant drone, birds); random sounds play now
// and then somewhere around the listener (a crow, a far shot, creaking metal),
// more or less often by day and by night. The level has one (Environment >
// Sound) and "ambience" zone entities have their own, crossfaded at the zone edge.
struct AmbienceLayer {
    std::filesystem::path sound; // absolute .sound
    float volume = 1.0f;
    float day = 1.0f, night = 1.0f; // volume multiplier by day / by night (birds 1 0, crickets 0 1)
};

struct AmbienceRandom {
    std::filesystem::path sound;   // absolute .sound (3D)
    float minInterval = 20.0f;     // seconds between tries
    float maxInterval = 60.0f;
    float minDistance = 50.0f;     // m from the listener
    float maxDistance = 200.0f;
    float day = 1.0f;              // chance (0..1) that a try plays by day
    float night = 1.0f;            // ... by night (blended at dusk and dawn)
    float height = 0.0f;           // m above the listener (birds, a roof), +-50 % at random
    float duck = 0.0f;             // 0..1: how much the ambience group ducks when it plays (a far explosion)
};

struct AmbienceSet {
    std::vector<AmbienceLayer> layers;
    std::vector<AmbienceRandom> randoms;
};

// Paths in the file are relative to it; in memory they are absolute.
bool loadAmbienceFile(AmbienceSet& set, const std::filesystem::path& file, std::string* error = nullptr);
bool saveAmbienceFile(const AmbienceSet& set, const std::filesystem::path& file, std::string* error = nullptr);

// 0 by day .. 1 at night, from the direction the sunlight travels (Scene::sunDirection):
// fades over the twilight while the sun is within ~6 degrees of the horizon.
float nightFactor(glm::vec3 sunDirection);

// Volume of a layer now (its day/night multipliers blended).
float ambienceLayerVolume(const AmbienceLayer& l, float night);
// Chance that a random sound plays now (its day/night chances blended).
float ambienceChance(const AmbienceRandom& r, float night);

} // namespace engine
