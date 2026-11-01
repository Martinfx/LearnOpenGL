#include "engine/Ambience.h"
#include "engine/Vfs.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace engine {

namespace {

std::filesystem::path pathFromUtf8(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string pathToUtf8(const std::filesystem::path& p)
{
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

void setError(std::string* error, const std::string& text)
{
    if (error)
        *error = text;
}

std::filesystem::path resolve(const std::filesystem::path& file, const std::string& text)
{
    std::filesystem::path p = pathFromUtf8(text);
    if (p.is_relative())
        p = std::filesystem::absolute(file).parent_path() / p;
    return p.lexically_normal();
}

std::string relativeTo(const std::filesystem::path& file, const std::filesystem::path& p)
{
    const std::filesystem::path rel = std::filesystem::absolute(p).lexically_relative(std::filesystem::absolute(file).parent_path());
    return pathToUtf8(rel.empty() ? p : rel);
}

} // namespace

bool loadAmbienceFile(AmbienceSet& set, const std::filesystem::path& file, std::string* error)
{
    engine::VfsStream in(file);
    if (!in) {
        setError(error, "cannot open " + file.string());
        return false;
    }
    AmbienceSet result;
    int lineNo = 0;
    for (std::string line; std::getline(in, line);) {
        ++lineNo;
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#')
            continue;
        std::string path;
        bool ok = false;
        if (key == "layer") {
            AmbienceLayer l;
            ok = static_cast<bool>(ls >> std::quoted(path) >> l.volume);
            ls >> l.day >> l.night; // optional
            l.sound = resolve(file, path);
            if (ok)
                result.layers.push_back(l);
        } else if (key == "random") {
            AmbienceRandom r;
            ok = static_cast<bool>(ls >> std::quoted(path) >> r.minInterval >> r.maxInterval >> r.minDistance >> r.maxDistance >> r.day >>
                                   r.night);
            if (ok) {
                ls >> r.height >> r.duck; // optional
                r.sound = resolve(file, path);
                r.maxInterval = std::max(r.minInterval, r.maxInterval);
                r.maxDistance = std::max(r.minDistance, r.maxDistance);
                result.randoms.push_back(r);
            }
        }
        if (!ok) {
            setError(error, file.filename().string() + ":" + std::to_string(lineNo) + ": invalid line: " + line);
            return false;
        }
    }
    set = std::move(result);
    return true;
}

bool saveAmbienceFile(const AmbienceSet& set, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file, std::ios::trunc);
    if (!out) {
        setError(error, "cannot write " + file.string());
        return false;
    }
    out << "# Ambience set: looping layers and random sounds around the listener (paths relative to this file)\n";
    out << "# layer \"sound\" volume day night\n";
    out << "# random \"sound\" min_interval max_interval min_distance max_distance day_chance night_chance height duck\n";
    for (const AmbienceLayer& l : set.layers)
        out << "layer " << std::quoted(relativeTo(file, l.sound)) << ' ' << l.volume << ' ' << l.day << ' ' << l.night << '\n';
    for (const AmbienceRandom& r : set.randoms)
        out << "random " << std::quoted(relativeTo(file, r.sound)) << ' ' << r.minInterval << ' ' << r.maxInterval << ' ' << r.minDistance
            << ' ' << r.maxDistance << ' ' << r.day << ' ' << r.night << ' ' << r.height << ' ' << r.duck << '\n';
    if (!out) {
        setError(error, "write failed: " + file.string());
        return false;
    }
    return true;
}

float nightFactor(glm::vec3 sunDirection)
{
    if (glm::dot(sunDirection, sunDirection) < 1e-8f)
        return 0.0f;
    const float elevation = -glm::normalize(sunDirection).y; // sin of the sun's height above the horizon
    const float t = std::clamp((elevation + 0.1f) / 0.2f, 0.0f, 1.0f); // -6 deg .. +6 deg
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

float ambienceLayerVolume(const AmbienceLayer& l, float night)
{
    return std::max(0.0f, l.volume * (l.day + (l.night - l.day) * std::clamp(night, 0.0f, 1.0f)));
}

float ambienceChance(const AmbienceRandom& r, float night)
{
    return std::clamp(r.day + (r.night - r.day) * std::clamp(night, 0.0f, 1.0f), 0.0f, 1.0f);
}

} // namespace engine
