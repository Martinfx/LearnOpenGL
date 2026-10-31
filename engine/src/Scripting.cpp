#include "engine/Log.h"
#include "engine/Scripting.h"
#include "engine/CharacterBody.h"
#include "engine/NavMesh.h"
#include "engine/Physics.h"
#include "engine/PlayerController.h"

#include <sol/sol.hpp>

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
    sol::environment env;
    sol::protected_function onUpdate;
    sol::protected_function onDamage;
    sol::protected_function onDeath;
    bool failed = false;
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
};

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
    }
    return d.errors;
}

void ScriptSystem::fixedUpdate(float dt)
{
    Impl& d = *impl_;
    d.time += dt;
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
        if (!se->body || !se->e().alive)
            continue;
        se->body->step(se->desiredVelocity, se->jumpRequest, 18.0f, 0.2f, dt);
        se->e().position = se->body->feet();
        se->desiredVelocity = glm::vec3(0.0f);
        se->jumpRequest = 0.0f;
    }
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
    if (e.health <= 0.0f && e.alive) {
        e.health = 0.0f;
        e.alive = false;
        call(se.onDeath, "on_death");
        se.body.reset(); // removes the capsule from the world
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
void ScriptSystem::setPlayerHealth(float health) { impl_->playerHealth = health; }
int ScriptSystem::errorCount() const { return impl_->errors; }

} // namespace engine
