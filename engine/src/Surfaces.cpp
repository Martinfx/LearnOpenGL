#include "engine/Surfaces.h"

#include "engine/Material.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace engine {

namespace {

void setError(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string toUtf8(const std::filesystem::path& p)
{
    const auto u8 = p.generic_u8string();
    return std::string(u8.begin(), u8.end());
}

} // namespace

const SurfaceType* SurfaceTable::find(std::string_view name) const
{
    for (const SurfaceType& t : types)
        if (t.name == name)
            return &t;
    for (const SurfaceType& t : types)
        if (t.name == fallback)
            return &t;
    return nullptr;
}

const std::vector<std::string>& standardSurfaceNames()
{
    static const std::vector<std::string> names = {"concrete", "metal", "wood", "dirt", "grass", "gravel", "water", "glass", "flesh"};
    return names;
}

std::string guessSurface(std::string_view text)
{
    const std::string s = lower(text);
    if (s.empty())
        return {};
    // First match wins: specific words before broad ones ("metal_plate" before "plate").
    static const std::vector<std::pair<const char*, const char*>> words = {
        {"water", "water"},     {"puddle", "water"},   {"glass", "glass"},    {"window", "glass"},
        {"flesh", "flesh"},     {"skin", "flesh"},     {"body", "flesh"},     {"metal", "metal"},
        {"steel", "metal"},     {"iron", "metal"},     {"rust", "metal"},     {"corrugated", "metal"},
        {"pipe", "metal"},      {"tank", "metal"},     {"car", "metal"},      {"vehicle", "metal"},
        {"container", "metal"}, {"rail", "metal"},     {"barrel", "metal"},   {"fence", "metal"},
        {"wood", "wood"},       {"plank", "wood"},     {"timber", "wood"},    {"crate", "wood"},
        {"bark", "wood"},       {"tree", "wood"},      {"trunk", "wood"},     {"log", "wood"},
        {"gravel", "gravel"},   {"rubble", "gravel"},  {"pebble", "gravel"},  {"ballast", "gravel"},
        {"grass", "grass"},     {"moss", "grass"},     {"leaves", "grass"},   {"leaf", "grass"},
        {"forest_floor", "grass"}, {"dirt", "dirt"},   {"mud", "dirt"},       {"soil", "dirt"},
        {"ground", "dirt"},     {"sand", "dirt"},      {"earth", "dirt"},     {"concrete", "concrete"},
        {"brick", "concrete"},  {"asphalt", "concrete"}, {"stone", "concrete"}, {"rock", "concrete"},
        {"tile", "concrete"},   {"plaster", "concrete"}, {"cement", "concrete"}, {"wall", "concrete"},
        {"floor", "concrete"},  {"road", "concrete"},
    };
    for (const auto& [word, surface] : words)
        if (s.find(word) != std::string::npos)
            return surface;
    return {};
}

std::string surfaceOf(const Material* material, std::string_view fallbackText)
{
    if (material) {
        if (!material->surface.empty())
            return material->surface;
        if (std::string g = guessSurface(material->name); !g.empty())
            return g;
        if (std::string g = guessSurface(material->sourcePath.stem().string()); !g.empty())
            return g;
        for (const auto& p : material->mapPaths)
            if (!p.empty())
                if (std::string g = guessSurface(p.stem().string()); !g.empty())
                    return g;
    }
    return guessSurface(fallbackText);
}

bool loadSurfaces(SurfaceTable& table, const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        setError(error, "Cannot open " + file.string());
        return false;
    }
    SurfaceTable result;
    const std::filesystem::path dir = std::filesystem::absolute(file).parent_path();
    auto path = [&](const std::string& s) {
        if (s.empty())
            return std::filesystem::path();
        std::filesystem::path p(std::u8string(s.begin(), s.end()));
        return (p.is_relative() ? dir / p : p).lexically_normal();
    };
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#')
            continue;
        if (key == "default") {
            ls >> result.fallback;
            continue;
        }
        if (key != "surface") {
            continue; // unknown keys: newer file
        }
        SurfaceType t;
        if (!(ls >> t.name)) {
            setError(error, file.string() + ":" + std::to_string(lineNo) + ": surface without a name");
            return false;
        }
        std::string k;
        while (ls >> k) {
            std::string value;
            if (k == "step" && ls >> std::quoted(value))
                t.footstep = path(value);
            else if (k == "impact" && ls >> std::quoted(value))
                t.impact = path(value);
            else if (k == "decal" && ls >> std::quoted(value))
                t.decal = path(value);
            else if (k == "casing" && ls >> std::quoted(value))
                t.casing = path(value);
            else if (k == "effect" && ls >> std::quoted(value))
                t.effect = path(value);
            else if (k == "decal_grid")
                ls >> t.decalColumns >> t.decalRows;
            else if (k == "decal_row")
                ls >> t.decalRow;
            else if (k == "decal_size")
                ls >> t.decalSize;
            else if (k == "step_volume")
                ls >> t.stepVolume;
        }
        result.types.push_back(std::move(t));
    }
    table = std::move(result);
    return true;
}

bool saveSurfaces(const SurfaceTable& table, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        setError(error, "Cannot write " + file.string());
        return false;
    }
    const std::filesystem::path dir = std::filesystem::absolute(file).parent_path();
    auto rel = [&](const std::filesystem::path& p) {
        const std::filesystem::path r = std::filesystem::absolute(p).lexically_relative(dir);
        return toUtf8(r.empty() ? p : r);
    };
    out << "# Surface types: sounds and bullet holes per material surface (editor: Tools > Surfaces)\n";
    out << "default " << table.fallback << '\n';
    for (const SurfaceType& t : table.types) {
        out << "surface " << t.name;
        if (!t.footstep.empty())
            out << " step " << std::quoted(rel(t.footstep));
        if (!t.impact.empty())
            out << " impact " << std::quoted(rel(t.impact));
        if (!t.casing.empty())
            out << " casing " << std::quoted(rel(t.casing));
        if (!t.effect.empty())
            out << " effect " << std::quoted(rel(t.effect));
        if (!t.decal.empty())
            out << " decal " << std::quoted(rel(t.decal)) << " decal_grid " << t.decalColumns << ' ' << t.decalRows
                << " decal_row " << t.decalRow;
        out << " decal_size " << t.decalSize << " step_volume " << t.stepVolume << '\n';
    }
    if (!out) {
        setError(error, "Cannot write " + file.string());
        return false;
    }
    return true;
}

} // namespace engine
