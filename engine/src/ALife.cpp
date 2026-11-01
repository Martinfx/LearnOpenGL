#include "engine/ALife.h"
#include "engine/Factions.h"
#include "engine/Scene.h"
#include "engine/Scripting.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>

namespace engine {

namespace {

float flatDistance(glm::vec3 a, glm::vec3 b) { return std::hypot(a.x - b.x, a.z - b.z); }

std::string prop(const Entity& e, const char* key, const char* fallback = "")
{
    const auto it = e.properties.find(key);
    return it != e.properties.end() ? it->second : std::string(fallback);
}

std::string vecText(glm::vec3 p)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.2f %.2f %.2f", p.x, p.y, p.z);
    return buf;
}

} // namespace

void ALife::start(Scene& scene, std::uint32_t seed)
{
    rng_.seed(seed);
    points_.clear();
    squads_.clear();
    events_.clear();
    tick_ = 0.0f;
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const Entity& e = scene.entities[i];
        if (e.type != "alife_point")
            continue;
        Point p;
        p.entity = static_cast<int>(i);
        p.name = e.name;
        p.roles = prop(e, "roles", "camp");
        p.position = e.position;
        p.capacity = std::max(1, std::atoi(prop(e, "capacity", "1").c_str()));
        std::stringstream list(prop(e, "factions"));
        for (std::string f; std::getline(list, f, ',');) {
            f.erase(0, f.find_first_not_of(" \t"));
            f.erase(f.find_last_not_of(" \t") + 1);
            if (!f.empty())
                p.factions.push_back(f);
        }
        points_.push_back(std::move(p));
    }
    std::map<std::string, std::size_t> byGroup;
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        Entity& e = scene.entities[i];
        e.offline = false;
        if (e.helper() || e.scriptPath.empty() || entityFaction(e).empty())
            continue; // not a character
        const std::string group = prop(e, "group");
        std::size_t index;
        if (const auto it = byGroup.find(group); !group.empty() && it != byGroup.end()) {
            index = it->second;
        } else {
            Squad s;
            s.name = group.empty() ? e.name : group;
            s.faction = entityFaction(e);
            index = squads_.size();
            squads_.push_back(std::move(s));
            if (!group.empty())
                byGroup[group] = index;
        }
        Squad& s = squads_[index];
        s.members.push_back(static_cast<int>(i));
        s.roams = s.roams || prop(e, "alife") == "roam";
    }
    std::uniform_real_distribution<float> wait(settings.minWait, settings.maxWait);
    for (Squad& s : squads_) {
        glm::vec3 c(0.0f);
        for (int m : s.members)
            c += scene.entities[static_cast<std::size_t>(m)].position;
        s.position = c / static_cast<float>(s.members.size());
        s.living = static_cast<int>(s.members.size());
        // It starts at the nearest point it may use (its home), if one is close.
        float best = 40.0f;
        for (std::size_t p = 0; p < points_.size(); ++p) {
            const Point& pt = points_[p];
            const bool allowed = pt.factions.empty() || std::find(pt.factions.begin(), pt.factions.end(), s.faction) != pt.factions.end();
            if (allowed && flatDistance(pt.position, s.position) < best) {
                best = flatDistance(pt.position, s.position);
                s.target = static_cast<int>(p);
            }
        }
        s.arrived = true;
        s.wait = wait(rng_) * 0.5f;
    }
}

int ALife::pickTarget(const Squad& s)
{
    std::vector<int> occupancy(points_.size(), 0);
    for (const Squad& o : squads_)
        if (&o != &s && o.living > 0 && o.target >= 0)
            ++occupancy[static_cast<std::size_t>(o.target)];
    std::vector<double> weights(points_.size(), 0.0);
    double total = 0.0;
    for (std::size_t p = 0; p < points_.size(); ++p) {
        const Point& pt = points_[p];
        if (static_cast<int>(p) == s.target || occupancy[p] >= pt.capacity)
            continue;
        if (!pt.factions.empty() && std::find(pt.factions.begin(), pt.factions.end(), s.faction) == pt.factions.end())
            continue;
        weights[p] = 1.0 / (1.0 + flatDistance(pt.position, s.position) / 150.0); // nearer points more often
        total += weights[p];
    }
    if (total <= 0.0)
        return s.target; // nowhere else to go: stays
    std::uniform_real_distribution<double> pick(0.0, total);
    double r = pick(rng_);
    for (std::size_t p = 0; p < weights.size(); ++p) {
        r -= weights[p];
        if (weights[p] > 0.0 && r <= 0.0)
            return static_cast<int>(p);
    }
    return s.target;
}

void ALife::goOffline(Scene& scene, Squad& s)
{
    s.online = false;
    s.offsets.clear();
    for (int m : s.members) {
        Entity& e = scene.entities[static_cast<std::size_t>(m)];
        s.offsets.push_back(e.position - s.position);
        e.offline = true;
    }
    events_.push_back({Event::Offline, s.position, static_cast<int>(&s - squads_.data()), -1});
}

void ALife::goOnline(Scene& scene, ScriptSystem& scripts, Squad& s, const Snap& snap)
{
    s.online = true;
    for (std::size_t k = 0; k < s.members.size(); ++k) {
        const int m = s.members[k];
        Entity& e = scene.entities[static_cast<std::size_t>(m)];
        glm::vec3 p = s.position + (k < s.offsets.size() ? s.offsets[k] : glm::vec3(0.0f));
        if (snap)
            if (const auto onMesh = snap(p, 3.0f))
                p = *onMesh;
        scripts.teleportEntity(m, p); // also its capsule, left where it went offline
        e.offline = false;
        // A roaming squad walks on to its point (or makes the point it reached its home).
        if (s.roams && e.alive && s.target >= 0)
            scripts.sendInput(e.name, "GoTo", vecText(points_[static_cast<std::size_t>(s.target)].position));
    }
    events_.push_back({Event::Online, s.position, static_cast<int>(&s - squads_.data()), -1});
}

void ALife::update(Scene& scene, ScriptSystem& scripts, const FactionTable& factions, glm::vec3 player, float dt, const Snap& snap)
{
    tick_ += dt;
    if (tick_ < 1.0f)
        return; // once a second is plenty
    const float step = tick_;
    tick_ = 0.0f;
    std::uniform_real_distribution<float> wait(settings.minWait, settings.maxWait);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    for (Squad& s : squads_) {
        s.living = 0;
        glm::vec3 c(0.0f);
        for (int m : s.members) {
            const Entity& e = scene.entities[static_cast<std::size_t>(m)];
            if (e.alive) {
                ++s.living;
                c += e.position;
            }
        }
        if (s.online && s.living > 0)
            s.position = c / static_cast<float>(s.living);
        const float d = flatDistance(s.position, player);
        if (s.online && d > settings.offlineRadius)
            goOffline(scene, s);
        else if (!s.online && d < settings.onlineRadius)
            goOnline(scene, scripts, s, snap);

        if (!s.roams || s.living == 0) // a squad wiped out offline still comes online: its dead lie there
            continue;
        if (s.arrived) {
            s.wait -= step;
            if (s.wait <= 0.0f) {
                const int next = pickTarget(s);
                s.wait = wait(rng_);
                if (next >= 0 && next != s.target) {
                    s.target = next;
                    s.arrived = false;
                    if (s.online)
                        for (int m : s.members)
                            if (scene.entities[static_cast<std::size_t>(m)].alive)
                                scripts.sendInput(scene.entities[static_cast<std::size_t>(m)].name, "GoTo",
                                                  vecText(points_[static_cast<std::size_t>(next)].position));
                }
            }
        } else if (s.target >= 0) {
            const glm::vec3 goal = points_[static_cast<std::size_t>(s.target)].position;
            if (!s.online) {
                glm::vec3 to = goal - s.position;
                to.y = 0.0f;
                const float len = std::sqrt(to.x * to.x + to.z * to.z);
                const float move = settings.travelSpeed * step;
                s.position = len <= move ? glm::vec3(goal.x, s.position.y, goal.z) : s.position + to * (move / len);
                s.position.y = goal.y; // records do not climb; online placement snaps to the ground
            }
            if (flatDistance(s.position, goal) < (s.online ? 6.0f : 1.0f)) {
                s.arrived = true;
                s.wait = wait(rng_);
            }
        }
        if (!s.online) // the records stand where the squad is (saved games, the map)
            for (std::size_t k = 0; k < s.members.size(); ++k) {
                Entity& e = scene.entities[static_cast<std::size_t>(s.members[k])];
                if (e.alive)
                    e.position = s.position + (k < s.offsets.size() ? s.offsets[k] : glm::vec3(0.0f));
            }
    }

    // Offline squads of hostile factions that meet fight: each side may lose a member a second.
    for (std::size_t a = 0; a < squads_.size(); ++a)
        for (std::size_t b = a + 1; b < squads_.size(); ++b) {
            Squad& x = squads_[a];
            Squad& y = squads_[b];
            if (x.online || y.online || x.living == 0 || y.living == 0)
                continue;
            if (flatDistance(x.position, y.position) > settings.fightRange || factions.attitude(x.faction, y.faction) != Attitude::Hostile)
                continue;
            events_.push_back({Event::Gunfire, (x.position + y.position) * 0.5f + glm::vec3(0.0f, 1.5f, 0.0f), static_cast<int>(a), -1});
            const int xLiving = x.living, yLiving = y.living;
            for (Squad* victim : {&x, &y}) {
                const int shooters = victim == &x ? yLiving : xLiving;
                if (chance(rng_) >= settings.killChance * static_cast<float>(shooters) * step)
                    continue;
                std::vector<int> alive;
                for (int m : victim->members)
                    if (scene.entities[static_cast<std::size_t>(m)].alive)
                        alive.push_back(m);
                if (alive.empty())
                    continue;
                const int dead = alive[std::uniform_int_distribution<std::size_t>(0, alive.size() - 1)(rng_)];
                scripts.damageEntity(dead, 1e6f, -2);
                --victim->living;
                events_.push_back({Event::Death, scene.entities[static_cast<std::size_t>(dead)].position,
                                   static_cast<int>(victim - squads_.data()), dead});
            }
        }
}

std::vector<ALife::Event> ALife::takeEvents()
{
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

std::vector<ALife::SquadState> ALife::save() const
{
    std::vector<SquadState> out;
    for (const Squad& s : squads_)
        if (s.roams)
            out.push_back({s.name, s.position, s.target, s.arrived, s.wait});
    return out;
}

void ALife::restore(const std::vector<SquadState>& states)
{
    for (const SquadState& st : states)
        for (Squad& s : squads_)
            if (s.name == st.name) {
                s.position = st.position;
                s.target = st.target < static_cast<int>(points_.size()) ? st.target : -1;
                s.arrived = st.arrived;
                s.wait = st.wait;
            }
}

} // namespace engine
