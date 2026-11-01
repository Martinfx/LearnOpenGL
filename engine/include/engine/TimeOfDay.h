#pragma once
#include <glm/glm.hpp>

#include <vector>

namespace engine {

struct Environment;

// Weather at one hour of the day; the cycle interpolates between the keys
// (wrapping around midnight). Values replace the level's environment values.
struct WeatherKey {
    float hour = 12.0f;        // 0..24
    float cloudCover = 0.45f;
    float fogDensity = 0.012f;
    float rain = 0.0f;
    float wind = 0.0f;
    float exposure = 1.0f;
    float storm = 0.0f;        // lightning frequency 0..1
};

// Day/night cycle of a level. Off: the sun stays where Scene::sunDirection points and
// the environment is used as authored. On: the sun and the moon follow their real
// path for the latitude and day of the year, and sky, light, fog and ambient follow
// the time (the authored values are the "noon on a clear day" look).
struct TimeOfDay {
    bool enabled = false;
    float hour = 12.0f;        // current time (the level's start time), 0..24
    float dayMinutes = 0.0f;   // real minutes per game day, 0 = time stands still
    float latitude = 50.0f;    // degrees north
    int dayOfYear = 172;       // 1..365 (172 = summer solstice)
    float north = 0.0f;        // degrees: rotates the compass (0 = north is -Z)
    float moonPhase = 0.5f;    // 0 new moon .. 0.5 full .. 1 new
    float nightBrightness = 1.0f; // scales moon light and night sky (0 = pitch black)
    float cloudShadows = 0.6f; // 0..1 how dark cloud shadows are on the ground
    std::vector<WeatherKey> keys; // empty = the authored weather all day
};

// Direction from the ground toward the sun / moon (y up) at `hour`.
glm::vec3 sunPosition(const TimeOfDay& t, float hour);
glm::vec3 moonPosition(const TimeOfDay& t, float hour);

// The lighting the renderer uses for `hour`: Environment::time off returns `base`
// unchanged with `sunDirection` = `authoredSun`. `lightDirection` receives the
// direction the main light travels (sun by day, moon at night), `moon` the
// direction toward the moon and `stars` how visible the stars are (0..1).
struct SkyState {
    glm::vec3 lightDirection{0.0f, -1.0f, 0.0f};
    glm::vec3 moon{0.0f, 1.0f, 0.0f};
    float moonLight = 0.0f;    // 0..1 brightness of the moon disk
    float stars = 0.0f;
    float twilight = 0.0f;     // 0..1 sunrise / sunset glow strength
    float night = 0.0f;        // 0 day .. 1 night
    float cloudShadows = 0.0f;
};
Environment environmentAt(const Environment& base, glm::vec3 authoredSun, SkyState* sky = nullptr);

// The weather keys interpolated at `hour` (keys must not be empty).
WeatherKey weatherAt(const std::vector<WeatherKey>& keys, float hour);

// "06:30" style text for an hour (wraps to 0..24).
const char* formatHour(float hour, char (&buffer)[8]);

} // namespace engine
