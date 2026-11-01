#pragma once
#include <glm/glm.hpp>

#include <random>
#include <string>
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

// A kind of weather the level can change into (clear, fog, rain, storm...).
// Values replace the environment's (exposure multiplies it).
struct WeatherPreset {
    std::string name = "Clear";
    float cloudCover = 0.2f;
    float fogDensity = 0.004f;
    float rain = 0.0f;
    float wind = 0.1f;
    float storm = 0.0f;
    float exposure = 1.0f;     // multiplier
    float chance = 1.0f;       // relative weight when the cycle picks the next weather
    bool operator==(const WeatherPreset&) const = default;
};

// The built-in kinds: Clear, Cloudy, Overcast, Fog, Rain, Storm.
std::vector<WeatherPreset> defaultWeatherPresets();

// Weather that changes over time (level line `weather_cycle`, `weather_preset`):
// each kind lasts minMinutes..maxMinutes of real time, then blends into the next
// one, chosen by chance. Scripts (game.set_weather) can force a change.
struct WeatherCycle {
    bool enabled = false;
    float minMinutes = 4.0f;
    float maxMinutes = 10.0f;
    float transitionMinutes = 1.5f;
    std::string start;                 // kind at the level start, empty = random
    std::vector<WeatherPreset> presets; // empty = defaultWeatherPresets()
    bool operator==(const WeatherCycle&) const = default;
};

// Runtime: what the cycle (or a script) applies now. Not saved with the level.
struct WeatherNow {
    WeatherPreset state;       // blended values
    float amount = 0.0f;       // 0 = the authored / hourly weather, 1 = `state`
    std::string current;       // kind now (the target while a change runs)
    std::string request;       // game.set_weather: change to this kind
    float requestSeconds = -1.0f; // transition length of the request, < 0 = the cycle's
};

// Drives WeatherNow from the cycle: call update once per frame (game) or when previewing (editor).
class WeatherDirector {
public:
    explicit WeatherDirector(unsigned seed = std::random_device{}()) : rng_(seed) {}
    void start(Environment& env);                     // level start: picks the first kind
    void update(Environment& env, float dt);
    // Changes to `name` over `seconds` (0 = at once). False if the level has no such kind.
    bool change(Environment& env, const std::string& name, float seconds);
    float holdSeconds() const { return hold_; }       // until the cycle picks the next kind
    void setHoldSeconds(float s) { hold_ = s; }        // saved games
    bool changing() const { return blend_ < 1.0f; }

private:
    std::mt19937 rng_;
    WeatherPreset from_;
    int to_ = -1;
    float blend_ = 1.0f, blendSeconds_ = 1.0f, hold_ = 0.0f;
    bool forced_ = false; // a script chose the weather: stays without the cycle
    float randomHold(const WeatherCycle& c);
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
