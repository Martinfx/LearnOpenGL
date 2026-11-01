#include "engine/Log.h"
#include "engine/Scripting.h"
#include "engine/CVars.h"
#include "engine/DebugDraw.h"
#include "engine/EntityIO.h"
#include "engine/CharacterBody.h"
#include "engine/NavMesh.h"
#include "engine/Physics.h"
#include "engine/PlayerController.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <optional>
#include <vector>

namespace engine {

namespace {

// Handle given to Lua as `self`. Refers to the entity by index, so it stays
// valid while the scene's entity vector is not resized during play.
struct ScriptEntity {
    ScriptSystem::LogFn* log = nullptr;
    Scene* scene = nullptr;
    PhysicsWorld* physics = nullptr;
    std::size_t index = 0;
    std::optional<CharacterBody> body;
    glm::vec3 desiredVelocity{0.0f};
    float jumpRequest = 0.0f;
    // Outside forces for the next step (ScriptSystem::pushEntity).
    glm::vec3 impulse{0.0f};
    glm::vec3 drift{0.0f};
    float slow = 0.0f;
    sol::environment env;
    sol::protected_function onUpdate;
    sol::protected_function onDamage;
    sol::protected_function onDeath;
    sol::protected_function onInput;
    bool failed = false;
    // Entity I/O
    bool touching = false;            // trigger: something allowed by its filter is inside
    bool propertiesChanged = false;   // an input changed its properties (saved games keep them)
    std::vector<char> fired;          // per output connection: a `once` one already fired
    // move_to(): the path being followed (navmesh corners).
    const NavMesh* const* nav = nullptr;
    const double* time = nullptr;
    std::vector<glm::vec3> path;
    std::size_t corner = 0;
    glm::vec3 pathTarget{0.0f};
    double pathTime = -1e9;

    Entity& e() const { return scene->entities[index]; }

    void steer(glm::vec3 to, float speed)
    {
        glm::vec3 d = to - e().position;
        d.y = 0.0f;
        desiredVelocity = glm::dot(d, d) > 1e-8f ? glm::normalize(d) * speed : glm::vec3(0.0f);
        if (glm::dot(d, d) > 1e-6f)
            e().yawDegrees = glm::degrees(std::atan2(d.z, d.x));
    }

    // Walks toward `target` around obstacles; true once there (or as close as the mesh gets).
    bool moveTo(glm::vec3 target, float speed)
    {
        const glm::vec3 pos = e().position;
        if (const CVar* dbg = CVarRegistry::global().find("ai_debug"); dbg && dbg->boolean() && corner < path.size()) {
            glm::vec3 from = pos + glm::vec3(0.0f, 0.1f, 0.0f);
            for (std::size_t i = corner; i < path.size(); ++i) { // the navmesh path being followed
                const glm::vec3 to = path[i] + glm::vec3(0.0f, 0.1f, 0.0f);
                DebugDraw::global().line(from, to, {0.2f, 0.9f, 1.0f, 1.0f});
                from = to;
            }
            DebugDraw::global().cross(target, 0.4f, {0.2f, 0.9f, 1.0f, 1.0f});
        }
        auto flatDist = [](glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); };
        if (flatDist(pos, target) < 0.3f) {
            desiredVelocity = glm::vec3(0.0f);
            path.clear();
            return true;
        }
        const NavMesh* mesh = nav ? *nav : nullptr;
        if (!mesh || !mesh->ready()) { // no navmesh (yet): straight line
            steer(target, speed);
            return false;
        }
        // New path when the target moved, every second (others block the way), or when done.
        if (path.empty() || corner >= path.size() || flatDist(target, pathTarget) > 1.0f || *time - pathTime > 1.0) {
            path = mesh->findPath(pos, target);
            corner = 1;
            pathTarget = target;
            pathTime = *time;
            if (path.size() < 2) {
                path.clear();
                steer(target, speed);
                return false;
            }
        }
        while (corner < path.size() && flatDist(pos, path[corner]) < 0.35f)
            ++corner;
        if (corner >= path.size()) {
            desiredVelocity = glm::vec3(0.0f);
            path.clear();
            return true; // end of a partial path: as close as it gets
        }
        steer(path[corner], speed);
        return false;
    }
};

} // namespace

// Builds `props` from the script's `properties` declaration and the values set in the editor.
void buildProps(sol::state& lua, sol::environment& env, const Entity& e)
{
    sol::table props = lua.create_table();
    sol::object decl = env["properties"];
    if (decl.is<sol::table>()) {
        for (auto& [k, v] : decl.as<sol::table>()) {
            if (!v.is<sol::table>())
                continue;
            sol::table row = v.as<sol::table>();
            sol::object name = row[1];
            sol::object def = row[2];
            if (!name.is<std::string>())
                continue;
            const std::string key = name.as<std::string>();
            auto it = e.properties.find(key);
            if (it == e.properties.end()) {
                props[key] = def;
            } else if (def.get_type() == sol::type::number) {
                try {
                    props[key] = std::stod(it->second);
                } catch (...) {
                    props[key] = def;
                }
            } else if (def.get_type() == sol::type::boolean) {
                props[key] = it->second == "true" || it->second == "1";
            } else {
                props[key] = it->second;
            }
        }
    }
    // Values without a declaration are passed through as strings.
    for (const auto& [key, value] : e.properties)
        if (props[key].get_type() == sol::type::lua_nil)
            props[key] = value;
    env["props"] = props;
}

struct ScriptSystem::Impl {
    Scene& scene;
    PhysicsWorld& physics;
    PlayerController& player;
    LogFn log;
    SoundFn playSound;
    const NavMesh* nav = nullptr;
    sol::state lua;
    std::vector<std::unique_ptr<ScriptEntity>> entities;
    float playerHealth = 100.0f;
    std::string levelChange; // game.change_level()
    MessageFn message;
    DuckFn duck;
    struct Queued {
        double at = 0.0;
        std::uint64_t order = 0; // same time: in the order they were sent
        std::string target, input, parameter;
        int caller = -1, activator = -1;
    };
    std::vector<Queued> queue;
    std::uint64_t queueOrder = 0;
    double time = 0.0;
    int errors = 0;

    Impl(Scene& s, PhysicsWorld& p, PlayerController& pl, LogFn l)
        : scene(s), physics(p), player(pl), log(std::move(l))
    {
        if (!log)
            log = [](const std::string& m) {
                // "[script] name: error" lines are script errors, the rest is game.log / print.
                logMessage(m.rfind("[script]", 0) == 0 ? LogLevel::Error : LogLevel::Info, "script", m);
            };
    }

    void report(const ScriptEntity& se, const std::string& what)
    {
        ++errors;
        log("[script] " + se.e().name + ": " + what);
    }

    void bindApi();

    // Entity I/O
    void fireOutput(int index, const std::string& output, int activator);
    void sendInput(const std::string& target, const std::string& input, const std::string& parameter, float delay, int activator,
                   int caller);
    void deliver(const Queued& q);
    void applyInput(int index, const std::string& input, const std::string& parameter, int activator);
    void updateTriggers();
    void kill(ScriptEntity& se, bool notify);
    void callScript(ScriptEntity& se, sol::protected_function& fn, const char* what, auto&&... args)
    {
        if (se.failed || !fn.valid())
            return;
        auto r = fn(&se, std::forward<decltype(args)>(args)...);
        if (!r.valid()) {
            sol::error err = r;
            report(se, std::string(what) + ": " + err.what());
            se.failed = true;
        }
    }
    bool ioDebug() const
    {
        const CVar* c = CVarRegistry::global().find("io_debug");
        return c && c->boolean();
    }
};

void ScriptSystem::Impl::fireOutput(int index, const std::string& output, int activator)
{
    if (index < 0 || index >= static_cast<int>(entities.size()))
        return;
    ScriptEntity& se = *entities[static_cast<std::size_t>(index)];
    const Entity& e = se.e();
    se.fired.resize(e.outputs.size(), 0);
    for (std::size_t i = 0; i < e.outputs.size(); ++i) {
        const EntityOutput& o = e.outputs[i];
        if (o.output != output || (o.once && se.fired[i]))
            continue;
        se.fired[i] = 1;
        std::string target = o.target;
        if (target == "!self")
            target = e.name;
        if (target == "!activator") {
            if (activator >= 0)
                target = entities[static_cast<std::size_t>(activator)]->e().name;
            else
                target = "!player";
        }
        if (ioDebug())
            log("[io] " + e.name + "." + output + " -> " + target + "." + o.input + (o.parameter.empty() ? "" : "(" + o.parameter + ")"));
        sendInput(target, o.input, o.parameter, o.delay, activator, index);
    }
}

void ScriptSystem::Impl::sendInput(const std::string& target, const std::string& input, const std::string& parameter, float delay,
                                   int activator, int caller)
{
    Queued q;
    q.at = time + std::max(0.0f, delay);
    q.order = queueOrder++;
    q.target = target;
    q.input = input;
    q.parameter = parameter;
    q.activator = activator;
    q.caller = caller;
    queue.push_back(std::move(q));
}

void ScriptSystem::Impl::deliver(const Queued& q)
{
    if (q.target == "!player") {
        if (q.input == "Damage")
            playerHealth = std::max(0.0f, playerHealth - (q.parameter.empty() ? 100.0f : std::strtof(q.parameter.c_str(), nullptr)));
        else if (q.input == "Heal")
            playerHealth = std::min(100.0f, playerHealth + (q.parameter.empty() ? 100.0f : std::strtof(q.parameter.c_str(), nullptr)));
        else if (q.input == "ShowMessage" && message)
            message(q.parameter);
        return;
    }
    const std::string target = q.target == "!self" && q.caller >= 0 ? entities[static_cast<std::size_t>(q.caller)]->e().name : q.target;
    const std::vector<int> found = findEntitiesByName(scene, target);
    if (found.empty())
        log("[script] I/O: no entity named '" + target + "' for input " + q.input);
    for (int i : found)
        applyInput(i, q.input, q.parameter, q.activator);
}

void ScriptSystem::Impl::kill(ScriptEntity& se, bool notify)
{
    Entity& e = se.e();
    if (!e.alive)
        return;
    e.alive = false;
    if (notify)
        callScript(se, se.onDeath, "on_death");
    se.body.reset();
}

void ScriptSystem::Impl::applyInput(int index, const std::string& input, const std::string& parameter, int activator)
{
    ScriptEntity& se = *entities[static_cast<std::size_t>(index)];
    Entity& e = se.e();
    if (ioDebug())
        log("[io] " + e.name + " <- " + input + (parameter.empty() ? "" : "(" + parameter + ")"));
    auto setEnabled = [&](bool on) {
        e.properties["enabled"] = on ? "1" : "0";
        se.propertiesChanged = true;
    };
    bool known = true;
    if (input == "Enable" || input == "TurnOn")
        setEnabled(true);
    else if (input == "Disable" || input == "TurnOff")
        setEnabled(false);
    else if (input == "Toggle")
        setEnabled(!entityEnabled(e));
    else if (input == "Kill")
        kill(se, false);
    else if (input == "Damage") {
        const float amount = parameter.empty() ? 100.0f : std::strtof(parameter.c_str(), nullptr);
        if (e.alive) {
            e.health -= amount;
            callScript(se, se.onDamage, "on_damage", amount);
            fireOutput(index, "OnDamaged", activator);
            if (e.health <= 0.0f && e.alive) {
                e.health = 0.0f;
                kill(se, true);
                fireOutput(index, "OnDeath", activator);
            }
        }
    } else if (input == "SetProperty") {
        const std::size_t eq = parameter.find('=');
        if (eq != std::string::npos && eq > 0) {
            e.properties[parameter.substr(0, eq)] = parameter.substr(eq + 1);
            se.propertiesChanged = true;
        }
    } else if (input == "ShowMessage") {
        std::string text = parameter;
        if (text.empty())
            if (const auto it = e.properties.find("message"); it != e.properties.end())
                text = it->second;
        if (message && !text.empty())
            message(text);
    } else if (input == "PlaySound") {
        const std::string sound = parameter.empty() && e.properties.count("sound") ? e.properties.at("sound") : parameter;
        if (playSound && !sound.empty()) {
            const glm::vec3 at = e.position + glm::vec3(0.0f, std::min(e.size.y, 1.5f) * 0.5f, 0.0f);
            playSound(sound, &at, 1.0f);
        }
    } else if (input == "Trigger" && (e.type == "relay" || e.type == "trigger")) {
        if (entityEnabled(e))
            fireOutput(index, "OnTrigger", activator);
    } else {
        known = false;
    }
    // Scripts may handle any input (also the built-in ones, after the engine did).
    if (!se.failed && se.onInput.valid()) {
        sol::object who = activator >= 0 ? sol::make_object(lua, entities[static_cast<std::size_t>(activator)].get()) : sol::object(sol::lua_nil);
        callScript(se, se.onInput, "on_input", input, parameter, who);
    } else if (!known) {
        log("[script] I/O: " + e.name + " (" + e.type + ") has no input " + input);
    }
}

void ScriptSystem::Impl::updateTriggers()
{
    const glm::vec3 feet = player.feet();
    const Aabb playerBox{feet - glm::vec3(0.3f, 0.0f, 0.3f), feet + glm::vec3(0.3f, 1.8f, 0.3f)};
    auto overlaps = [](const Aabb& a, const Aabb& b) {
        return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z &&
               a.max.z >= b.min.z;
    };
    for (std::size_t i = 0; i < entities.size(); ++i) {
        ScriptEntity& se = *entities[i];
        const Entity& e = se.e();
        if (e.type != "trigger")
            continue;
        bool inside = false;
        int activator = -1;
        if (e.alive && entityEnabled(e)) {
            const Aabb box = e.bounds();
            const TriggerFilter filter = triggerFilter(e);
            if (filter != TriggerFilter::Npc && playerHealth > 0.0f && overlaps(box, playerBox))
                inside = true;
            if (!inside && filter != TriggerFilter::Player)
                for (std::size_t j = 0; j < entities.size() && !inside; ++j) {
                    const Entity& other = entities[j]->e();
                    if (j != i && other.alive && !other.helper() && overlaps(box, other.bounds())) {
                        inside = true;
                        activator = static_cast<int>(j);
                    }
                }
        }
        if (inside == se.touching)
            continue;
        se.touching = inside;
        if (inside) {
            fireOutput(static_cast<int>(i), "OnStartTouch", activator);
            fireOutput(static_cast<int>(i), "OnTrigger", activator);
            if (const auto it = e.properties.find("once"); it != e.properties.end() && it->second == "1") {
                se.e().properties["enabled"] = "0"; // a one-shot trigger switches itself off
                se.propertiesChanged = true;
            }
        } else {
            fireOutput(static_cast<int>(i), "OnEndTouch", activator);
        }
    }
}

void ScriptSystem::Impl::bindApi()
{
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::coroutine);

    // vec3 value type
    lua.new_usertype<glm::vec3>("vec3",
        sol::call_constructor, sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),
        "x", &glm::vec3::x, "y", &glm::vec3::y, "z", &glm::vec3::z,
        sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
        sol::meta_function::multiplication, [](const glm::vec3& a, float k) { return a * k; },
        sol::meta_function::unary_minus, [](const glm::vec3& a) { return -a; },
        sol::meta_function::to_string, [](const glm::vec3& a) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "vec3(%.2f, %.2f, %.2f)", a.x, a.y, a.z);
            return std::string(buf);
        },
        "length", [](const glm::vec3& a) { return glm::length(a); },
        "normalized", [](const glm::vec3& a) { return glm::dot(a, a) > 0.0f ? glm::normalize(a) : a; },
        "flat", [](const glm::vec3& a) { return glm::vec3(a.x, 0.0f, a.z); },
        "dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); });

    lua.new_usertype<ScriptEntity>("Entity", sol::no_constructor,
        "name", sol::property([](ScriptEntity& s) { return s.e().name; }),
        "id", sol::property([](ScriptEntity& s) { return objectIdToString(s.e().id); }), // persistent, 16 hex digits
        "type", sol::property([](ScriptEntity& s) { return s.e().type; }),
        "position", sol::property(
            [](ScriptEntity& s) { return s.e().position; },
            [](ScriptEntity& s, const glm::vec3& p) {
                s.e().position = p;
                if (s.body)
                    s.body->teleport(p);
            }),
        "yaw", sol::property([](ScriptEntity& s) { return s.e().yawDegrees; },
                             [](ScriptEntity& s, float y) { s.e().yawDegrees = y; }),
        "color", sol::property([](ScriptEntity& s) { return s.e().color; },
                               [](ScriptEntity& s, const glm::vec3& c) { s.e().color = c; }),
        "height", sol::property([](ScriptEntity& s) { return s.e().size.y; }),
        "health", sol::property([](ScriptEntity& s) { return s.e().health; },
                                [](ScriptEntity& s, float h) { s.e().health = h; }),
        "alive", sol::property([](ScriptEntity& s) { return s.e().alive; }),
        "grounded", sol::property([](ScriptEntity& s) { return s.body && s.body->grounded(); }),
        "eye", [](ScriptEntity& s) { return s.e().position + glm::vec3(0.0f, s.e().size.y * 0.9f, 0.0f); },
        "forward", [](ScriptEntity& s) {
            const float yaw = glm::radians(s.e().yawDegrees);
            return glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw));
        },
        "enable_character", [](ScriptEntity& s) {
            if (s.body)
                return;
            CharacterShape shape;
            shape.radius = std::max(0.1f, std::min(s.e().size.x, s.e().size.z) * 0.5f);
            shape.height = std::max(shape.radius * 2.0f + 0.1f, s.e().size.y);
            s.body.emplace();
            s.body->create(*s.physics, shape, s.e().position);
        },
        "move", [](ScriptEntity& s, const glm::vec3& direction, float speed) {
            glm::vec3 d(direction.x, 0.0f, direction.z);
            s.desiredVelocity = glm::dot(d, d) > 1e-8f ? glm::normalize(d) * speed : glm::vec3(0.0f);
        },
        "jump", [](ScriptEntity& s, float speed) { s.jumpRequest = speed; },
        "move_to", [](ScriptEntity& s, const glm::vec3& target, float speed) { return s.moveTo(target, speed); },
        "face", [](ScriptEntity& s, const glm::vec3& point) {
            const glm::vec3 d = point - s.e().position;
            if (d.x * d.x + d.z * d.z > 1e-6f)
                s.e().yawDegrees = glm::degrees(std::atan2(d.z, d.x));
        },
        "log", [](ScriptEntity& s, const std::string& msg) { (*s.log)("[" + s.e().name + "] " + msg); },
        // Entity I/O: fires this entity's output (its connections set in the editor), e.g. "OnOpen".
        "fire_output", [this](ScriptEntity& s, const std::string& output) {
            fireOutput(static_cast<int>(s.index), output, -1);
        },
        // Entity properties (set in the editor: light "enabled", sit point "occupied"...), as text; nil if unset.
        "get_property", [](ScriptEntity& s, const std::string& key) -> sol::optional<std::string> {
            const auto it = s.e().properties.find(key);
            if (it == s.e().properties.end())
                return sol::nullopt;
            return it->second;
        },
        "set_property", [](ScriptEntity& s, const std::string& key, sol::object value) {
            if (!value.valid() || value.get_type() == sol::type::lua_nil)
                s.e().properties.erase(key);
            else if (value.get_type() == sol::type::boolean)
                s.e().properties[key] = value.as<bool>() ? "1" : "0";
            else
                s.e().properties[key] = value.get_type() == sol::type::number ? std::to_string(value.as<double>()) : value.as<std::string>();
        },
        // Animation (entities with a character model; see Properties > Character model in the editor).
        "has_model", sol::property([](ScriptEntity& s) { return s.e().model && s.e().model->skeleton; }),
        "flash", sol::property([](ScriptEntity& s) { return s.e().flash; },
                               [](ScriptEntity& s, float f) { s.e().flash = f; }),
        "play", [](ScriptEntity& s, const std::string& clip, sol::optional<sol::table> opts) {
            Entity& e = s.e();
            if (!e.model || !e.model->skeleton || clip.empty())
                return false;
            bool loop = true, restart = false;
            float speed = 1.0f, fade = 0.2f;
            if (opts) {
                loop = opts->get_or("loop", loop);
                restart = opts->get_or("restart", restart);
                speed = opts->get_or("speed", speed);
                fade = opts->get_or("fade", fade);
            }
            return e.animator.play(*e.model->skeleton, clip, loop, speed, fade, restart);
        },
        "animation", sol::property([](ScriptEntity& s) {
            const Entity& e = s.e();
            return e.model && e.model->skeleton ? e.animator.clipName(*e.model->skeleton) : std::string();
        }),
        "animation_done", [](ScriptEntity& s) {
            const Entity& e = s.e();
            return !e.model || !e.model->skeleton || e.animator.finished(*e.model->skeleton);
        },
        "animation_time", sol::property([](ScriptEntity& s) { return s.e().animator.current.time; }),
        "has_animation", [](ScriptEntity& s, const std::string& clip) {
            const Entity& e = s.e();
            return e.model && e.model->skeleton && e.model->skeleton->findClip(clip) >= 0;
        });

    sol::table game = lua.create_named_table("game");
    game.set_function("player_position", [this] { return player.feet(); });
    // Nearest entity of a type within max_distance (any distance when omitted); `free_key` skips
    // entities whose property of that name is set (e.g. "occupied" sit points).
    game.set_function("find_nearest", [this](const std::string& type, const glm::vec3& from, sol::optional<float> maxDistance,
                                             sol::optional<std::string> freeKey) -> ScriptEntity* {
        ScriptEntity* best = nullptr;
        float bestDistance = maxDistance.value_or(1e30f);
        for (auto& se : entities) {
            const Entity& e = se->e();
            if (e.type != type || !e.alive)
                continue;
            if (freeKey)
                if (const auto it = e.properties.find(*freeKey); it != e.properties.end() && !it->second.empty() && it->second != "0")
                    continue;
            const float d = glm::distance(e.position, from);
            if (d <= bestDistance) {
                bestDistance = d;
                best = se.get();
            }
        }
        return best;
    });
    game.set_function("player_eye", [this] { return player.eye(); });
    game.set_function("player_health", [this] { return playerHealth; });
    game.set_function("play_sound", [this](const std::string& sound, sol::optional<glm::vec3> position, sol::optional<float> volume) {
        if (playSound)
            playSound(sound, position ? &*position : nullptr, volume.value_or(1.0f));
    });
    game.set_function("nav_ready", [this] { return nav && nav->ready(); });
    game.set_function("random_nav_point", [this](const glm::vec3& center, float radius) -> sol::optional<glm::vec3> {
        if (nav && nav->ready())
            if (auto p = nav->randomPointAround(center, radius))
                return *p;
        return sol::nullopt;
    });
    game.set_function("find_path", [this](const glm::vec3& from, const glm::vec3& to) {
        sol::table t = lua.create_table();
        if (nav && nav->ready()) {
            int i = 1;
            for (const glm::vec3& p : nav->findPath(from, to))
                t[i++] = p;
        }
        return t;
    });
    game.set_function("change_level", [this](const std::string& file) { levelChange = file; });
    game.set_function("damage_player", [this](float amount) { playerHealth = std::max(0.0f, playerHealth - amount); });
    game.set_function("time", [this] { return time; });
    game.set_function("can_see", [this](const glm::vec3& from, const glm::vec3& to) {
        const glm::vec3 d = to - from;
        const float dist = glm::length(d);
        if (dist < 1e-4f)
            return true;
        const auto hit = physics.raycast(from, d, dist);
        return !hit || hit->distance >= dist - 0.05f;
    });
    game.set_function("raycast", [this](const glm::vec3& origin, const glm::vec3& dir, float maxDistance)
                                     -> std::tuple<bool, glm::vec3, float> {
        if (auto hit = physics.raycast(origin, dir, maxDistance))
            return {true, hit->point, hit->distance};
        return {false, origin, maxDistance};
    });
    game.set_function("find", [this](const std::string& name) -> ScriptEntity* {
        for (auto& se : entities)
            if (se->e().name == name)
                return se.get();
        return nullptr;
    });
    game.set_function("log", [this](const std::string& msg) { log("[lua] " + msg); });
    // Entity I/O: game.fire("Gate light", "TurnOn" [, parameter [, delay]]).
    game.set_function("fire", [this](const std::string& target, const std::string& input, sol::optional<std::string> parameter,
                                     sol::optional<float> delay) { sendInput(target, input, parameter.value_or(""), delay.value_or(0.0f), -1, -1); });
    game.set_function("duck_ambience", [this](float gain, sol::optional<float> seconds) {
        if (duck)
            duck(std::clamp(gain, 0.0f, 1.0f), std::max(0.0f, seconds.value_or(2.0f)));
    });
    // Day/night cycle: game.time_of_day() -> hour 0..24, game.set_time(hour [, minutesPerDay]).
    game.set_function("time_of_day", [this] { return scene.environment.time.hour; });
    game.set_function("set_time", [this](float hour, sol::optional<float> dayMinutes) {
        TimeOfDay& t = scene.environment.time;
        t.hour = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
        if (dayMinutes)
            t.dayMinutes = std::max(0.0f, *dayMinutes);
        t.enabled = true;
    });
    game.set_function("is_night", [this] {
        SkyState sky;
        environmentAt(scene.environment, scene.sunDirection, &sky);
        return sky.night > 0.5f;
    });
    game.set_function("message", [this](const std::string& text) {
        if (message)
            message(text);
    });
    // Console variables: game.cvar("fov") returns the value as text (nil if unknown); set_cvar respects cheat / read-only flags.
    // Debug drawing (shown while r_debugdraw is 1): colour vec3 (0..1), seconds 0 = this frame.
    auto rgba = [](const sol::optional<glm::vec3>& c, glm::vec3 fallback) { return glm::vec4(c.value_or(fallback), 1.0f); };
    game.set_function("debug_line", [rgba](const glm::vec3& a, const glm::vec3& b, sol::optional<glm::vec3> color, sol::optional<float> seconds) {
        DebugDraw::global().line(a, b, rgba(color, {1, 1, 0}), seconds.value_or(0.0f));
    });
    game.set_function("debug_arrow", [rgba](const glm::vec3& a, const glm::vec3& b, sol::optional<glm::vec3> color, sol::optional<float> seconds) {
        DebugDraw::global().arrow(a, b, rgba(color, {1, 1, 0}), seconds.value_or(0.0f));
    });
    game.set_function("debug_box", [rgba](const glm::vec3& center, const glm::vec3& half, sol::optional<glm::vec3> color, sol::optional<float> seconds) {
        DebugDraw::global().box(center, half, rgba(color, {0, 1, 1}), seconds.value_or(0.0f));
    });
    game.set_function("debug_sphere", [rgba](const glm::vec3& center, float radius, sol::optional<glm::vec3> color, sol::optional<float> seconds) {
        DebugDraw::global().sphere(center, radius, rgba(color, {1, 0.4f, 0.2f}), seconds.value_or(0.0f));
    });
    game.set_function("debug_text", [rgba](const glm::vec3& at, const std::string& text, sol::optional<glm::vec3> color, sol::optional<float> seconds) {
        DebugDraw::global().text(at, text, rgba(color, {1, 1, 1}), seconds.value_or(0.0f));
    });
    game.set_function("cvar", [](const std::string& name) -> sol::optional<std::string> {
        if (const CVar* v = CVarRegistry::global().find(name))
            return v->value;
        return sol::nullopt;
    });
    game.set_function("set_cvar", [](const std::string& name, const std::string& value) {
        return CVarRegistry::global().set(name, value);
    });
    lua.set_function("print", [this](sol::variadic_args args) {
        std::string line;
        for (auto a : args) {
            if (!line.empty())
                line += ' ';
            line += lua["tostring"](a.get<sol::object>()).get<std::string>();
        }
        log("[lua] " + line);
    });
}

ScriptSystem::ScriptSystem(Scene& scene, PhysicsWorld& physics, PlayerController& player, LogFn log)
    : impl_(std::make_unique<Impl>(scene, physics, player, std::move(log)))
{
    impl_->bindApi();
}

ScriptSystem::~ScriptSystem() = default;

int ScriptSystem::start()
{
    Impl& d = *impl_;
    d.entities.clear();
    for (std::size_t i = 0; i < d.scene.entities.size(); ++i) {
        auto se = std::make_unique<ScriptEntity>();
        se->log = &d.log;
        se->scene = &d.scene;
        se->physics = &d.physics;
        se->nav = &d.nav;
        se->time = &d.time;
        se->index = i;
        d.entities.push_back(std::move(se));
    }
    for (auto& se : d.entities) {
        const Entity& e = se->e();
        if (e.scriptPath.empty())
            continue;
        // Each entity gets its own globals, falling back to the shared ones.
        se->env = sol::environment(d.lua, sol::create, d.lua.globals());
        const std::u8string u8 = e.scriptPath.u8string();
        sol::protected_function_result loaded =
            d.lua.safe_script_file(std::string(u8.begin(), u8.end()), se->env, sol::script_pass_on_error);
        if (!loaded.valid()) {
            sol::error err = loaded;
            d.report(*se, err.what());
            se->failed = true;
            continue;
        }
        buildProps(d.lua, se->env, e);
        if (sol::protected_function onStart = se->env["on_start"]; onStart.valid()) {
            auto r = onStart(se.get());
            if (!r.valid()) {
                sol::error err = r;
                d.report(*se, std::string("on_start: ") + err.what());
                se->failed = true;
                continue;
            }
        }
        se->onUpdate = se->env["on_update"];
        se->onDamage = se->env["on_damage"];
        se->onDeath = se->env["on_death"];
        se->onInput = se->env["on_input"];
    }
    d.queue.clear();
    for (std::size_t i = 0; i < d.entities.size(); ++i)
        d.fireOutput(static_cast<int>(i), "OnStart", -1); // delivered on the first update
    return d.errors;
}

void ScriptSystem::fixedUpdate(float dt)
{
    Impl& d = *impl_;
    d.time += dt;
    d.updateTriggers();
    // Due inputs, oldest first; inputs they cause with no delay run in this step too.
    for (int guard = 0; guard < 64 && !d.queue.empty(); ++guard) {
        std::vector<Impl::Queued> due;
        for (auto it = d.queue.begin(); it != d.queue.end();) {
            if (it->at <= d.time) {
                due.push_back(std::move(*it));
                it = d.queue.erase(it);
            } else {
                ++it;
            }
        }
        if (due.empty())
            break;
        std::sort(due.begin(), due.end(), [](const Impl::Queued& a, const Impl::Queued& b) {
            return a.at != b.at ? a.at < b.at : a.order < b.order;
        });
        for (const Impl::Queued& q : due)
            d.deliver(q);
    }
    for (auto& se : d.entities) {
        if (se->failed || !se->onUpdate.valid() || !se->e().alive)
            continue;
        auto r = se->onUpdate(se.get(), dt);
        if (!r.valid()) {
            sol::error err = r;
            d.report(*se, std::string("on_update: ") + err.what());
            se->failed = true; // stop calling a broken script every frame
        }
    }
    for (auto& se : d.entities) {
        if (!se->e().alive) {
            se->impulse = se->drift = glm::vec3(0.0f);
            continue;
        }
        if (!se->body) { // no capsule: outside forces just move it
            se->e().position += (se->drift + se->impulse * 0.1f) * dt;
            se->impulse = se->drift = glm::vec3(0.0f);
            continue;
        }
        glm::vec3 impulse = se->impulse;
        if (se->drift.y > 0.0f)
            impulse.y += (se->drift.y - std::max(0.0f, se->body->velocity().y)) * std::min(1.0f, 8.0f * dt) + 18.0f * dt;
        const bool pushed = glm::dot(se->impulse, se->impulse) > 0.0f;
        se->body->step(se->desiredVelocity * (1.0f - std::clamp(se->slow, 0.0f, 1.0f)) + glm::vec3(se->drift.x, 0.0f, se->drift.z) +
                           (pushed ? glm::vec3(se->impulse.x, 0.0f, se->impulse.z) : glm::vec3(0.0f)),
                       se->jumpRequest, 18.0f, pushed ? 1.0f : 0.2f, dt, glm::vec3(0.0f, impulse.y, 0.0f));
        se->e().position = se->body->feet();
        se->desiredVelocity = glm::vec3(0.0f);
        se->jumpRequest = 0.0f;
        se->impulse = se->drift = glm::vec3(0.0f);
        se->slow = 0.0f;
    }
}

void ScriptSystem::pushEntity(int entityIndex, glm::vec3 impulse, glm::vec3 drift, float slow)
{
    Impl& d = *impl_;
    if (entityIndex < 0 || entityIndex >= static_cast<int>(d.entities.size()))
        return;
    ScriptEntity& se = *d.entities[static_cast<std::size_t>(entityIndex)];
    se.impulse += impulse;
    se.drift += drift;
    se.slow = std::max(se.slow, slow);
}

void ScriptSystem::teleportEntity(int entityIndex, glm::vec3 feet)
{
    Impl& d = *impl_;
    if (entityIndex < 0 || entityIndex >= static_cast<int>(d.entities.size()))
        return;
    ScriptEntity& se = *d.entities[static_cast<std::size_t>(entityIndex)];
    if (se.body)
        se.body->teleport(feet);
    se.e().position = feet;
    se.path.clear();
}

void ScriptSystem::damageEntity(int entityIndex, float amount)
{
    Impl& d = *impl_;
    if (entityIndex < 0 || entityIndex >= static_cast<int>(d.entities.size()))
        return;
    ScriptEntity& se = *d.entities[static_cast<std::size_t>(entityIndex)];
    Entity& e = se.e();
    if (!e.alive)
        return;
    e.health -= amount;
    auto call = [&](sol::protected_function& fn, const char* what, auto&&... args) {
        if (se.failed || !fn.valid())
            return;
        auto r = fn(&se, std::forward<decltype(args)>(args)...);
        if (!r.valid()) {
            sol::error err = r;
            d.report(se, std::string(what) + ": " + err.what());
            se.failed = true;
        }
    };
    call(se.onDamage, "on_damage", amount);
    d.fireOutput(entityIndex, "OnDamaged", -1);
    if (e.health <= 0.0f && e.alive) {
        e.health = 0.0f;
        e.alive = false;
        call(se.onDeath, "on_death");
        se.body.reset(); // removes the capsule from the world
        d.fireOutput(entityIndex, "OnDeath", -1);
    }
}

bool readScriptProperties(const std::filesystem::path& script, std::vector<ScriptProperty>& out, std::string* error)
{
    out.clear();
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
    // Stubs so top-level code that references the API still loads.
    lua.create_named_table("game");
    lua["vec3"] = [](sol::variadic_args) { return 0; };
    const std::u8string u8 = script.u8string();
    auto loaded = lua.safe_script_file(std::string(u8.begin(), u8.end()), sol::script_pass_on_error);
    if (!loaded.valid()) {
        if (error) {
            sol::error err = loaded;
            *error = err.what();
        }
        return false;
    }
    sol::object decl = lua["properties"];
    if (!decl.is<sol::table>())
        return true; // script has no tunables
    sol::table t = decl.as<sol::table>();
    for (std::size_t i = 1; i <= t.size(); ++i) {
        sol::object rowObj = t[i];
        if (!rowObj.is<sol::table>())
            continue;
        sol::table row = rowObj.as<sol::table>();
        ScriptProperty p;
        sol::object name = row[1];
        sol::object def = row[2];
        sol::object desc = row[3];
        if (!name.is<std::string>())
            continue;
        p.name = name.as<std::string>();
        p.defaultValue = lua["tostring"](def).get<std::string>();
        if (desc.is<std::string>())
            p.description = desc.as<std::string>();
        out.push_back(std::move(p));
    }
    return true;
}

ScriptCheck checkScriptSyntax(const std::string& source, const std::string& chunkName)
{
    ScriptCheck result;
    sol::state lua;
    const std::string chunk = "=" + chunkName; // '=' keeps the name verbatim in messages
    if (luaL_loadbufferx(lua.lua_state(), source.data(), source.size(), chunk.c_str(), "t") == LUA_OK)
        return result;
    result.ok = false;
    std::string msg = lua_tostring(lua.lua_state(), -1) ? lua_tostring(lua.lua_state(), -1) : "syntax error";
    // "name:12: message"
    const std::string prefix = chunkName + ":";
    if (msg.rfind(prefix, 0) == 0) {
        const std::size_t colon = msg.find(':', prefix.size());
        if (colon != std::string::npos) {
            try {
                result.line = std::stoi(msg.substr(prefix.size(), colon - prefix.size()));
                msg = msg.substr(colon + 1);
                if (!msg.empty() && msg[0] == ' ')
                    msg.erase(0, 1);
            } catch (...) {
            }
        }
    }
    result.message = msg;
    return result;
}

float ScriptSystem::playerHealth() const { return impl_->playerHealth; }
void ScriptSystem::setSoundHandler(SoundFn handler) { impl_->playSound = std::move(handler); }
void ScriptSystem::setNavMesh(const NavMesh* nav) { impl_->nav = nav; }
void ScriptSystem::setMessageHandler(MessageFn handler) { impl_->message = std::move(handler); }
void ScriptSystem::setDuckHandler(DuckFn handler) { impl_->duck = std::move(handler); }
void ScriptSystem::fireOutput(int entityIndex, const std::string& output, int activator) { impl_->fireOutput(entityIndex, output, activator); }
void ScriptSystem::sendInput(const std::string& target, const std::string& input, const std::string& parameter, float delay, int activator,
                             int caller)
{
    impl_->sendInput(target, input, parameter, delay, activator, caller);
}
int ScriptSystem::pendingInputs() const { return static_cast<int>(impl_->queue.size()); }

std::vector<ScriptSystem::PendingInput> ScriptSystem::savePendingInputs() const
{
    const Impl& d = *impl_;
    std::vector<PendingInput> out;
    for (const Impl::Queued& q : d.queue) {
        PendingInput p;
        p.delay = static_cast<float>(std::max(0.0, q.at - d.time));
        p.target = q.target;
        p.input = q.input;
        p.parameter = q.parameter;
        p.caller = q.caller >= 0 ? d.entities[static_cast<std::size_t>(q.caller)]->e().id : 0;
        p.activator = q.activator >= 0 ? d.entities[static_cast<std::size_t>(q.activator)]->e().id : 0;
        out.push_back(std::move(p));
    }
    return out;
}

void ScriptSystem::restorePendingInputs(const std::vector<PendingInput>& pending)
{
    Impl& d = *impl_;
    d.queue.clear();
    auto indexOf = [&d](ObjectId id) {
        for (std::size_t i = 0; id != 0 && i < d.entities.size(); ++i)
            if (d.entities[i]->e().id == id)
                return static_cast<int>(i);
        return -1;
    };
    for (const PendingInput& p : pending)
        d.sendInput(p.target, p.input, p.parameter, p.delay, indexOf(p.activator), indexOf(p.caller));
}
void ScriptSystem::setPlayerHealth(float health) { impl_->playerHealth = health; }

std::string ScriptSystem::takeLevelChange()
{
    std::string file;
    file.swap(impl_->levelChange);
    return file;
}
int ScriptSystem::errorCount() const { return impl_->errors; }

} // namespace engine

namespace engine {

namespace {

// Lua value -> one-line Lua literal (nil, booleans, numbers, strings, vec3, tables with
// string / number keys). Functions and other userdata are left out.
void writeLua(const sol::object& o, std::string& out, int depth)
{
    switch (o.get_type()) {
    case sol::type::boolean: out += o.as<bool>() ? "true" : "false"; break;
    case sol::type::number: {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.17g", o.as<double>());
        out += buf;
        break;
    }
    case sol::type::string: {
        out += '"';
        for (char c : o.as<std::string>()) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) < 32) {
                    char esc[8];
                    std::snprintf(esc, sizeof esc, "\\%03d", static_cast<unsigned char>(c));
                    out += esc;
                } else {
                    out += c;
                }
            }
        }
        out += '"';
        break;
    }
    case sol::type::userdata:
        if (o.is<glm::vec3>()) {
            const glm::vec3 v = o.as<glm::vec3>();
            char buf[96];
            std::snprintf(buf, sizeof buf, "vec3(%.9g,%.9g,%.9g)", v.x, v.y, v.z);
            out += buf;
        } else {
            out += "nil";
        }
        break;
    case sol::type::table: {
        if (depth > 16) { // cycles or absurd nesting
            out += "nil";
            break;
        }
        out += '{';
        bool first = true;
        for (const auto& [key, value] : o.as<sol::table>()) {
            const sol::type kt = key.get_type(), vt = value.get_type();
            if ((kt != sol::type::string && kt != sol::type::number) || vt == sol::type::function || vt == sol::type::lua_nil)
                continue;
            if (!first)
                out += ',';
            first = false;
            out += '[';
            writeLua(key, out, depth + 1);
            out += "]=";
            writeLua(value, out, depth + 1);
        }
        out += '}';
        break;
    }
    default: out += "nil"; break;
    }
}

} // namespace

std::vector<ScriptSystem::EntityState> ScriptSystem::saveEntities()
{
    Impl& d = *impl_;
    std::vector<EntityState> states;
    for (auto& se : d.entities) {
        const Entity& e = se->e();
        EntityState st;
        st.id = e.id;
        st.alive = e.alive;
        st.health = e.health;
        st.position = e.position;
        st.yaw = e.yawDegrees;
        st.propertiesChanged = se->propertiesChanged;
        if (se->propertiesChanged)
            st.properties = e.properties;
        st.touching = se->touching;
        for (std::size_t i = 0; i < se->fired.size(); ++i)
            if (se->fired[i] && i < e.outputs.size() && e.outputs[i].once)
                st.firedOutputs.push_back(static_cast<int>(i));
        if (!se->failed && se->env.valid()) {
            if (sol::protected_function onSave = se->env["on_save"]; onSave.valid()) {
                auto r = onSave(se.get());
                if (!r.valid()) {
                    sol::error err = r;
                    d.report(*se, std::string("on_save: ") + err.what());
                } else if (r.return_count() > 0) {
                    sol::object value = r.get<sol::object>();
                    if (value.get_type() != sol::type::lua_nil)
                        writeLua(value, st.data, 0);
                }
            }
        }
        states.push_back(std::move(st));
    }
    return states;
}

void ScriptSystem::restoreEntities(const std::vector<EntityState>& states)
{
    Impl& d = *impl_;
    for (const EntityState& st : states) {
        auto it = std::find_if(d.entities.begin(), d.entities.end(), [&](const auto& se) { return se->e().id == st.id; });
        if (it == d.entities.end())
            continue; // the level changed since the save: that object is gone
        ScriptEntity& se = **it;
        Entity& e = se.e();
        e.alive = st.alive;
        e.health = st.health;
        e.yawDegrees = st.yaw;
        if (se.body)
            se.body->teleport(st.position);
        e.position = st.position;
        se.path.clear();
        if (!e.alive)
            se.body.reset();
        if (st.propertiesChanged) {
            e.properties = st.properties;
            se.propertiesChanged = true;
        }
        se.touching = st.touching;
        se.fired.assign(e.outputs.size(), 0);
        for (int i : st.firedOutputs)
            if (i >= 0 && i < static_cast<int>(se.fired.size()))
                se.fired[static_cast<std::size_t>(i)] = 1;
        if (se.failed || !se.env.valid())
            continue;
        sol::protected_function onLoad = se.env["on_load"];
        if (!onLoad.valid())
            continue;
        sol::object data = sol::lua_nil;
        if (!st.data.empty()) {
            // Read the literal in an environment that knows only vec3.
            sol::environment readEnv(d.lua, sol::create);
            readEnv["vec3"] = d.lua["vec3"];
            auto parsed = d.lua.safe_script("return " + st.data, readEnv, sol::script_pass_on_error);
            if (parsed.valid())
                data = parsed.get<sol::object>();
            else
                d.report(se, "saved data unreadable");
        }
        auto r = onLoad(&se, data);
        if (!r.valid()) {
            sol::error err = r;
            d.report(se, std::string("on_load: ") + err.what());
        }
    }
}

double ScriptSystem::time() const
{
    return impl_->time;
}

void ScriptSystem::setTime(double seconds)
{
    impl_->time = seconds;
}

} // namespace engine
