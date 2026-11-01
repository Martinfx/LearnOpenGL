#include "engine/Factions.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace engine {

namespace {

std::pair<std::string, std::string> key(std::string_view a, std::string_view b)
{
    return a < b ? std::make_pair(std::string(a), std::string(b)) : std::make_pair(std::string(b), std::string(a));
}

// Next token: a "quoted string" or a bare word; false at the end of the line.
bool token(std::istringstream& in, std::string& out)
{
    out.clear();
    in >> std::ws;
    if (in.peek() == '"') {
        in.get();
        std::getline(in, out, '"');
        return true;
    }
    return static_cast<bool>(in >> out);
}

std::string quoted(const std::string& s)
{
    std::string out = "\"";
    for (char c : s)
        if (c != '"' && c != '\n')
            out += c;
    return out + '"';
}

} // namespace

const char* attitudeName(Attitude a)
{
    switch (a) {
    case Attitude::Hostile: return "hostile";
    case Attitude::Friendly: return "friendly";
    default: return "neutral";
    }
}

const Faction* FactionTable::find(std::string_view id) const
{
    for (const Faction& f : factions)
        if (f.id == id)
            return &f;
    return nullptr;
}

float FactionTable::relation(std::string_view a, std::string_view b) const
{
    float bonus = 0.0f;
    if (a == "player" && b == "player")
        return 100.0f;
    if (a == "player" || b == "player") { // the player's faction plus the goodwill earned with the other one
        const std::string_view other = a == "player" ? b : a;
        if (const auto it = goodwill.find(std::string(other)); it != goodwill.end())
            bonus = it->second;
        a = playerFaction;
        b = other;
    }
    float base = 0.0f;
    if (const auto it = relations.find(key(a, b)); it != relations.end())
        base = it->second;
    else if (a == b && !a.empty())
        base = 100.0f;
    return std::clamp(base + bonus, -100.0f, 100.0f);
}

bool FactionTable::hasRelation(std::string_view a, std::string_view b) const { return relations.count(key(a, b)) > 0; }

void FactionTable::setRelation(std::string_view a, std::string_view b, float value)
{
    relations[key(a, b)] = std::clamp(value, -100.0f, 100.0f);
}

Attitude FactionTable::attitudeOf(float r) const
{
    if (r < hostileBelow)
        return Attitude::Hostile;
    if (r > friendlyAbove)
        return Attitude::Friendly;
    return Attitude::Neutral;
}

Attitude FactionTable::attitude(std::string_view a, std::string_view b) const { return attitudeOf(relation(a, b)); }

void FactionTable::changeGoodwill(std::string_view faction, float delta)
{
    if (faction.empty() || faction == "player")
        return;
    float& g = goodwill[std::string(faction)];
    float base = 0.0f;
    if (const auto it = relations.find(key(playerFaction, faction)); it != relations.end())
        base = it->second;
    else if (faction == playerFaction)
        base = 100.0f;
    g = std::clamp(g + delta, -100.0f - base, 100.0f - base);
}

FactionTable defaultFactions()
{
    FactionTable t;
    t.factions = {
        {"stalker", "Stalkeři", {0.45f, 0.7f, 0.35f}},
        {"bandit", "Bandité", {0.75f, 0.3f, 0.2f}},
        {"military", "Armáda", {0.35f, 0.5f, 0.75f}},
        {"mutant", "Mutanti", {0.6f, 0.45f, 0.6f}},
    };
    // Loners trust nobody much: neutral to each other (and to the player) until shot at.
    t.setRelation("stalker", "stalker", 20.0f);
    t.setRelation("stalker", "bandit", -80.0f);
    t.setRelation("stalker", "military", -50.0f);
    t.setRelation("bandit", "military", -100.0f);
    for (const char* f : {"stalker", "bandit", "military"})
        t.setRelation("mutant", f, -100.0f);
    return t;
}

bool loadFactions(FactionTable& table, const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        if (error)
            *error = "cannot open " + file.string();
        return false;
    }
    FactionTable t;
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::string& what) {
        if (error)
            *error = file.filename().string() + ":" + std::to_string(lineNo) + ": " + what;
        return false;
    };
    while (std::getline(in, line)) {
        ++lineNo;
        if (const std::size_t hash = line.find('#'); hash != std::string::npos && line.find('"') > hash)
            line.erase(hash);
        std::istringstream s(line);
        std::string word;
        if (!(s >> word))
            continue;
        if (word == "player") {
            if (!token(s, t.playerFaction))
                return fail("player needs a faction");
        } else if (word == "thresholds") {
            if (!(s >> t.hostileBelow >> t.friendlyAbove))
                return fail("thresholds needs two numbers");
        } else if (word == "faction") {
            Faction f;
            if (!token(s, f.id) || f.id.empty())
                return fail("faction needs an id");
            if (!token(s, f.name))
                f.name = f.id;
            s >> f.color.x >> f.color.y >> f.color.z;
            if (t.find(f.id))
                return fail("faction " + f.id + " twice");
            t.factions.push_back(std::move(f));
        } else if (word == "relation") {
            std::string a, b;
            float v = 0.0f;
            if (!token(s, a) || !token(s, b) || !(s >> v))
                return fail("relation needs two factions and a value");
            t.setRelation(a, b, v);
        } else {
            return fail("unknown line '" + word + "'");
        }
    }
    if (t.hostileBelow > t.friendlyAbove)
        std::swap(t.hostileBelow, t.friendlyAbove);
    t.goodwill = std::move(table.goodwill); // play state stays
    table = std::move(t);
    return true;
}

bool saveFactions(const FactionTable& table, const std::filesystem::path& file, std::string* error)
{
    std::ostringstream out;
    out << "# Factions and their relations (-100 hate .. 100 allies); edited in the editor: Assets > Factions.\n";
    out << "player " << quoted(table.playerFaction) << "\n";
    char buf[160];
    std::snprintf(buf, sizeof buf, "thresholds %g %g\n", table.hostileBelow, table.friendlyAbove);
    out << buf;
    for (const Faction& f : table.factions) {
        std::snprintf(buf, sizeof buf, " %.3g %.3g %.3g\n", f.color.x, f.color.y, f.color.z);
        out << "faction " << quoted(f.id) << ' ' << quoted(f.name) << buf;
    }
    for (const auto& [k, v] : table.relations) {
        std::snprintf(buf, sizeof buf, " %g\n", v);
        out << "relation " << quoted(k.first) << ' ' << quoted(k.second) << buf;
    }
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f || !(f << out.str())) {
        if (error)
            *error = "cannot write " + file.string();
        return false;
    }
    return true;
}

} // namespace engine

#include "engine/Scene.h"

namespace engine {

std::string entityFaction(const Entity& e)
{
    if (const auto it = e.properties.find("faction"); it != e.properties.end() && !it->second.empty())
        return it->second;
    if (e.type == "enemy")
        return "bandit";
    if (e.type == "npc")
        return "stalker";
    if (e.type == "mutant")
        return "mutant";
    return {};
}

} // namespace engine
