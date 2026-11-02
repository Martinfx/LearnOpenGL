// Game runtime: GLFW window, fixed 60 Hz simulation, first-person player.
// Usage: fps_game [level-file]
#include <engine/ALife.h>
#include <engine/Vfs.h>
#include <engine/EntityIO.h>
#include <engine/Fauna.h>
#include <engine/ProbeBaker.h>
#include <engine/JobSystem.h>
#include <engine/Log.h>
#include <engine/Profile.h>
#include <engine/CVars.h>
#include <engine/DebugDraw.h>
#include <engine/Paths.h>
#include <engine/Crash.h>
#include <engine/Assert.h>
#include <engine/Camera.h>
#include <engine/CpuFeatures.h>
#include <engine/FixedStepClock.h>
#include <engine/Input.h>
#include <engine/Physics.h>
#include <engine/Ragdoll.h>
#include <engine/PlayerController.h>
#include <engine/Renderer.h>
#include <engine/Scene.h>
#include <engine/Ui2D.h>

#include "UiScale.h"
#include "GameAnomalies.h"
#include "GameAudio.h"
#include "GameEffects.h"
#include "GameConsole.h"
#include "GameDialog.h"
#include "GameTrade.h"
#include "GameArtifacts.h"
#include "GameHud.h"
#include "GameMenu.h"
#include "GameSave.h"
#include "Flashlight.h"
#include "GameWeather.h"
#include "Inventory.h"
#include <engine/Water.h>
#include <engine/Memory.h>
#include <engine/FileWatcher.h>
#include <engine/HotReload.h>
#include <engine/WorldStreaming.h>
#include <engine/Demo.h>
#include <engine/Localization.h>
#include <limits>
#include "KeyNames.h"
#include "MapHud.h"
#include <engine/Scripting.h>
#include <engine/ScriptDebug.h>
#include <engine/NavMesh.h>
#include <engine/Weapon.h>

#include <glm/gtc/matrix_transform.hpp>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>
#include <atomic>
#include <ctime>
#include <cctype>
#include <map>
#include <optional>
#include <thread>
#include <deque>
#include <iostream>
#include <mutex>
#include <set>
#include <chrono>
#include <functional>
#include <array>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <string>
#include <random>
#include <vector>

namespace {

struct Mouse {
    double lastX = 0.0, lastY = 0.0;
    bool first = true;
    glm::vec2 delta{0.0f};
    double scroll = 0.0;
    GameConsole* console = nullptr; // receives typed text and keys while open
    GameMenu* menu = nullptr;       // key presses while it waits for a key binding
};

// How a level session ended, and what a level change carries over.
struct LevelExit {
    enum Kind { None, Quit, Load, Change, LoadSave } kind = None; // LoadSave: `level` is the save file
    std::filesystem::path level;
};
struct Carry {
    float health = 100.0f;
    std::size_t weapon = 0;
    std::vector<int> ammo;                          // magazines, parallel to weaponFiles
    std::vector<std::filesystem::path> weaponFiles; // carried weapons (loadout and pickups)
    std::map<std::string, int> stock;               // rounds by ammo type
    int medkits = 0;
    int money = 0;
    std::vector<float> condition;                   // weapon wear, parallel to weaponFiles
    std::string armor;
    float armorCondition = 1.0f;
    std::vector<std::string> artifacts;
};

// Attribution required by CC-BY assets: print the CREDITS.md rows that name the
// view models in use (shown again in the credits screen once the menu exists).
void printModelCredits(const std::vector<std::filesystem::path>&, const std::vector<engine::Weapon>& weapons)
{
    std::set<std::string> printed;
    for (const engine::Weapon& w : weapons) {
        const std::filesystem::path& model = w.settings.modelPath;
        if (model.empty())
            continue;
        engine::VfsStream in(model.parent_path() / "CREDITS.md");
        const std::string name = model.filename().string();
        for (std::string line; std::getline(in, line);)
            if (line.find(name) != std::string::npos && printed.insert(line).second)
                LOG_INFO("credits", "%s\n", line.c_str());
        std::fflush(stdout);
    }
}

// One weapon from a .weapon file with its view model; nullopt (and a warning) on error.
std::optional<engine::Weapon> loadWeapon(const std::filesystem::path& file)
{
    engine::WeaponSettings settings;
    std::string error;
    if (!engine::loadWeaponFile(settings, file, &error)) {
        LOG_WARN("weapon", "%s\n", error.c_str());
        return std::nullopt;
    }
    engine::Weapon weapon(settings);
    if (!settings.modelPath.empty()) {
        // Skinned arms + weapon keep their skeleton and clips (cached); static guns are baked.
        if (auto model = engine::loadCharacterModel(settings.modelPath, &error); model && model->skeleton) {
            weapon.viewModel = model;
        } else if (auto baked = std::make_shared<engine::Model>(); engine::loadModelFile(*baked, settings.modelPath, &error)) {
            weapon.viewModel = baked;
        } else {
            LOG_WARN("weapon", "%s\n", error.c_str());
        }
    }
    return weapon;
}

// Player's weapons from the level loadout (falls back to the AK-74M); `files` gets the
// file of each weapon loaded (same order).
std::vector<engine::Weapon> loadLoadout(const engine::Scene& scene, const std::filesystem::path& exeDir,
                                        std::vector<std::filesystem::path>& files)
{
    std::vector<std::filesystem::path> wanted = scene.loadout;
    if (wanted.empty())
        wanted.push_back(exeDir / "assets/weapons/ak74.weapon");
    std::vector<engine::Weapon> weapons;
    files.clear();
    for (const auto& file : wanted)
        if (auto weapon = loadWeapon(file)) {
            weapons.push_back(std::move(*weapon));
            files.push_back(std::filesystem::absolute(file).lexically_normal());
        }
    printModelCredits(wanted, weapons);
    if (weapons.empty()) {
        weapons.emplace_back(); // bare default so the game stays playable
        files.emplace_back();
    }
    return weapons;
}

// Ricochet dice (seeded from the game seed, so a demo replays the same ricochets).
std::mt19937& ricochetRng()
{
    static std::mt19937 rng{engine::seedFor("ricochet")};
    return rng;
}
bool ricochetRoll(float chance)
{
    static const bool always = std::getenv("FPS_ENGINE_RICOCHET") != nullptr; // test hook: every possible ricochet happens
    if (always && chance > 0.0f)
        return true;
    return chance > 0.0f && std::uniform_real_distribution<float>(0.0f, 1.0f)(ricochetRng()) < chance;
}
float ricochetLift() { return std::uniform_real_distribution<float>(1.0f, 8.0f)(ricochetRng()); }

// Key bindings: console variables in_* (key names from KeyNames.h), edited in
// Settings > Controls and saved in game.cfg.
struct Binding {
    const char* cvar;
    const char* key;   // default
    const char* label; // settings screen
    int action;        // engine::Action, or -1 for keys the game reads itself (bolt, map)
};
constexpr Binding kBindings[] = {
    {"in_forward", "W", "Dopředu", static_cast<int>(engine::Action::MoveForward)},
    {"in_back", "S", "Dozadu", static_cast<int>(engine::Action::MoveBack)},
    {"in_left", "A", "Vlevo", static_cast<int>(engine::Action::MoveLeft)},
    {"in_right", "D", "Vpravo", static_cast<int>(engine::Action::MoveRight)},
    {"in_jump", "SPACE", "Skok", static_cast<int>(engine::Action::Jump)},
    {"in_sprint", "LSHIFT", "Sprint", static_cast<int>(engine::Action::Sprint)},
    {"in_crouch", "LCTRL", "Dolů (noclip, volná kamera)", static_cast<int>(engine::Action::Crouch)},
    {"in_fire", "MOUSE1", "Střelba", static_cast<int>(engine::Action::Fire)},
    {"in_aim", "MOUSE2", "Míření", static_cast<int>(engine::Action::Aim)},
    {"in_reload", "R", "Přebití", static_cast<int>(engine::Action::Reload)},
    {"in_bolt", "G", "Hodit šroub", -1},
    {"in_map", "M", "Mapa", -1},
    {"in_flashlight", "F", "Baterka", -1},
    {"in_heal", "H", "Lékárnička", -1},
    {"in_use", "E", "Mluvit / použít", -1},
    {"in_quicksave", "F5", "Rychlé uložení", -1},
    {"in_quickload", "F9", "Rychlé načtení", -1},
};

// Current key of a binding (parsed again only when the variable changed).
int boundKey(const char* cvar)
{
    static std::map<std::string, std::pair<std::string, int>> cache;
    const engine::CVar* v = engine::CVarRegistry::global().find(cvar);
    if (!v)
        return -1;
    auto& [text, code] = cache[cvar];
    if (text != v->value || code == 0) {
        text = v->value;
        code = keyCode(text);
    }
    return code;
}

// Demo recording and playback (--record x.demo, --play x.demo [--benchmark]): every key, button
// and cursor read of a frame goes through keyHeld / cursorPos, so a recording holds exactly what
// the game asked for and a replay answers the same.
struct DemoState {
    enum Mode { Off, Record, Play } mode = Off;
    engine::Demo demo;
    std::filesystem::path file;
    bool benchmark = false;       // play, print the frame statistics and quit
    std::size_t next = 0;         // next frame to play
    engine::DemoFrame current;    // the frame being recorded or played
    std::vector<float> realFrames; // measured frame times while playing
    double lastReal = 0.0;
    long desyncFrame = -1;        // first frame where the player left the recorded path
    float desyncDistance = 0.0f;
    float maxDistance = 0.0f;
    bool replayedSettings = false; // the demo's settings were applied: do not save them as the player's
};
DemoState& demoState()
{
    static DemoState state;
    return state;
}
constexpr int kDemoFocus = 999; // pseudo key: the window has the focus
// Settings that change what the same input does; a demo stores and replays them.
const char* const kDemoCVars[] = {"m_sensitivity", "m_invert_y", "god", "noclip", "freecam", "timescale", "fov"};

bool rawHeld(GLFWwindow* window, int code)
{
    if (code == kDemoFocus)
        return glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
    if (code >= kMouseBase)
        return glfwGetMouseButton(window, code - kMouseBase) == GLFW_PRESS;
    return glfwGetKey(window, code) == GLFW_PRESS;
}

bool keyHeld(GLFWwindow* window, int code)
{
    if (code < 0)
        return false;
    DemoState& demo = demoState();
    auto& keys = demo.current.keys;
    const bool listed = std::find(keys.begin(), keys.end(), static_cast<std::uint16_t>(code)) != keys.end();
    if (demo.mode == DemoState::Play)
        return listed;
    const bool down = rawHeld(window, code);
    if (demo.mode == DemoState::Record && down && !listed)
        keys.push_back(static_cast<std::uint16_t>(code));
    return down;
}

void cursorPos(GLFWwindow* window, double* x, double* y)
{
    DemoState& demo = demoState();
    if (demo.mode == DemoState::Play) {
        *x = demo.current.cursor.x;
        *y = demo.current.cursor.y;
        return;
    }
    glfwGetCursorPos(window, x, y);
    if (demo.mode == DemoState::Record)
        demo.current.cursor = glm::vec2(static_cast<float>(*x), static_cast<float>(*y));
}

bool keyDown(GLFWwindow* window, int code)
{
    return keyHeld(window, code);
}

void readInput(GLFWwindow* window, engine::InputState& input)
{
    for (const Binding& b : kBindings)
        if (b.action >= 0)
            input.set(static_cast<engine::Action>(b.action), keyDown(window, boundKey(b.cvar)));
}

// Gamepad (any GLFW-mapped controller): left stick moves, right stick looks, RT fires,
// LT aims, A jump, B down, X reload, L3 sprint, Y next weapon, Start menu, Back map.
struct PadState {
    bool present = false;
    glm::vec2 look{0.0f};
    bool start = false, back = false, nextWeapon = false;
};
PadState readGamepad(engine::InputState& input, float deadzone)
{
    PadState out;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_4; ++jid) {
        GLFWgamepadstate pad;
        if (!glfwJoystickIsGamepad(jid) || !glfwGetGamepadState(jid, &pad))
            continue;
        out.present = true;
        auto axis = [&](int a) {
            const float v = pad.axes[a];
            if (std::abs(v) < deadzone)
                return 0.0f;
            const float k = (std::abs(v) - deadzone) / (1.0f - deadzone);
            return std::copysign(k * k, v); // finer control near the centre
        };
        using engine::Action;
        const float lx = axis(GLFW_GAMEPAD_AXIS_LEFT_X), ly = axis(GLFW_GAMEPAD_AXIS_LEFT_Y);
        auto press = [&](Action a, bool on) {
            if (on)
                input.set(a, true);
        };
        press(Action::MoveForward, ly < -0.3f);
        press(Action::MoveBack, ly > 0.3f);
        press(Action::MoveLeft, lx < -0.3f);
        press(Action::MoveRight, lx > 0.3f);
        press(Action::Fire, pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.0f);
        press(Action::Aim, pad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] > 0.0f || pad.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER]);
        press(Action::Jump, pad.buttons[GLFW_GAMEPAD_BUTTON_A]);
        press(Action::Crouch, pad.buttons[GLFW_GAMEPAD_BUTTON_B]);
        press(Action::Reload, pad.buttons[GLFW_GAMEPAD_BUTTON_X]);
        press(Action::Sprint, pad.buttons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB]);
        out.look += glm::vec2(axis(GLFW_GAMEPAD_AXIS_RIGHT_X), axis(GLFW_GAMEPAD_AXIS_RIGHT_Y));
        out.start = out.start || pad.buttons[GLFW_GAMEPAD_BUTTON_START];
        out.back = out.back || pad.buttons[GLFW_GAMEPAD_BUTTON_BACK];
        out.nextWeapon = out.nextWeapon || pad.buttons[GLFW_GAMEPAD_BUTTON_Y];
    }
    return out;
}

// Window mode: 0 windowed, 1 borderless on a monitor, 2 exclusive fullscreen.
struct WindowMode {
    int mode = 0, monitor = 0;
    glm::ivec2 windowedPos{100, 100}, windowedSize{1280, 720};
};
void applyWindowMode(GLFWwindow* window, WindowMode& current, int mode, int monitorIndex)
{
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    if (count <= 0)
        return;
    GLFWmonitor* monitor = monitors[std::clamp(monitorIndex, 0, count - 1)];
    const GLFWvidmode* video = glfwGetVideoMode(monitor);
    if (!video)
        return;
    if (current.mode == 0 && mode != 0) { // remember the window to go back to
        glfwGetWindowPos(window, &current.windowedPos.x, &current.windowedPos.y);
        glfwGetWindowSize(window, &current.windowedSize.x, &current.windowedSize.y);
    }
    if (mode == 2) {
        glfwSetWindowMonitor(window, monitor, 0, 0, video->width, video->height, video->refreshRate);
    } else if (mode == 1) {
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitor, &mx, &my);
        glfwSetWindowMonitor(window, nullptr, mx, my, video->width, video->height, 0);
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowPos(window, mx, my);
        glfwSetWindowSize(window, video->width, video->height);
    } else {
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowMonitor(window, nullptr, current.windowedPos.x, current.windowedPos.y, current.windowedSize.x,
                             current.windowedSize.y, 0);
    }
    current.mode = mode;
    current.monitor = monitorIndex;
    LOG_INFO("window", "%s on monitor %d (%dx%d)", mode == 2 ? "fullscreen" : mode == 1 ? "borderless" : "windowed",
             monitorIndex, video->width, video->height);
}

struct Impact {
    glm::vec3 point;
    float age = 0.0f;
    bool onEnemy = false;
};

// View-space transform of the first-person weapon: idle offset, walk bob,
// recoil kick and the reload dip.
float smoothAim(const engine::Weapon& weapon)
{
    const float a = weapon.aimAmount();
    return a * a * (3.0f - 2.0f * a);
}

float viewBobScale()
{
    const engine::CVar* v = engine::CVarRegistry::global().find("cl_viewbob");
    return v ? std::clamp(static_cast<float>(v->number()), 0.0f, 1.0f) : 1.0f;
}

glm::mat4 weaponTransform(const engine::Weapon& weapon, float time, float moveSpeed, float lowered)
{
    // Aiming brings the sights to the eye and steadies the walk bob; cl_viewbob scales
    // it (0 = still, for players who get motion sick).
    const float aim = smoothAim(weapon);
    const float bob = std::min(moveSpeed / 5.0f, 1.5f) * (1.0f - 0.85f * aim) * viewBobScale();
    glm::vec3 offset = glm::mix(weapon.settings.viewOffset, weapon.settings.aimOffset, aim);
    offset.y -= lowered * 0.35f; // weapon switch
    offset.x += std::sin(time * 9.0f) * 0.012f * bob;
    offset.y += std::abs(std::cos(time * 9.0f)) * 0.012f * bob;
    offset.z += weapon.recoil() * (weapon.animated() ? 0.015f : 0.05f);
    // Animated arms play their own reload and swing clips; the rest is moved procedurally.
    const bool animated = weapon.animated();
    const float reload = weapon.reloading() && !animated ? std::sin(weapon.reloadProgress() * 3.14159265f) : 0.0f;
    offset.y -= reload * 0.12f;
    // Melee swing: a quick slash from right to left and forward, then back.
    const float swing = animated ? 0.0f : std::sin(weapon.swing() * 3.14159265f);
    offset.x -= swing * 0.16f;
    offset.z -= swing * 0.18f;
    offset.y += swing * 0.03f;

    glm::mat4 m = glm::translate(glm::mat4(1.0f), offset);
    m = glm::rotate(m, glm::radians(swing * 55.0f), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(weapon.recoil() * 5.0f + reload * 20.0f - swing * 25.0f), glm::vec3(1, 0, 0));
    // Turn the model so it points into the screen (glTF guns usually point +Z).
    const glm::vec3 r = weapon.settings.viewRotation + weapon.settings.aimRotation * aim;
    m = glm::rotate(m, glm::radians(r.y), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(r.x), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(r.z), glm::vec3(0, 0, 1));
    return glm::scale(m, glm::vec3(weapon.settings.viewScale));
}

// Script debugging from the editor (Play starts the game with --debug): commands come on
// stdin ("dbg break scripts/enemy.lua 42", "dbg continue", ...), a reader thread queues them;
// pauses go to stdout as "@dbg ..." lines (engine/ScriptDebug.h).
struct DebugChannel {
    bool on = false;
    std::mutex mutex;
    std::deque<std::string> lines;
    std::map<std::string, std::set<int>> breakpoints; // kept across levels

    void start()
    {
        on = true;
        std::thread([this] {
            for (std::string line; std::getline(std::cin, line);) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                std::lock_guard lock(mutex);
                lines.push_back(line);
            }
        }).detach(); // blocks in getline until the editor closes the pipe
    }
    std::optional<std::string> take()
    {
        std::lock_guard lock(mutex);
        if (lines.empty())
            return std::nullopt;
        std::string l = std::move(lines.front());
        lines.pop_front();
        return l;
    }
    // Breakpoint commands; returns the step command word for the others ("continue", "step"...).
    std::string handle(const std::string& line, engine::ScriptDebugger& debugger)
    {
        std::istringstream in(line);
        std::string tag, cmd, file;
        int at = 0;
        if (!(in >> tag >> cmd) || tag != "dbg")
            return {};
        if ((cmd == "break" || cmd == "clear") && in >> std::quoted(file) >> at) {
            const bool set = cmd == "break";
            const std::string key = engine::ScriptDebugger::fileKey(file);
            if (set)
                breakpoints[key].insert(at);
            else
                breakpoints[key].erase(at);
            debugger.setBreakpoint(key, at, set);
            return {};
        }
        if (cmd == "clearall") {
            breakpoints.clear();
            debugger.clearBreakpoints();
            return {};
        }
        if (cmd == "pause") {
            debugger.requestPause();
            return {};
        }
        return cmd;
    }
};
DebugChannel& debugChannel()
{
    static DebugChannel channel;
    return channel;
}

void setCaptured(GLFWwindow* window, bool captured)
{
    glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
}

GLFWwindow* createWindow()
{
    // Prefer 4.6, fall back to 4.5 (all features the engine uses are 4.5 core).
    for (int minor : {6, 5}) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_SAMPLES, 4);
        // FPS_ENGINE_GL_DEBUG=1: debug context, driver messages arrive synchronously (engine log, category "gl").
        if (const char* dbg = std::getenv("FPS_ENGINE_GL_DEBUG"); dbg && *dbg && *dbg != '0')
            glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
        if (GLFWwindow* w = glfwCreateWindow(1280, 720, "FPS Engine", nullptr, nullptr))
            return w;
    }
    return nullptr;
}

} // namespace

#ifdef _WIN32
// Laptops with two GPUs: ask the NVIDIA / AMD driver for the fast one.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char** argv)
{
    if (std::string missing; !engine::checkCpuFeatures(&missing)) {
        std::fprintf(stderr, "This build needs CPU features this processor lacks: %s.\n"
                             "Rebuild on this machine (a clean build dir with the bundled Jolt, ENGINE_USE_SYSTEM_JOLT=OFF).\n",
                     missing.c_str());
        return 1;
    }
    engine::openLogFile("game");
    engine::installCrashHandler("game");
    LOG_INFO("game", "Version %s, log file: %s", engine::engineVersion(), engine::logFilePath().string().c_str());
    // Assets: the folder next to the executable, then its .pak archives, then mods/ (folders and
    // .pak files) replacing files by path (engine/Vfs.h).
    for (const std::string& layer : engine::vfs().mountGame(std::filesystem::absolute(std::filesystem::path(argv[0]).parent_path())))
        LOG_INFO("vfs", "%s", layer.c_str());
    // Texts in the base language until the settings say which one (cvar language).
    if (std::string textError; !engine::localization().load(std::filesystem::path(argv[0]).parent_path() / "assets" / "text",
                                                            engine::Localization::kBase, &textError))
        LOG_WARN("text", "%s", textError.c_str());
    glfwSetErrorCallback([](int code, const char* message) { LOG_WARN("glfw", "%d: %s", code, message); });
    if (!glfwInit())
        return 1;

    GLFWwindow* window = createWindow();
    if (!window) {
        LOG_ERROR("render", "Could not create an OpenGL 4.5+ window");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // uncapped, to see real frame times

    engine::Renderer renderer;
    std::string error;
    if (!renderer.init(reinterpret_cast<engine::GlLoader>(glfwGetProcAddress), &error)) {
        LOG_ERROR("render", "%s", error.c_str());
        glfwTerminate();
        return 1;
    }
    // For comparing optimisations (with FPS_ENGINE_STATS=1).
    if (std::getenv("FPS_ENGINE_NO_INSTANCING") || std::getenv("FPS_ENGINE_NO_OCCLUSION") || std::getenv("FPS_ENGINE_NO_IMPOSTORS") ||
        std::getenv("FPS_ENGINE_NO_ASYNC_CULL")) {
        engine::RenderQuality q = renderer.quality();
        q.instancing = !std::getenv("FPS_ENGINE_NO_INSTANCING");
        if (std::getenv("FPS_ENGINE_NO_VOLUMETRIC"))
            q.volumetricFog = 0;
        q.impostors = !std::getenv("FPS_ENGINE_NO_IMPOSTORS");
        q.occlusionCulling = !std::getenv("FPS_ENGINE_NO_OCCLUSION");
        q.asyncCull = !std::getenv("FPS_ENGINE_NO_ASYNC_CULL");
        renderer.setQuality(q);
    }
    LOG_INFO("render", "%s", renderer.glInfo().c_str());
    engine::setCrashContext("gpu", renderer.glInfo());

    // Command line: [level] [--shaders DIR] (shaders edited in the editor replace the built-in ones).
    std::filesystem::path levelPath = std::filesystem::path(argv[0]).parent_path() / "assets/levels/test.level";
    std::vector<std::string> cvarArgs; // "+name value" pairs, applied once the variables exist
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.size() > 1 && arg[0] == '+' && i + 1 < argc) {
            cvarArgs.push_back(arg);
            cvarArgs.push_back(argv[++i]);
        } else if (arg == "--debug") {
            debugChannel().start(); // the editor's script debugger talks on stdin / stdout
        } else if (arg == "--shaders" && i + 1 < argc) {
            if (!renderer.reloadShaders(argv[++i], &error))
                LOG_WARN("shaders", "%s\n", error.c_str());
        } else if ((arg == "--record" || arg == "--play") && i + 1 < argc) {
            DemoState& demo = demoState();
            demo.file = argv[++i];
            if (arg == "--record") {
                demo.mode = DemoState::Record;
            } else if (std::string demoError; engine::loadDemo(demo.demo, demo.file, &demoError)) {
                demo.mode = DemoState::Play;
                engine::setGameSeed(demo.demo.seed); // the same dice as the recording
                demo.replayedSettings = true;
                for (const auto& [name, value] : demo.demo.cvars) {
                    cvarArgs.push_back("+" + name);
                    cvarArgs.push_back(value);
                }
            } else {
                LOG_ERROR("demo", "%s", demoError.c_str());
            }
        } else if (arg == "--benchmark") {
            demoState().benchmark = true;
        } else if (arg.rfind("--", 0) != 0) { // options like --no-menu are read elsewhere
            levelPath = arg;
        }
    }
    if (DemoState& demo = demoState(); demo.mode == DemoState::Play) {
        std::filesystem::path level(std::u8string(demo.demo.level.begin(), demo.demo.level.end()));
        levelPath = level.is_relative() ? std::filesystem::path(argv[0]).parent_path() / "assets" / level : level;
        LOG_INFO("demo", "playing %s: %zu frames, %.1f s, level %s, seed %u", demo.file.string().c_str(), demo.demo.frames.size(),
                 demo.demo.duration(), demo.demo.level.c_str(), demo.demo.seed);
    } else if (demo.mode == DemoState::Record) {
        // Relative to the assets folder the level is in (the game's or the editor's source copy),
        // so the demo replays on another machine; else the absolute path.
        const auto full = std::filesystem::absolute(levelPath).lexically_normal();
        std::filesystem::path stored = full;
        for (auto dir = full.parent_path(); !dir.empty() && dir != dir.root_path(); dir = dir.parent_path())
            if (dir.filename() == "assets") {
                stored = full.lexically_relative(dir);
                break;
            }
        const std::u8string level = stored.generic_u8string();
        demo.demo.level.assign(level.begin(), level.end());
        demo.demo.seed = engine::gameSeed();
    }

    // Level: command line argument, else the bundled sample, else built-in default.
    // It loads on a background thread (models in parallel) while the loading
    // screen shows the progress; then meshes and textures go to the GPU.
    engine::Ui2D ui; // loading screen, menus, HUD
    {
        const std::filesystem::path assetDir = std::filesystem::path(argv[0]).parent_path() / "assets";
        if (!ui.init(assetDir / "fonts/DejaVuSans.ttf", assetDir / "fonts/DejaVuSans-Bold.ttf", &error))
            LOG_WARN("ui", "Fonts: %s", error.c_str());
    }
    GameConsole console;
    GameMenu menu;
    GameHud hud;
    {
        const std::filesystem::path bin = std::filesystem::path(argv[0]).parent_path();
        menu.init(bin / "assets", bin / "licenses" / "THIRD_PARTY.md");
    }

    Mouse mouse;
    mouse.console = &console;
    mouse.menu = &menu;
    glfwSetWindowUserPointer(window, &mouse);
    glfwSetCharCallback(window, [](GLFWwindow* w, unsigned codepoint) {
        if (demoState().mode == DemoState::Play)
            return; // a replay ignores the keyboard
        if (auto* m = static_cast<Mouse*>(glfwGetWindowUserPointer(w)); m->console)
            m->console->onChar(codepoint);
    });
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int mods) {
        auto* m = static_cast<Mouse*>(glfwGetWindowUserPointer(w));
        if (!m->console || demoState().mode == DemoState::Play)
            return;
        if (m->menu && m->menu->capturingKey() && action == GLFW_PRESS && !m->console->open()) {
            m->menu->keyEvent(key);
            return;
        }
        if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS) { // ~ (key left of 1 on any layout)
            m->console->toggle();
            return;
        }
        m->console->onKey(key, action, mods);
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        auto* m = static_cast<Mouse*>(glfwGetWindowUserPointer(w));
        if (!m->first)
            m->delta += glm::vec2(static_cast<float>(x - m->lastX), static_cast<float>(y - m->lastY));
        m->first = false;
        m->lastX = x;
        m->lastY = y;
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
        auto* m = static_cast<Mouse*>(glfwGetWindowUserPointer(w));
        if (m->menu && m->menu->capturingKey() && action == GLFW_PRESS)
            m->menu->keyEvent(kMouseBase + button);
    });
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double y) {
        static_cast<Mouse*>(glfwGetWindowUserPointer(w))->scroll += y;
    });
    bool noMenu = false;
    for (int i = 1; i < argc; ++i)
        noMenu = noMenu || std::string(argv[i]) == "--no-menu";
    noMenu = noMenu || demoState().mode != DemoState::Off; // a demo starts in the level
    // FPS_ENGINE_FRAMES=N quits after N frames (used for smoke tests in CI).
    const char* frameLimitEnv = std::getenv("FPS_ENGINE_FRAMES");
    const long frameLimit = frameLimitEnv ? std::strtol(frameLimitEnv, nullptr, 10) : -1;
    long frame = 0;
    bool firstLevel = true;
    // Factions and the player's standing with them: one table for the whole game (goodwill
    // carries over level changes and is saved).
    engine::FactionTable factions = engine::defaultFactions();
    {
        std::string factionError;
        if (!engine::loadFactions(factions, std::filesystem::path(argv[0]).parent_path() / "assets" / "factions.cfg", &factionError))
            LOG_WARN("factions", "%s (built-in factions used)", factionError.c_str());
    }
    // Quests (assets/quests/*.quest) and the player's log: like the factions, one for the whole
    // game (progress carries over level changes and is saved).
    engine::Quests quests;
    {
        std::vector<std::string> questErrors;
        const int n = quests.loadFolder(std::filesystem::path(argv[0]).parent_path() / "assets" / "quests", &questErrors);
        for (const std::string& e : questErrors)
            LOG_WARN("quests", "%s", e.c_str());
        LOG_INFO("quests", "%d quests", n);
    }
    // Price list, weights and armor (assets/items.cfg); weapons have theirs in the .weapon files.
    engine::ItemCatalog items = engine::defaultItems();
    {
        std::string itemError;
        if (!engine::loadItems(items, std::filesystem::path(argv[0]).parent_path() / "assets" / "items.cfg", &itemError))
            LOG_WARN("items", "%s (built-in price list used)", itemError.c_str());
    }
    WindowMode windowMode;

    // One level from loading to leaving it (quit, restart, another level). Everything
    // level-scoped lives in here; the window, renderer, UI, console and menu outlive it.
    auto playLevel = [&](const std::filesystem::path& levelPath, const Carry* carry, Carry& carryOut, const SaveGame* loadFrom) -> LevelExit {
    LevelExit exitRequest; // set to leave the level (menu, console `map`, game.change_level)
    std::function<bool(const std::string&)> saveGame; // console `save` (set once the level runs)
    engine::setCrashContext("level", levelPath.string());
    engine::Scene scene;
    engine::PhysicsWorld physics;
    // Dead characters whose animation graph lists ragdoll bones: entity index -> ragdoll.
    std::map<std::size_t, engine::Ragdoll> ragdolls;
    std::map<std::size_t, std::pair<glm::vec3, glm::vec3>> killShots; // entity -> (point, impulse) of the shot that killed it
    MapHud map;
    std::string levelTitle = levelPath.stem().string();
    {
        const engine::LoadingScreen look = engine::readLoadingScreen(levelPath);
        int imageW = 0, imageH = 0;
        const unsigned background = look.image.empty() ? 0u : engine::Ui2D::loadImage(look.image, &imageW, &imageH);
        const std::string title = engine::levelText(levelPath.stem().string(), "title", look.title, levelPath.stem().string());
        const std::string hint = engine::levelText(levelPath.stem().string(), "hint", look.hint);
        levelTitle = title;
        engine::LoadProgress progress;
        std::atomic<bool> finished{false};
        std::string loadError;
        bool loaded = false;
        double sceneSeconds = 0.0, physicsSeconds = 0.0;
        std::thread loader([&] {
            ENGINE_THREAD_NAME("Level loader");
            // World streaming: far static models are read later, around where the player appears
            // (a save's position, else the level's start: NaN).
            const glm::vec3 streamFrom = loadFrom ? loadFrom->feet : glm::vec3(std::numeric_limits<float>::quiet_NaN());
            const double sceneStart = glfwGetTime();
            loaded = engine::loadScene(scene, levelPath, &loadError, &progress, &streamFrom);
            sceneSeconds = glfwGetTime() - sceneStart;
            if (!loaded)
                scene = engine::makeDefaultScene();
            engine::expandFlocks(scene); // a crow flock entity becomes its birds
            progress.setStep("physics");
            const double physicsStart = glfwGetTime();
            ragdolls.clear(); // setLevel removes their bodies
            killShots.clear();
            physics.setLevel(scene);
            physicsSeconds = glfwGetTime() - physicsStart;
            ++progress.done;
            finished = true;
        });
        const double loadStart = glfwGetTime();
        auto drawLoading = [&](float fraction, const std::string& step) {
            int w = 0, h = 0;
            glfwGetFramebufferSize(window, &w, &h);
            const float W = static_cast<float>(w), H = static_cast<float>(h);
            ui.begin(w, h);
            // Background: the level's image filling the screen (cropped, never stretched), darkened toward the bottom.
            ui.rect({0, 0}, {W, H}, {0.055f, 0.05f, 0.045f, 1.0f});
            if (background && imageW > 0 && imageH > 0) {
                const float screenAspect = W / H, imageAspect = static_cast<float>(imageW) / static_cast<float>(imageH);
                glm::vec4 uv(0, 0, 1, 1);
                if (imageAspect > screenAspect) {
                    const float cut = (1.0f - screenAspect / imageAspect) * 0.5f;
                    uv.x = cut;
                    uv.z = 1.0f - cut;
                } else {
                    const float cut = (1.0f - imageAspect / screenAspect) * 0.5f;
                    uv.y = cut;
                    uv.w = 1.0f - cut;
                }
                // Slow push-in so the screen is not frozen.
                const float zoom = 0.02f * static_cast<float>(std::min(glfwGetTime() - loadStart, 20.0) / 20.0);
                uv += glm::vec4(zoom, zoom, -zoom, -zoom) * glm::vec4(uv.z - uv.x, uv.w - uv.y, uv.z - uv.x, uv.w - uv.y);
                ui.image(background, {0, 0}, {W, H}, glm::vec4(0.85f, 0.82f, 0.76f, 1.0f), uv);
            }
            for (int i = 0; i < 24; ++i) { // bottom shade behind the text (bands touch, never overlap)
                const float t = static_cast<float>(i) / 24.0f;
                const float y0 = std::round(H * (0.55f + 0.45f * t)), y1 = std::round(H * (0.55f + 0.45f * (t + 1.0f / 24.0f)));
                ui.rect({0, y0}, {W, y1 - y0}, {0.03f, 0.028f, 0.025f, 0.15f + 0.75f * t});
            }
            const float margin = std::round(W * 0.06f);
            const float titleSize = std::round(std::clamp(H * 0.075f, 28.0f, 96.0f));
            const float textSize = std::round(std::clamp(H * 0.026f, 14.0f, 30.0f));
            const glm::vec4 bone(0.86f, 0.82f, 0.72f, 1.0f), dim(0.62f, 0.6f, 0.55f, 1.0f), rust(0.78f, 0.42f, 0.13f, 1.0f);
            const float barY = H - margin - textSize * 2.2f;
            const float barH = std::max(6.0f, std::round(H * 0.012f));
            ui.text(title, {margin, barY - titleSize * 1.55f - textSize * 1.4f}, titleSize, bone, true);
            if (!hint.empty())
                ui.text(hint, {margin, barY - textSize * 1.9f}, textSize, dim);
            // Progress bar: frame, fill and a moving highlight at its end.
            const float barW = W - 2.0f * margin;
            fraction = std::clamp(fraction, 0.0f, 1.0f);
            ui.rect({margin - 2, barY - 2}, {barW + 4, barH + 4}, {0.0f, 0.0f, 0.0f, 0.6f});
            ui.rect({margin, barY}, {barW, barH}, {0.16f, 0.15f, 0.13f, 0.9f});
            ui.rect({margin, barY}, {barW * fraction, barH}, rust);
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(glfwGetTime()) * 5.0f);
            ui.rect({margin + barW * fraction - 3, barY}, {3, barH}, {1.0f, 0.75f, 0.4f, 0.4f + 0.6f * pulse});
            char percent[16];
            std::snprintf(percent, sizeof percent, "%d %%", static_cast<int>(fraction * 100.0f + 0.5f));
            ui.text(percent, {margin + barW - ui.textWidth(percent, textSize, true), barY + barH + textSize * 0.5f}, textSize, bone, true);
            ui.text(engine::trf("loading.step", step), {margin, barY + barH + textSize * 0.5f}, textSize, dim);
            ui.end();
            glfwSwapBuffers(window);
            glfwPollEvents(); // the window stays responsive (moving, resizing)
        };
        while (!finished)
            drawLoading(0.85f * progress.fraction(), [&] { // engine steps by name, else a file name
                const std::string step = progress.step();
                return step == "level" ? engine::tr("loading.level") : step == "physics" ? engine::tr("loading.physics") : step;
            }());
        loader.join();
        if (!loaded)
            LOG_ERROR("level", "%s, using the built-in level", loadError.c_str());
        const double gpuStart = glfwGetTime();
        std::size_t cursor = 0;
        for (float uploaded = 0.0f; uploaded < 1.0f;) {
            uploaded = renderer.preload(scene, cursor, 12.0, scene.playerStart); // full textures near the start
            drawLoading(0.85f + 0.15f * uploaded, engine::tr("loading.textures"));
        }
        drawLoading(1.0f, engine::tr("loading.map"));
        const double mapStart = glfwGetTime();
        map.init(renderer, scene);
        const double now = glfwGetTime();
        LOG_INFO("level", "%s loaded in %.1f s (models %.1f, level %.1f, physics %.1f, GPU %.1f, map %.1f)",
                 levelPath.filename().string().c_str(), now - loadStart, progress.modelSeconds.load(),
                 sceneSeconds - progress.modelSeconds.load(), physicsSeconds, mapStart - gpuStart, now - mapStart);
#ifndef NDEBUG
        LOG_INFO("warning", "debug build (no optimisation), expect a much lower frame rate. Build with -DCMAKE_BUILD_TYPE=Release.");
#endif
        if (background)
            engine::Ui2D::deleteImage(background);
    }

    engine::FpsCamera camera;

    engine::PlayerController player;
    player.settings = scene.player; // movement and health come from the level (editor: Level > Player)
    player.spawn(physics, scene.playerStart);
    // World streaming: near cells load on workers as the player moves, far ones are released.
    engine::WorldStreamer streamer;
    streamer.reset(scene);
    streamer.loadAround(scene, &physics, scene.playerStart);

    // Audio: mixing runs on miniaudio's device thread, files decode on its job thread.
    GameAudio audio;
    audio.init(std::filesystem::path(argv[0]).parent_path() / "assets");
    // Subtitles (cl_subtitles): sounds with a caption that are close enough to be heard.
    audio.system().setCaptionListener([&hud, &audio](const std::string& caption, const glm::vec3* position, const engine::SoundDef& sound) {
        const engine::CVar* on = engine::CVarRegistry::global().find("cl_subtitles");
        if (!on || !on->boolean())
            return;
        if (position && glm::distance(*position, audio.system().listenerPosition()) > sound.maxDistance)
            return;
        hud.caption(engine::localization().localize(caption));
    });

    // Navigation mesh: built from the level collision in the background (tiles in
    // parallel); enemies walk straight lines until it is ready.
    engine::NavMesh navmesh;
    std::thread navBuilder;
    std::atomic<bool> navCancel{false};
    std::atomic<bool> navDone{!scene.nav.enabled};
    if (scene.nav.enabled) {
        // The builder reads its own copy of the level geometry (mesh data is shared, not
        // copied): the world streamer and dynamic props change scene.models meanwhile.
        auto navScene = std::make_shared<engine::Scene>();
        navScene->boxes = scene.boxes;
        navScene->models = scene.models;
        navScene->terrain = scene.terrain;
        navScene->nav = scene.nav;
        navBuilder = std::thread([&, navScene] {
            ENGINE_THREAD_NAME("Navmesh builder");
            // Models the streamer has not loaded yet (far cells) still shape the paths:
            // read their meshes here, once per file, and drop them after the build.
            std::map<std::filesystem::path, engine::PartList> meshes;
            for (engine::Model& m : navScene->models) {
                if (navCancel)
                    break;
                if (!m.deferred || !m.collision || m.sourcePath.empty())
                    continue;
                auto it = meshes.find(m.sourcePath);
                if (it == meshes.end()) {
                    engine::Model loaded;
                    std::string loadError;
                    if (!engine::loadModelFile(loaded, m.sourcePath, &loadError))
                        LOG_WARN("navmesh", "%s", loadError.c_str());
                    it = meshes.emplace(m.sourcePath, loaded.parts).first;
                }
                m.parts = it->second;
            }
            std::string navError;
            if (navmesh.build(*navScene, navScene->nav, &navError, nullptr, &navCancel))
                LOG_INFO("navmesh", "%d tiles (%d from the cache), %d polygons in %.1f s\n", navmesh.tileCount(),
                         navmesh.cachedTileCount(), navmesh.polygonCount(), navmesh.buildSeconds());
            else if (!navCancel) // leaving the level early is not a problem
                LOG_WARN("navmesh", "%s", navError.c_str());
            std::fflush(stdout);
            navDone = true;
        });
    }

    engine::ScriptSystem scripts(scene, physics, player);
    scripts.setNavMesh(&navmesh);
    scripts.setFactions(&factions);
    if (loadFrom)
        factions.goodwill = loadFrom->goodwill;
    else if (!carry)
        factions.goodwill.clear(); // a new game
    scripts.setQuests(&quests);
    if (loadFrom)
        quests.restore(loadFrom->quests);
    else if (!carry)
        quests.clearProgress();
    scripts.setPlayerHealth(scene.player.maxHealth);
    scripts.setSoundHandler([&audio](const std::string& sound, const glm::vec3* position, float volume) {
        audio.playAsset(sound, position, volume);
    });
    scripts.setMessageHandler([&hud](const std::string& text) { hud.notify(engine::localize(text)); }); // ShowMessage input, game.message()
    scripts.setDuckHandler([&audio](float gain, float seconds) {
        audio.system().duck(engine::AudioGroup::Ambient, gain, seconds, 2.0f);
        audio.system().duck(engine::AudioGroup::Music, gain, seconds, 2.0f);
    });
    // Sound occlusion: three rays from the ear to the source (straight and 0.5 m to each side
    // of it); each one a wall stops counts a third. Props and characters do not block.
    audio.system().setOcclusionTest([&physics](glm::vec3 ear, glm::vec3 source) {
        const engine::CVar* on = engine::CVarRegistry::global().find("snd_occlusion");
        if (on && !on->boolean())
            return 0.0f;
        const glm::vec3 d = source - ear;
        const float length = glm::length(d);
        if (length < 0.5f || length > 150.0f)
            return 0.0f;
        const glm::vec3 dir = d / length;
        glm::vec3 side = glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f));
        side = glm::dot(side, side) > 1e-6f ? glm::normalize(side) * 0.5f : glm::vec3(0.5f, 0.0f, 0.0f);
        float blocked = 0.0f;
        for (const glm::vec3 offset : {glm::vec3(0.0f), side, -side}) {
            const glm::vec3 to = source + offset;
            const glm::vec3 ray = to - ear;
            const float rayLength = glm::length(ray);
            if (auto hit = physics.raycast(ear, ray / rayLength, rayLength - 0.3f); hit && hit->model < 0)
                blocked += 1.0f / 3.0f;
        }
        return blocked;
    });

    // Weapons (credits of the models: CREDITS.md next to each model)
    Inventory inventory; // rounds by type, medkits, weapon files, money, armor
    inventory.money = items.moneyStart; // a new game; a save or the previous level replaces it
    std::vector<engine::Weapon> weapons = loadLoadout(scene, std::filesystem::path(argv[0]).parent_path(), inventory.weaponFiles);
    for (const engine::Weapon& w : weapons)
        inventory.addStartingAmmo(w);
    inventory.link(weapons);
    const std::filesystem::path assetRoot = std::filesystem::path(argv[0]).parent_path() / "assets";
    // Adds a carried weapon (pickup, save, level change) unless it is there already; returns its slot.
    auto addWeapon = [&](const std::filesystem::path& file) -> std::optional<std::size_t> {
        const std::filesystem::path key = std::filesystem::absolute(file).lexically_normal();
        if (auto it = std::find(inventory.weaponFiles.begin(), inventory.weaponFiles.end(), key); it != inventory.weaponFiles.end())
            return static_cast<std::size_t>(it - inventory.weaponFiles.begin());
        auto weapon = loadWeapon(key);
        if (!weapon)
            return std::nullopt;
        weapons.push_back(std::move(*weapon));
        inventory.weaponFiles.push_back(key);
        inventory.link(weapons);
        return weapons.size() - 1;
    };
    // The weapons, stock and magazines of a save or the previous level.
    auto restoreInventory = [&](const std::vector<std::filesystem::path>& files, const std::vector<int>& magazines,
                                const std::map<std::string, int>& stock, int medkits) {
        for (std::size_t i = 0; i < files.size(); ++i)
            if (!files[i].empty())
                if (auto slot = addWeapon(files[i]); slot && i < magazines.size())
                    weapons[*slot].setAmmo(magazines[i]);
        if (files.empty()) // older saves: magazines by slot
            for (std::size_t w = 0; w < weapons.size() && w < magazines.size(); ++w)
                weapons[w].setAmmo(magazines[w]);
        if (!stock.empty() || medkits > 0 || !files.empty()) {
            inventory.ammo = stock;
            inventory.medkits = medkits;
        }
        inventory.link(weapons);
    };
    std::size_t current = 0;
    std::size_t next = 0;     // weapon being switched to
    float lowered = 1.0f;     // 0 = ready, 1 = fully lowered (switch animation)
    std::array<bool, 9> slotHeld{};
    std::vector<Impact> impacts;
    int kills = 0;
    for (const engine::Weapon& w : weapons)
        for (const auto* s : {&w.settings.soundFire, &w.settings.soundReload, &w.settings.soundDraw, &w.settings.soundImpact,
                              &w.settings.soundHit, &w.settings.soundReloadEmpty, &w.settings.soundDryFire})
            audio.preload(*s);
    for (const auto* s : {&scene.player.footstepSound, &scene.player.landSound, &scene.player.hurtSound})
        audio.preload(*s);
    audio.startLevel(scene);
    GameEffects effects;
    effects.init(std::filesystem::path(argv[0]).parent_path() / "assets", audio.surfaces());
    effects.startLevel(scene);
    engine::WeatherDirector weatherDirector; // Environment > Weather cycle: clear, fog, rain, storm over time
    weatherDirector.start(scene.environment);
    GameWeather weather; // wet ground, lightning and thunder, roof over the player
    weather.startLevel(scene);
    scripts.setLightningHandler([&weather](float distance) { weather.strikeNow(distance); });
    // The Zone's anomaly storm (Environment > Anomaly storm): siren, red sky, then get under a roof.
    engine::AnomalyStorm anomalyStorm;
    anomalyStorm.start(scene.environment.anomalyStorm);
    anomalyStorm.publish(scene.environment.anomalyStormNow);
    float stormHurtTimer = 0.0f;
    std::string talkRequest; // console `talk <name>`: starts a talk with that character
    std::string tradeNameRequest; // console `trade <name>`: opens that character's trade
    std::function<void()> growArtifacts; // set once the artifacts exist (below)
    std::function<int()> artifactsLying;
    auto stormPhaseChanged = [&](engine::AnomalyStorm::Phase phase) {
        using P = engine::AnomalyStorm::Phase;
        const engine::AnomalyStormSettings& ss = scene.environment.anomalyStorm;
        if (phase == P::Warning) { // the siren wails, the Zone goes quiet, far rumbling
            hud.notify(engine::tr("storm.coming"));
            audio.playAsset("sounds/ambient/storm_siren.sound", nullptr, 1.0f);
            audio.system().duck(engine::AudioGroup::Ambient, 0.35f, std::max(2.0f, ss.warningSeconds), 6.0f);
            audio.system().duck(engine::AudioGroup::Music, 0.25f, std::max(2.0f, ss.warningSeconds + ss.durationSeconds), 6.0f);
            weather.strikeNow(2500.0f);
        } else if (phase == P::Storm) {
            hud.notify(engine::tr("storm.started"));
            weather.strikeNow(350.0f);
        } else if (phase == P::Fading) {
            hud.notify(engine::tr("storm.fading"));
        } else if (phase == P::Calm && growArtifacts) {
            growArtifacts(); // the storm leaves new artifacts in the anomalies
        }
        LOG_INFO("game", "Anomaly storm: %s", engine::stormPhaseName(phase));
    };
    float shelterTimer = 0.0f;
    engine::TaskGraph frameGraph; // per-frame update (see the main loop)
    bool sheltered = false;
    // Memory budget of the level (engine/Memory.h): measured every 2 s, a warning when a
    // category goes over its limit (once until it drops below again), console `memory`.
    // Hot reload: files saved in the editor (or an image editor) show up while playing.
    // The level's own assets folder is watched (the source tree when started from the editor).
    std::unique_ptr<engine::FileWatcher> watcher;
    float watchTimer = 0.0f;
    const std::filesystem::path watchRoot = [&] {
        for (std::filesystem::path p = std::filesystem::absolute(levelPath).parent_path(); !p.empty() && p != p.root_path(); p = p.parent_path())
            if (p.filename() == "assets")
                return p;
        return std::filesystem::path(argv[0]).parent_path() / "assets";
    }();
    auto hotReload = [&] {
        const std::vector<std::filesystem::path> changed = watcher->changes();
        if (changed.empty())
            return;
        std::vector<std::string> lines = engine::hotReload(changed, {&scene, &physics, &audio.system(), &scripts});
        for (const std::filesystem::path& f : changed) { // carried weapons: new settings, same magazine
            const std::filesystem::path key = std::filesystem::absolute(f).lexically_normal();
            for (std::size_t i = 0; i < inventory.weaponFiles.size() && i < weapons.size(); ++i) {
                if (inventory.weaponFiles[i] != key)
                    continue;
                if (auto w = loadWeapon(key)) {
                    const int ammo = weapons[i].ammo();
                    weapons[i] = std::move(*w);
                    weapons[i].setAmmo(ammo);
                    inventory.link(weapons);
                    lines.push_back("weapon " + key.filename().string());
                }
            }
        }
        for (const std::string& line : lines)
            LOG_INFO("reload", "%s", line.c_str());
        if (!lines.empty())
            hud.notify(lines.size() == 1 ? engine::trf("hud.reloaded", lines.front()) : engine::trn("hud.reloaded_files", static_cast<long>(lines.size())));
    };
    std::array<bool, engine::kMemoryCategories> overBudgetWarned{};
    float memoryTimer = 2.0f;
    auto measureMemory = [&] {
        engine::MemoryUsage u;
        const engine::Renderer::MemoryStats g = renderer.memoryStats();
        u.set(engine::MemoryCategory::GpuTextures, g.textureBytes);
        u.set(engine::MemoryCategory::GpuMeshes, g.meshBytes);
        u.set(engine::MemoryCategory::CpuMeshes, engine::sceneMemory(scene).meshBytes);
        u.set(engine::MemoryCategory::Audio, audio.system().memoryBytes());
        u.set(engine::MemoryCategory::Physics, physics.memoryBytes());
        u.set(engine::MemoryCategory::Lua, scripts.memoryBytes());
        u.set(engine::MemoryCategory::Particles, effects.memoryBytes());
        return u;
    };
    // Anomalies (gravity traps, discharges, vents, acid, rifts, radiation), detector and Geiger counter.
    GameAnomalies anomalies;
    anomalies.init(std::filesystem::path(argv[0]).parent_path() / "assets", audio);
    anomalies.startLevel(scene);
    // Artifacts grow in the anomalies (kinds in assets/items.cfg); a saved game replaces them.
    GameArtifacts artifacts;
    artifacts.init(std::filesystem::path(argv[0]).parent_path() / "assets", audio);
    artifacts.startLevel(anomalies.field(), items, physics, true);
    growArtifacts = [&] {
        if (const int n = artifacts.grow(anomalies.field(), items, physics))
            LOG_INFO("game", "Anomaly storm left %d new artifact(s)", n);
    };
    artifactsLying = [&] { return static_cast<int>(artifacts.lying().size()); };
    int artifactTarget = -1; // revealed artifact in reach (E picks it up)
    // Flashlight (F), settings from the level's player.
    Flashlight flashlight;
    flashlight.reset(scene.player);
    renderer.resetExposure(); // auto-exposure starts from this level's view
    bool flashlightHeld = false;
    bool healHeld = false;
    bool boltHeld = false;
    bool wasReloading = false;
    if (const int errors = scripts.start())
        LOG_ERROR("script", "%d script error(s), see above", errors);
    // A-life: squads far from the player live on as records (engine/ALife.h).
    engine::ALife alife;
    // Seeded by the level file name (not its path): the same zone life wherever the game is installed, and in demos.
    alife.start(scene, static_cast<std::uint32_t>(std::hash<std::string>{}(levelPath.filename().string())));
    const engine::ALife::Snap navSnap = [&navmesh](glm::vec3 p, float radius) -> std::optional<glm::vec3> {
        if (!navmesh.ready())
            return std::nullopt;
        return navmesh.nearestPoint(p, glm::vec3(radius, 6.0f, radius));
    };
    engine::FixedStepClock clock;
    engine::InputState input;

    // Console variables: defaults here, then the user's config, then +name value from the command line.
    engine::CVarRegistry& cvars = engine::CVarRegistry::global();
    const float baseSensitivity = camera.sensitivity;
    {
        using engine::CVarType;
        cvars.add("m_sensitivity", CVarType::Float, "1", "Mouse sensitivity multiplier", engine::CVarConfig, 0.05, 10.0);
        cvars.add("m_invert_y", CVarType::Bool, "0", "Invert vertical mouse look", engine::CVarConfig);
        cvars.add("fov", CVarType::Float, "90", "Vertical field of view (degrees, hip fire)", engine::CVarConfig, 50.0, 120.0);
        cvars.add("vsync", CVarType::Bool, "0", "Wait for the monitor refresh (no tearing, adds latency)", engine::CVarConfig)
            .onChange = [](const engine::CVar& v) { glfwSwapInterval(v.boolean() ? 1 : 0); };
        cvars.add("fps_max", CVarType::Int, "0", "Frame rate limit, 0 = none", engine::CVarConfig, 0.0, 1000.0);
        cvars.add("dev_hot_reload", CVarType::Bool, "1",
                  "Reload changed assets while playing (textures, materials, models, sounds, effects, .weapon, Lua)", engine::CVarConfig);
        cvars.add("r_quality", CVarType::Int, "2", "Graphics preset: 0 low, 1 medium, 2 high, 3 ultra", engine::CVarConfig, 0.0, 3.0)
            .onChange = [&renderer, &cvars](const engine::CVar& v) {
                engine::RenderQuality q = engine::RenderQuality::preset(v.integer());
                q.renderScale = cvars.find("r_scale") ? cvars.find("r_scale")->number() : 1.0f;
                // Comparing optimisations (FPS_ENGINE_STATS=1): the switches outlive the preset.
                q.instancing = q.instancing && !std::getenv("FPS_ENGINE_NO_INSTANCING");
                if (std::getenv("FPS_ENGINE_NO_VOLUMETRIC"))
                    q.volumetricFog = 0;
                q.impostors = q.impostors && !std::getenv("FPS_ENGINE_NO_IMPOSTORS");
                q.occlusionCulling = q.occlusionCulling && !std::getenv("FPS_ENGINE_NO_OCCLUSION");
                q.asyncCull = q.asyncCull && !std::getenv("FPS_ENGINE_NO_ASYNC_CULL");
                if (const engine::CVar* blur = cvars.find("r_motionblur"))
                    q.motionBlur = blur->number();
                if (const engine::CVar* aa = cvars.find("r_aa")) {
                    q.fxaa = aa->integer() == 1;
                    q.taa = aa->integer() == 2;
                }
                renderer.setQuality(q);
            };
        cvars.add("r_scale", CVarType::Float, "1", "Render resolution (0.5 = half; menus and HUD stay sharp)", engine::CVarConfig, 0.5, 1.0)
            .onChange = [&renderer](const engine::CVar& v) {
                engine::RenderQuality q = renderer.quality();
                q.renderScale = v.number();
                renderer.setQuality(q);
            };
        cvars.add("r_aa", CVarType::Int, "2", "Anti-aliasing: 0 off, 1 FXAA, 2 TAA (temporal, sharper on edges and foliage)",
                  engine::CVarConfig, 0.0, 2.0)
            .onChange = [&renderer](const engine::CVar& v) {
                engine::RenderQuality q = renderer.quality();
                q.fxaa = v.integer() == 1;
                q.taa = v.integer() == 2;
                renderer.setQuality(q);
            };
        cvars.add("r_motionblur", CVarType::Float, "0", "Camera motion blur: share of a frame's motion (0 = off)", engine::CVarConfig, 0.0, 1.0)
            .onChange = [&renderer](const engine::CVar& v) {
                engine::RenderQuality q = renderer.quality();
                q.motionBlur = v.number();
                renderer.setQuality(q);
            };
        auto windowChanged = [&](const engine::CVar&) {
            const int mode = cvars.find("r_fullscreen")->integer(), monitor = cvars.find("r_monitor")->integer();
            if (mode != windowMode.mode || monitor != windowMode.monitor)
                applyWindowMode(window, windowMode, mode, monitor);
        };
        cvars.add("r_fullscreen", CVarType::Int, "0", "0 window, 1 borderless, 2 fullscreen", engine::CVarConfig, 0.0, 2.0).onChange = windowChanged;
        cvars.add("r_monitor", CVarType::Int, "0", "Monitor for fullscreen (0 = primary)", engine::CVarConfig, 0.0, 7.0).onChange = windowChanged;
        cvars.add("r_width", CVarType::Int, "1280", "Window width (windowed)", engine::CVarConfig | engine::CVarDev, 320.0, 16384.0);
        cvars.add("r_height", CVarType::Int, "720", "Window height (windowed)", engine::CVarConfig | engine::CVarDev, 240.0, 16384.0);
        cvars.add("joy_sensitivity", CVarType::Float, "1", "Gamepad look speed", engine::CVarConfig, 0.1, 4.0);
        cvars.add("joy_deadzone", CVarType::Float, "0.15", "Gamepad stick dead zone", engine::CVarConfig, 0.0, 0.5);
        for (const Binding& b : kBindings)
            cvars.add(b.cvar, CVarType::String, b.key, b.label, engine::CVarConfig);
        auto volume = [&](const char* name, const char* description, std::function<void(float)> apply) {
            cvars.add(name, CVarType::Float, "1", description, engine::CVarConfig, 0.0, 1.0).onChange =
                [apply](const engine::CVar& v) { apply(v.number()); };
        };
        volume("snd_volume", "Master volume", [&audio](float v) { audio.system().setMasterVolume(v); });
        volume("snd_sfx", "Weapons, footsteps and effects volume", [&audio](float v) { audio.system().setGroupVolume(engine::AudioGroup::Sfx, v); });
        volume("snd_ambient", "Ambience and weather volume", [&audio](float v) { audio.system().setGroupVolume(engine::AudioGroup::Ambient, v); });
        volume("snd_music", "Music volume", [&audio](float v) { audio.system().setGroupVolume(engine::AudioGroup::Music, v); });
        cvars.add("snd_duck", CVarType::Float, "1", "How much gunfire ducks the ambience and music (0 = not at all)", engine::CVarConfig, 0.0, 1.0);
        cvars.add("snd_occlusion", CVarType::Bool, "1", "Walls muffle the sounds behind them", engine::CVarConfig);
        cvars.add("cl_showfps", CVarType::Bool, "0", "Frame rate in the corner", engine::CVarConfig);
        cvars.add("cl_viewbob", CVarType::Float, "1", "Weapon bob while walking (0 = off, against motion sickness)", engine::CVarConfig, 0.0, 1.0);
        cvars.add("cl_subtitles", CVarType::Bool, "0", "Captions for sounds and speech at the bottom of the screen", engine::CVarConfig);
        cvars.add("ui_scale", CVarType::Float, "1", "Size of texts and HUD (1 = normal)", engine::CVarConfig, 0.75, 1.75)
            .onChange = [](const engine::CVar& v) { uiScale() = std::clamp(static_cast<float>(v.number()), 0.75f, 1.75f); };
        cvars.add("language", CVarType::String, engine::Localization::kBase, "Language of the texts (assets/text/<language>.strings)",
                  engine::CVarConfig)
            .onChange = [textDir = std::filesystem::path(argv[0]).parent_path() / "assets" / "text"](const engine::CVar& v) {
                std::string error;
                if (!engine::localization().load(textDir, v.value, &error))
                    LOG_WARN("text", "%s", error.c_str());
            };
        cvars.addCommand("quit", "Leaves the game", [window](const std::vector<std::string>&) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return engine::ConsoleResult{true, "bye"};
        });
        cvars.addCommand("memory", "Memory in use by category against the level's budget (editor: Memory dock)",
                         [&](const std::vector<std::string>&) {
            const engine::MemoryUsage u = measureMemory();
            std::string text;
            for (int i = 0; i < engine::kMemoryCategories; ++i) {
                const auto c = static_cast<engine::MemoryCategory>(i);
                const double used = static_cast<double>(u.bytes[static_cast<std::size_t>(i)]) / (1024.0 * 1024.0);
                const float limit = scene.memoryBudget.limit(c);
                char line[128];
                std::snprintf(line, sizeof line, "%s%s %.1f / %.0f MB%s", text.empty() ? "" : ", ", engine::memoryCategoryKey(c), used,
                              static_cast<double>(limit), limit > 0.0f && used > static_cast<double>(limit) ? " OVER" : "");
                text += line;
            }
            return engine::ConsoleResult{true, text};
        });
        cvars.addCommand("streaming", "World streaming: cells loaded, meshes in memory", [&](const std::vector<std::string>&) {
            const engine::WorldStreamer::Stats s = streamer.stats();
            char buf[256];
            std::snprintf(buf, sizeof buf, "%s: %d/%d cells loaded (%d loading), %d models in memory, %.1f MB meshes, %d loads, %d releases%s",
                          scene.streaming.enabled ? "on" : "off", s.loadedCells, s.cells, s.loadingCells, s.residentModels,
                          static_cast<double>(s.meshBytes) / (1024.0 * 1024.0), s.loadsStarted, s.released,
                          s.overBudget ? ", over budget" : "");
            return engine::ConsoleResult{true, buf};
        });
        cvars.addCommand("pos", "Prints the player's position and view", [&player, &camera](const std::vector<std::string>&) {
            char buf[160];
            const glm::vec3 p = player.feet();
            std::snprintf(buf, sizeof buf, "feet %.2f %.2f %.2f, yaw %.1f, pitch %.1f", p.x, p.y, p.z, camera.yawDegrees, camera.pitchDegrees);
            return engine::ConsoleResult{true, buf};
        });
        cvars.addCommand("time", "time [HH[:MM] [minutes per day]]: shows or sets the time of day (turns the day/night cycle on)",
                         [&](const std::vector<std::string>& a) {
            engine::TimeOfDay& t = scene.environment.time;
            if (!a.empty()) {
                float hour = 0.0f, minute = 0.0f;
                if (std::sscanf(a[0].c_str(), "%f:%f", &hour, &minute) < 1)
                    return engine::ConsoleResult{false, "usage: time HH[:MM] [minutes per day]"};
                t.hour = std::fmod(std::fmod(hour + minute / 60.0f, 24.0f) + 24.0f, 24.0f);
                t.enabled = true;
                if (a.size() > 1)
                    t.dayMinutes = std::max(0.0f, static_cast<float>(std::atof(a[1].c_str())));
            }
            char text[8];
            return engine::ConsoleResult{true, std::string(t.enabled ? "time " : "time (cycle off) ") + engine::formatHour(t.hour, text) +
                                                   (t.dayMinutes > 0.0f ? ", day = " + std::to_string(static_cast<int>(t.dayMinutes)) + " min" : ", stopped")};
        });
        cvars.addCommand("weather", "weather [rain 0..1] [storm 0..1] | weather <kind> [seconds]: shows or sets rain and lightning "
                                    "(the ground is soaked at once), or changes to a kind of the weather cycle (Clear, Fog, Storm...)",
                         [&](const std::vector<std::string>& a) {
            engine::Environment& e = scene.environment;
            if (!a.empty() && !a[0].empty() && (std::isalpha(static_cast<unsigned char>(a[0][0])) != 0)) {
                const float seconds = a.size() > 1 ? static_cast<float>(std::atof(a[1].c_str())) : 0.0f;
                if (!weatherDirector.change(e, a[0], std::max(0.0f, seconds))) {
                    std::string kinds;
                    for (const engine::WeatherPreset& p : e.weather.presets.empty() ? engine::defaultWeatherPresets() : e.weather.presets)
                        kinds += (kinds.empty() ? "" : ", ") + p.name;
                    return engine::ConsoleResult{false, "no weather kind '" + a[0] + "' (" + kinds + ")"};
                }
                if (seconds <= 0.0f)
                    weather.setWetness(engine::environmentAt(e, scene.sunDirection).rain);
                return engine::ConsoleResult{true, "weather " + a[0]};
            }
            if (!a.empty()) {
                e.rain = std::clamp(static_cast<float>(std::atof(a[0].c_str())), 0.0f, 1.0f);
                for (engine::WeatherKey& k : e.time.keys) // keys would override it
                    k.rain = e.rain;
                weather.setWetness(e.rain);
            }
            if (a.size() > 1) {
                e.storm = std::clamp(static_cast<float>(std::atof(a[1].c_str())), 0.0f, 1.0f);
                for (engine::WeatherKey& k : e.time.keys)
                    k.storm = e.storm;
            }
            const engine::Environment now = engine::environmentAt(e, scene.sunDirection);
            if (e.weatherNow.amount > 0.0f)
                return engine::ConsoleResult{true, e.weatherNow.current + ": rain " + std::to_string(now.rain) + ", storm " +
                                                       std::to_string(now.storm) + ", fog " + std::to_string(now.fogDensity) +
                                                       ", wetness " + std::to_string(weather.wetness())};
            return engine::ConsoleResult{true, "rain " + std::to_string(e.rain) + ", storm " + std::to_string(e.storm) +
                                                   ", wetness " + std::to_string(weather.wetness())};
        });
        cvars.addCommand("lightning", "lightning [distance m]: a lightning strike now (thunder after distance / 343 s)",
                         [&](const std::vector<std::string>& a) {
            const float distance = a.empty() ? 800.0f : static_cast<float>(std::atof(a[0].c_str()));
            weather.strikeNow(distance);
            return engine::ConsoleResult{true, "lightning " + std::to_string(static_cast<int>(distance)) + " m"};
        });
        cvars.addCommand("quest", "quest: lists the quest log | quest start|done|fail|next <id> | quest stage <id> <stage>",
                         [&](const std::vector<std::string>& a) {
            if (a.empty()) {
                std::string out;
                for (const auto& [id, def] : quests.all())
                    out += id + " [" + engine::questStatusName(quests.status(id)) + "] " + quests.stage(id) + " - " + def.title + "\n";
                return engine::ConsoleResult{true, out.empty() ? std::string("no quests in assets/quests") : out};
            }
            const std::string id = a.size() > 1 ? a[1] : std::string();
            bool ok = false;
            if (a[0] == "start")
                ok = quests.start(id);
            else if (a[0] == "done")
                ok = quests.complete(id);
            else if (a[0] == "fail")
                ok = quests.fail(id);
            else if (a[0] == "next")
                ok = quests.advance(id);
            else if (a[0] == "stage" && a.size() > 2)
                ok = quests.setStage(id, a[2]);
            return engine::ConsoleResult{ok, ok ? a[0] + " " + id : "cannot " + a[0] + " " + id};
        });
        cvars.addCommand("talk", "talk <name>: talks to the character of that name (wherever it is)", [&](const std::vector<std::string>& a) {
            std::string name;
            for (const std::string& w : a)
                name += (name.empty() ? "" : " ") + w;
            talkRequest = name;
            return engine::ConsoleResult{!name.empty(), "talk " + name};
        });
        cvars.addCommand("trade", "trade <name>: opens the trade of that character (prop trade = 1)", [&](const std::vector<std::string>& a) {
            std::string name;
            for (const std::string& w : a)
                name += (name.empty() ? "" : " ") + w;
            tradeNameRequest = name;
            return engine::ConsoleResult{!name.empty(), "trade " + name};
        });
        cvars.addCommand("artifact", "artifact <id>: gives that artifact (assets/items.cfg) | artifact grow: grows new ones in the anomalies now | artifact: lists the lying ones",
                         [&](const std::vector<std::string>& a) {
            if (a.empty()) {
                std::string out = std::to_string(artifactsLying ? artifactsLying() : 0) + " lying";
                return engine::ConsoleResult{true, out};
            }
            if (a[0] == "grow") {
                if (growArtifacts)
                    growArtifacts();
                return engine::ConsoleResult{true, "grown"};
            }
            if (!items.find(engine::ItemDef::Kind::Artifact, a[0]))
                return engine::ConsoleResult{false, "no artifact " + a[0]};
            inventory.artifacts.push_back(a[0]);
            return engine::ConsoleResult{true, "artifact " + a[0]};
        });
        cvars.addCommand("money", "money [n]: shows or sets the player's roubles", [&](const std::vector<std::string>& a) {
            if (!a.empty())
                inventory.money = std::max(0, std::atoi(a[0].c_str()));
            return engine::ConsoleResult{true, std::to_string(inventory.money) + " RU"};
        });
        cvars.addCommand("storm", "storm [warning seconds]: an anomaly storm comes now | storm stop: ends it | without arguments: shows it",
                         [&](const std::vector<std::string>& a) {
            const engine::AnomalyStormSettings& s = scene.environment.anomalyStorm;
            if (!a.empty() && a[0] == "stop") {
                anomalyStorm.restore(static_cast<int>(engine::AnomalyStorm::Phase::Fading), 5.0f);
                return engine::ConsoleResult{true, "storm fading"};
            }
            if (!a.empty()) {
                // Through the request, like game.start_storm(): the siren and the warning come too.
                scene.environment.anomalyStormNow.request = std::max(0.0f, static_cast<float>(std::atof(a[0].c_str())));
                return engine::ConsoleResult{true, "storm warning " + a[0] + " s"};
            }
            char line[160];
            std::snprintf(line, sizeof line, "%s, %.0f s left, look %.2f, danger %.2f%s", engine::stormPhaseName(anomalyStorm.phase()),
                          anomalyStorm.secondsLeft(), anomalyStorm.look(), anomalyStorm.danger(), s.enabled ? "" : " (storms off in this level)");
            return engine::ConsoleResult{true, line};
        });
        cvars.addCommand("config_save", "Saves the settings now (also done on exit)", [&cvars](const std::vector<std::string>&) {
            const std::string file = (engine::userDirectory("config") / "game.cfg").string();
            const bool ok = cvars.saveConfig(file);
            return engine::ConsoleResult{ok, ok ? "saved " + file : "cannot write " + file};
        });
        if (firstLevel) {
            const std::filesystem::path config = engine::userDirectory("config") / "game.cfg";
            if (cvars.loadConfig(config.string()))
                LOG_INFO("cvar", "Settings from %s", config.string().c_str());
            for (const std::string& rest : cvars.applyCommandLine(cvarArgs))
                LOG_WARN("cvar", "Ignored command line argument %s", rest.c_str());
        }
        // Apply what the callbacks have not (values equal to the defaults do not fire onChange).
        if (firstLevel && cvars.find("language")->value != engine::localization().language())
            cvars.find("language")->onChange(*cvars.find("language"));
        if (firstLevel && cvars.find("r_fullscreen")->integer() == 0) // the remembered window size
            glfwSetWindowSize(window, cvars.find("r_width")->integer(), cvars.find("r_height")->integer());
        for (const char* name : {"vsync", "r_quality", "r_fullscreen", "snd_volume", "snd_sfx", "snd_ambient", "snd_music"})
            if (const engine::CVar* v = cvars.find(name); v && v->onChange)
                v->onChange(*v);
    }
    const engine::CVar& cvSensitivity = *cvars.find("m_sensitivity");
    const engine::CVar& cvInvert = *cvars.find("m_invert_y");
    const engine::CVar& cvFov = *cvars.find("fov");
    const engine::CVar& cvFpsMax = *cvars.find("fps_max");
    const engine::CVar& cvShowFps = *cvars.find("cl_showfps");

    // Cheats and debugging (need `cheats 1`): god, noclip, freecam, timescale, give, teleport; debug drawing.
    {
        using engine::CVarType;
        cvars.add("god", CVarType::Bool, "0", "No damage", engine::CVarCheat);
        cvars.add("noclip", CVarType::Bool, "0", "Fly through walls (look to steer, Space up, Ctrl down)", engine::CVarCheat)
            .onChange = [&player](const engine::CVar& v) { player.noclip = v.boolean(); };
        cvars.add("freecam", CVarType::Bool, "0", "Detach the camera from the player and fly it", engine::CVarCheat);
        cvars.add("timescale", CVarType::Float, "1", "Game speed (0.1 slow motion .. 4)", engine::CVarCheat, 0.05, 4.0);
        cvars.add("r_debugdraw", CVarType::Bool, "1", "Show debug lines, boxes and texts from code and scripts", engine::CVarDev);
        cvars.add("ai_debug", CVarType::Bool, "0", "Draw enemy states, paths and sight lines", engine::CVarDev);
        cvars.add("alife_debug", CVarType::Bool, "0", "Draw A-life squads (online green, offline grey), their targets and points", engine::CVarDev);
        cvars.addCommand("alife", "alife: lists the A-life squads (online / offline, members alive, target)", [&](const std::vector<std::string>&) {
            std::string out;
            for (const engine::ALife::Squad& s : alife.squads()) {
                char line[256];
                std::snprintf(line, sizeof line, "%s [%s] %s %d/%zu at %.0f %.0f -> %s%s\n", s.name.c_str(), s.faction.c_str(),
                              s.online ? "online" : "offline", s.living, s.members.size(), s.position.x, s.position.z,
                              s.target >= 0 ? alife.points()[static_cast<std::size_t>(s.target)].name.c_str() : "-",
                              s.roams ? (s.arrived ? " (waiting)" : " (travelling)") : "");
                out += line;
            }
            return engine::ConsoleResult{true, out.empty() ? std::string("no squads") : out};
        });
        cvars.add("io_debug", CVarType::Bool, "0", "Log entity I/O: outputs fired and inputs received", engine::CVarDev);
        auto levelFile = [&argv](const std::string& name) {
            std::filesystem::path file = name;
            if (file.extension().empty())
                file += ".level";
            if (file.is_relative())
                file = std::filesystem::path(argv[0]).parent_path() / "assets" / "levels" / file;
            return file;
        };
        cvars.addCommand("save", "save [slot]: saves the game (quick, slot1..slot3)", [&](const std::vector<std::string>& a) {
            const std::string slot = a.empty() ? "quick" : a[0];
            return engine::ConsoleResult{saveGame && saveGame(slot), "save " + slot};
        });
        cvars.addCommand("load", "load [slot]: loads a saved game (quick, auto, slot1..slot3)", [&](const std::vector<std::string>& a) {
            const std::string slot = a.empty() ? "quick" : a[0];
            if (!std::filesystem::exists(saveFile(slot)))
                return engine::ConsoleResult{false, "no save " + slot};
            exitRequest = {LevelExit::LoadSave, saveFile(slot)};
            return engine::ConsoleResult{true, "loading " + slot};
        });
        cvars.addCommand("map", "map NAME: loads assets/levels/NAME.level (without anything carried over)",
                         [&exitRequest, levelFile](const std::vector<std::string>& a) {
            if (a.empty())
                return engine::ConsoleResult{false, "usage: map NAME"};
            const std::filesystem::path file = levelFile(a[0]);
            if (!engine::fileExists(file))
                return engine::ConsoleResult{false, "no level " + file.string()};
            exitRequest = {LevelExit::Load, file};
            return engine::ConsoleResult{true, "loading " + file.filename().string()};
        });
        cvars.addCommand("changelevel", "changelevel NAME: goes to another level with health and weapons",
                         [&exitRequest, levelFile](const std::vector<std::string>& a) {
            if (a.empty())
                return engine::ConsoleResult{false, "usage: changelevel NAME"};
            const std::filesystem::path file = levelFile(a[0]);
            if (!engine::fileExists(file))
                return engine::ConsoleResult{false, "no level " + file.string()};
            exitRequest = {LevelExit::Change, file};
            return engine::ConsoleResult{true, "changing to " + file.filename().string()};
        });
        cvars.addCommand("flashlight", "flashlight [0|1]: switch the flashlight", [&](const std::vector<std::string>& a) {
            if (!scene.player.flashlight)
                return engine::ConsoleResult{false, "no flashlight in this level"};
            if (a.empty())
                flashlight.toggle();
            else
                flashlight.setOn(a[0] != "0");
            return engine::ConsoleResult{true, flashlight.on() ? "flashlight on" : "flashlight off"};
        });
        cvars.addCommand("give", "give health|ammo|medkit|all", [&](const std::vector<std::string>& a) {
            const std::string what = a.empty() ? "all" : a[0];
            if (what == "health" || what == "all")
                scripts.setPlayerHealth(scene.player.maxHealth);
            if (what == "ammo" || what == "all") {
                for (engine::Weapon& w : weapons) {
                    w.refill();
                    if (!w.settings.ammoType.empty())
                        inventory.ammo[w.settings.ammoType] = std::max(inventory.ammo[w.settings.ammoType], w.settings.startReserve * 2);
                }
            }
            if (what == "medkit" || what == "all")
                inventory.medkits += 3;
            if (what != "health" && what != "ammo" && what != "medkit" && what != "all")
                return engine::ConsoleResult{false, "usage: give health|ammo|medkit|all"};
            return engine::ConsoleResult{true, "gave " + what};
        }, engine::CVarCheat);
        cvars.addCommand("kill", "kill [radius m]: kills the characters (NPCs, enemies, mutants) within the radius (default 15 m), e.g. to look at ragdolls",
                         [&](const std::vector<std::string>& a) {
            float radius = 15.0f;
            try {
                if (!a.empty())
                    radius = std::stof(a[0]);
            } catch (...) {
                return engine::ConsoleResult{false, "usage: kill [radius]"};
            }
            int killed = 0;
            for (std::size_t i = 0; i < scene.entities.size(); ++i) {
                const engine::Entity& e = scene.entities[i];
                if (e.alive && !e.helper() && !engine::entityFaction(e).empty() && glm::distance(e.position, player.feet()) <= radius) {
                    scripts.damageEntity(static_cast<int>(i), 100000.0f);
                    ++killed;
                }
            }
            return engine::ConsoleResult{true, "killed " + std::to_string(killed)};
        }, engine::CVarCheat);
        cvars.addCommand("teleport", "teleport X Y Z (feet), or teleport NAME (an entity or object), or teleport (to where you look)",
                         [&](const std::vector<std::string>& a) {
            glm::vec3 target;
            if (a.size() >= 3) {
                try {
                    target = {std::stof(a[0]), std::stof(a[1]), std::stof(a[2])};
                } catch (...) {
                    return engine::ConsoleResult{false, "usage: teleport X Y Z"};
                }
            } else if (a.size() == 1) {
                bool found = false;
                for (const engine::Entity& e : scene.entities)
                    if (e.name == a[0]) {
                        target = e.position + glm::vec3(1.0f, 0.0f, 0.0f);
                        found = true;
                        break;
                    }
                if (!found)
                    return engine::ConsoleResult{false, "no entity named " + a[0]};
            } else {
                const auto hit = physics.raycast(camera.position, camera.forward(), 500.0f);
                if (!hit)
                    return engine::ConsoleResult{false, "nothing in sight"};
                target = hit->point + hit->normal * 0.3f;
            }
            player.teleport(target);
            streamer.loadAround(scene, &physics, target);
            char buf[96];
            std::snprintf(buf, sizeof buf, "teleported to %.1f %.1f %.1f", target.x, target.y, target.z);
            return engine::ConsoleResult{true, buf};
        }, engine::CVarCheat);
    }
    const engine::CVar& cvGod = *cvars.find("god");
    const engine::CVar& cvFreecam = *cvars.find("freecam");
    const engine::CVar& cvTimescale = *cvars.find("timescale");
    const engine::CVar& cvDebugDraw = *cvars.find("r_debugdraw");
    const engine::CVar& cvAiDebug = *cvars.find("ai_debug");
    glm::vec3 freecamPosition{0.0f};
    bool freecamActive = false;
    menu.setTitle(levelTitle, engine::tr("menu.subtitle"));
    // The game starts in the main menu; test runs (FPS_ENGINE_FRAMES) and --no-menu go straight in.
    // Later levels (restart, level change) start playing at once.
    if (!firstLevel || std::getenv("FPS_ENGINE_FRAMES") || noMenu) {
        menu.setStarted(true);
        menu.close();
    } else {
        menu.open(GameMenu::Screen::Main);
    }
    bool captured = !menu.active();
    setCaptured(window, captured);
    // Capturing moves the cursor to the window centre: that jump is not a look movement
    // (the camera turned at the start of a level, wherever the pointer was).
    mouse.first = true;
    mouse.delta = glm::vec2(0.0f);

    // Test helpers: FPS_ENGINE_AUTOFIRE=1 holds the trigger, FPS_ENGINE_SCREENSHOT=file.ppm saves the last frame.
    const bool autoFire = std::getenv("FPS_ENGINE_AUTOFIRE") != nullptr;
    const char* screenshotFile = std::getenv("FPS_ENGINE_SCREENSHOT");
    // Saved game: the player, weapons and every scripted entity as they were.
    auto makeSave = [&] {
        SaveGame save;
        { // relative to the game folder when inside it, so saves survive moving the game
            const std::filesystem::path exeDir = std::filesystem::absolute(std::filesystem::path(argv[0]).parent_path());
            const std::filesystem::path rel = std::filesystem::absolute(levelPath).lexically_normal().lexically_relative(exeDir.lexically_normal());
            save.level = !rel.empty() && *rel.begin() != ".." ? rel : std::filesystem::absolute(levelPath);
        }
        save.title = levelTitle;
        const std::time_t t = std::time(nullptr);
        char when[32];
        std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", std::localtime(&t));
        save.created = when;
        save.playTime = scripts.time();
        save.feet = player.feet();
        save.yaw = camera.yawDegrees;
        save.pitch = camera.pitchDegrees;
        save.health = scripts.playerHealth();
        save.dose = anomalies.dose();
        save.weapon = current;
        for (const engine::Weapon& w : weapons)
            save.ammo.push_back(w.ammo());
        save.kills = kills;
        save.goodwill = factions.goodwill;
        save.flashlightOn = flashlight.on();
        for (const engine::Model& m : scene.models)
            if (m.dynamic)
                save.props.push_back({m.id, m.position, m.rotation});
        save.flashlightCharge = flashlight.charge();
        { // weapon files relative to the game folder too
            const std::filesystem::path exeDir = std::filesystem::absolute(std::filesystem::path(argv[0]).parent_path()).lexically_normal();
            for (const std::filesystem::path& f : inventory.weaponFiles) {
                const std::filesystem::path rel = f.lexically_relative(exeDir);
                save.weaponFiles.push_back(!f.empty() && !rel.empty() && *rel.begin() != ".." ? rel : f);
            }
        }
        save.stock = inventory.ammo;
        save.medkits = inventory.medkits;
        save.money = inventory.money;
        for (const engine::Weapon& w : weapons)
            save.weaponCondition.push_back(w.condition);
        save.armor = inventory.armor;
        save.armorCondition = inventory.armorCondition;
        save.artifacts = inventory.artifacts;
        save.artifactsSaved = true;
        for (const GameArtifacts::Lying& l : artifacts.lying())
            save.lyingArtifacts.push_back({l.id, l.position, l.anomaly});
        for (const engine::Entity& e : scene.entities) // traders' goods as left after trading
            if (const auto t = e.properties.find("trade"); t != e.properties.end() && std::atof(t->second.c_str()) >= 0.5)
                if (const auto st = e.properties.find("stock"); st != e.properties.end())
                    save.traders.push_back(std::string("\"") + e.name + "\" \"" + st->second + "\"");
        save.timeOfDay = scene.environment.time.hour;
        save.dayMinutes = scene.environment.time.dayMinutes;
        save.timeEnabled = scene.environment.time.enabled;
        save.wetness = weather.wetness();
        save.weather = scene.environment.weatherNow.amount > 0.0f ? scene.environment.weatherNow.current : std::string();
        save.weatherHold = weatherDirector.holdSeconds();
        save.entities = scripts.saveEntities();
        save.pendingInputs = scripts.savePendingInputs();
        save.squads = alife.save();
        save.quests = quests.save();
        save.stormPhase = anomalyStorm.phaseIndex();
        save.stormSeconds = anomalyStorm.secondsLeft();
        return save;
    };
    auto saveTo = [&](const std::string& slot) {
        std::string saveError;
        if (scripts.playerHealth() <= 0.0f) {
            hud.notify(engine::tr("save.dead"));
            return false;
        }
        if (!makeSave().write(saveFile(slot), &saveError)) {
            LOG_ERROR("save", "%s", saveError.c_str());
            hud.notify(engine::tr("save.failed"));
            return false;
        }
        LOG_INFO("save", "Saved %s", saveFile(slot).string().c_str());
        hud.notify(slot == "auto" ? engine::tr("save.autosaved") : engine::trf("save.saved", slotLabel(slot)));
        return true;
    };
    if (loadFrom) {
        player.teleport(loadFrom->feet);
        streamer.loadAround(scene, &physics, loadFrom->feet);
        camera.yawDegrees = loadFrom->yaw;
        camera.pitchDegrees = loadFrom->pitch;
        scripts.setPlayerHealth(loadFrom->health);
        anomalies.setDose(loadFrom->dose);
        flashlight.setCharge(loadFrom->flashlightCharge);
        if (loadFrom->timeOfDay >= 0.0f) {
            scene.environment.time.hour = loadFrom->timeOfDay;
            scene.environment.time.dayMinutes = loadFrom->dayMinutes;
            scene.environment.time.enabled = loadFrom->timeEnabled;
        }
        weather.setWetness(loadFrom->wetness);
        if (!loadFrom->weather.empty() && weatherDirector.change(scene.environment, loadFrom->weather, 0.0f))
            weatherDirector.setHoldSeconds(loadFrom->weatherHold);
        if (!loadFrom->props.empty()) { // physics props where they were left
            for (const SaveGame::Prop& p : loadFrom->props)
                if (const int object = engine::findObjectById(scene, p.id); scene.isModel(object)) {
                    scene.model(object).position = p.position;
                    scene.model(object).rotation = p.rotation;
                }
            physics.resetDynamic(scene);
        }
        flashlight.setOn(loadFrom->flashlightOn);
        std::vector<std::filesystem::path> savedWeapons;
        for (const std::filesystem::path& f : loadFrom->weaponFiles)
            savedWeapons.push_back(f.is_relative() && !f.empty() ? std::filesystem::path(argv[0]).parent_path() / f : f);
        restoreInventory(savedWeapons, loadFrom->ammo, loadFrom->stock, loadFrom->medkits);
        if (loadFrom->money >= 0)
            inventory.money = loadFrom->money;
        for (std::size_t i = 0; i < weapons.size() && i < loadFrom->weaponCondition.size(); ++i)
            weapons[i].condition = loadFrom->weaponCondition[i];
        inventory.armor = loadFrom->armor;
        inventory.armorCondition = loadFrom->armorCondition;
        inventory.artifacts = loadFrom->artifacts;
        if (loadFrom->artifactsSaved) {
            std::vector<GameArtifacts::Lying> lying;
            for (const SaveGame::LyingArtifact& a : loadFrom->lyingArtifacts)
                lying.push_back({a.id, a.position, a.anomaly, static_cast<float>(lying.size()) * 1.7f});
            artifacts.setLying(std::move(lying));
        }
        for (const std::string& line : loadFrom->traders) {
            std::istringstream ls(line);
            std::string name, stock;
            if (ls >> std::quoted(name) >> std::quoted(stock))
                for (int t : engine::findEntitiesByName(scene, name))
                    scene.entities[static_cast<std::size_t>(t)].properties["stock"] = stock;
        }
        if (loadFrom->weapon < weapons.size()) {
            current = next = loadFrom->weapon;
            weapons[current].drawn();
        }
        kills = loadFrom->kills;
        scripts.setTime(loadFrom->playTime);
        scripts.restoreEntities(loadFrom->entities);
        scripts.restorePendingInputs(loadFrom->pendingInputs); // also drops the OnStart inputs of a fresh start
        alife.restore(loadFrom->squads);
        if (loadFrom->stormPhase >= 0) {
            anomalyStorm.restore(loadFrom->stormPhase, loadFrom->stormSeconds);
            anomalyStorm.update(scene.environment.anomalyStorm, 0.0f);
            anomalyStorm.publish(scene.environment.anomalyStormNow);
        }
        hud.notify(engine::tr("save.loaded"));
    }
    // Carried over from the previous level (level change): health, weapon, magazines.
    if (carry) {
        scripts.setPlayerHealth(std::min(carry->health, scene.player.maxHealth));
        restoreInventory(carry->weaponFiles, carry->ammo, carry->stock, carry->medkits);
        inventory.money = carry->money;
        for (std::size_t i = 0; i < weapons.size() && i < carry->condition.size(); ++i)
            weapons[i].condition = carry->condition[i];
        inventory.armor = carry->armor;
        inventory.armorCondition = carry->armorCondition;
        inventory.artifacts = carry->artifacts;
        if (carry->weapon < weapons.size()) {
            current = next = carry->weapon;
            weapons[current].drawn();
        }
        saveTo("auto"); // arriving in a new level
    }
    saveGame = saveTo;
    if (firstLevel) { // test hooks
        if (const char* pos = std::getenv("FPS_ENGINE_POS")) { // "x y z" feet position (test hook)
            glm::vec3 p(0.0f);
            if (std::sscanf(pos, "%f %f %f", &p.x, &p.y, &p.z) == 3) {
                streamer.loadAround(scene, &physics, p);
                player.spawn(physics, p);
            }
        }
        if (const char* crash = std::getenv("FPS_ENGINE_CRASH")) { // test hook: "segv" or "check" checks the crash report
            if (std::string(crash) == "check")
                ENGINE_CHECK(false, "FPS_ENGINE_CRASH test");
            volatile int* nowhere = nullptr;
            *nowhere = 1;
        }
        if (const char* line = std::getenv("FPS_ENGINE_CONSOLE")) { // console lines separated by ";" (test hook)
            console.toggle();
            std::string all = line, part; // several lines separated by ';'
            for (std::size_t start = 0; start <= all.size();) {
                const std::size_t end = all.find(';', start);
                part = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
                console.submitLine(part);
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            if (const char* keep = std::getenv("FPS_ENGINE_CONSOLE_OPEN"); !keep || *keep == '0')
                console.toggle(); // closed again unless the screenshot should show it
        }
        if (const char* screen = std::getenv("FPS_ENGINE_MENU")) { // test hook: open a menu screen (screenshots)
            const std::string name = screen;
            menu.setStarted(name != "main");
            if (name == "controls")
                menu.setSettingsPage(1);
            menu.open(name == "main" ? GameMenu::Screen::Main : name == "settings" || name == "controls" ? GameMenu::Screen::Settings
                      : name == "credits" ? GameMenu::Screen::Credits : name == "death" ? GameMenu::Screen::Death : name == "levels" ? GameMenu::Screen::Levels
                      : GameMenu::Screen::Pause);
        }
        if (const char* slot = std::getenv("FPS_ENGINE_SLOT")) { // weapon slot 1..9 to start with (test hook)
            const long n = std::strtol(slot, nullptr, 10);
            if (n >= 1 && static_cast<std::size_t>(n) <= weapons.size()) {
                current = next = static_cast<std::size_t>(n - 1);
                weapons[current].drawn();
            }
        }
        if (const char* look = std::getenv("FPS_ENGINE_LOOK")) // "yaw pitch" in degrees
            std::sscanf(look, "%f %f", &camera.yawDegrees, &camera.pitchDegrees);
    }
    if (std::getenv("FPS_ENGINE_BAKE_PROBES")) { // test hook: bake the level's probes headless, then play
        engine::ProbeBaker baker(scene);
        while (!baker.finished())
            baker.step(renderer, scene, 1000.0);
        std::string bakeError;
        if (engine::saveProbes(baker.result(), engine::probeFile(levelPath), &bakeError)) {
            scene.probes = std::make_shared<engine::ProbeSet>(baker.result());
            LOG_INFO("game", "baked %d probe captures into %s", baker.total(), engine::probeFile(levelPath).string().c_str());
        } else {
            LOG_ERROR("game", "probe bake: %s", bakeError.c_str());
        }
    }

    // Test hook FPS_ENGINE_FIXED_DT (seconds): a virtual clock from 0 with equal frames, so a test
    // run (golden images) shows the same frame whatever the machine's speed.
    const double fixedDt = std::getenv("FPS_ENGINE_FIXED_DT") ? std::strtod(std::getenv("FPS_ENGINE_FIXED_DT"), nullptr) : 0.0;
    double lastTime = fixedDt > 0.0 ? 0.0 : glfwGetTime();
    if (DebugChannel& dbg = debugChannel(); dbg.on) {
        // Breakpoints from the editor; a stop blocks the script (and the game) until it says how to go on.
        for (const auto& [file, lines] : dbg.breakpoints)
            for (int l : lines)
                scripts.debugger().setBreakpoint(file, l, true);
        scripts.debugger().setPauseHandler([&, window](const engine::ScriptDebugPause& p) {
            for (const std::string& line : engine::ScriptDebugger::pauseLines(p))
                std::printf("%s\n", line.c_str());
            std::fflush(stdout);
            setCaptured(window, false); // the mouse is free to go to the editor
            glfwSetWindowTitle(window, ("Skript zastaven: " + p.file + ":" + std::to_string(p.line) + " (pokračuj v editoru)").c_str());
            const double pausedAt = glfwGetTime();
            engine::ScriptDebugAction action = engine::ScriptDebugAction::Continue;
            for (bool waiting = true; waiting && !glfwWindowShouldClose(window);) {
                glfwWaitEventsTimeout(0.02); // the window stays alive while the script waits
                while (auto line = dbg.take()) {
                    const std::string cmd = dbg.handle(*line, scripts.debugger());
                    if (cmd == "continue")
                        action = engine::ScriptDebugAction::Continue, waiting = false;
                    else if (cmd == "step")
                        action = engine::ScriptDebugAction::StepInto, waiting = false;
                    else if (cmd == "next")
                        action = engine::ScriptDebugAction::StepOver, waiting = false;
                    else if (cmd == "out")
                        action = engine::ScriptDebugAction::StepOut, waiting = false;
                }
            }
            std::printf("@dbg resumed\n");
            std::fflush(stdout);
            glfwSetWindowTitle(window, "FPS Engine");
            lastTime += glfwGetTime() - pausedAt; // the game does not catch up on the time it stood
            setCaptured(window, captured);
            mouse.first = true; // the pointer went to the editor and back: no turn
            mouse.delta = glm::vec2(0.0f);
            return action;
        });
    }
    renderer.setDeterministic(fixedDt > 0.0);
    double fpsTimer = 0.0;
    int fpsFrames = 0;
    bool escHeld = false;
    bool padStartHeld = false, padWeaponHeld = false;
    bool quickSaveHeld = false, quickLoadHeld = false;
    bool mapOpen = std::getenv("FPS_ENGINE_MAP") != nullptr, mapHeld = false; // test hook: start with the PDA map open
    // Talking (E at a character with a "dialog" property): the game pauses, answers by 1-9 or mouse.
    GameDialog talk;
    GameTrade trade; // trade = 1 characters: buy, sell, repair
    int tradeRequest = -1; // a dialog's `trade` action opens it after the talk
    std::vector<char> jammed; // per weapon slot: jammed until R clears it
    float unjamTimer = 0.0f;
    bool overloadedShown = false;
    GameTrade::Hooks tradeHooks;
    tradeHooks.addWeapon = [&](const std::filesystem::path& file) -> std::optional<std::size_t> {
        const std::size_t before = weapons.size();
        auto slot = addWeapon(file);
        if (slot && weapons.size() > before)
            weapons[*slot].condition = 1.0f;
        return slot;
    };
    tradeHooks.removeWeapon = [&](std::size_t slot) {
        if (slot >= weapons.size() || slot == current)
            return;
        weapons.erase(weapons.begin() + static_cast<std::ptrdiff_t>(slot));
        inventory.weaponFiles.erase(inventory.weaponFiles.begin() + static_cast<std::ptrdiff_t>(slot));
        if (slot < jammed.size())
            jammed.erase(jammed.begin() + static_cast<std::ptrdiff_t>(slot));
        if (current > slot)
            --current;
        next = current;
        inventory.link(weapons);
    };
    tradeHooks.sound = [&](const std::string& s) { audio.playAsset(s, nullptr, 0.8f); };
    bool useHeld = false, talkClickHeld = false;
    std::array<bool, 9> digitHeld{};
    int talkHover = -1, useTarget = -1;
    // Rewards and dialog "give": medkits n | ammo <type> n | goodwill <faction> n; returns a short text.
    auto giveReward = [&](const std::vector<std::string>& w) -> std::string {
        auto num = [&](std::size_t i) { return i < w.size() ? static_cast<int>(std::lround(std::atof(w[i].c_str()))) : 0; };
        if (w.empty())
            return {};
        if (w[0] == "money") {
            inventory.money += num(1);
            return "+" + std::to_string(num(1)) + " RU";
        }
        if (w[0] == "medkits" || w[0] == "medkit") {
            inventory.medkits += num(1);
            return engine::trf("reward.medkits", std::to_string(num(1)));
        }
        if (w[0] == "ammo" && w.size() >= 3) {
            inventory.ammo[w[1]] += num(2);
            inventory.link(weapons);
            return engine::trf("reward.ammo", std::to_string(num(2)), w[1]);
        }
        if (w[0] == "goodwill" && w.size() >= 3) {
            factions.changeGoodwill(w[1], static_cast<float>(num(2)));
            const engine::Faction* f = factions.find(w[1]);
            return (num(2) >= 0 ? "+" : "") + std::to_string(num(2)) + " reputace " + (f ? f->name : w[1]);
        }
        LOG_WARN("quests", "unknown reward %s", w[0].c_str());
        return {};
    };
    auto runTalkActions = [&](const std::vector<engine::DialogAction>& actions) {
        for (const engine::DialogAction& a : actions) {
            const std::vector<std::string>& w = a.words;
            if (w[0] == "give") {
                const std::string got = giveReward(std::vector<std::string>(w.begin() + 1, w.end()));
                if (!got.empty())
                    hud.notify(got);
            } else if (w[0] == "fire" && w.size() >= 3) {
                scripts.sendInput(w[1], w[2], w.size() > 3 ? w[3] : std::string(), 0.0f, -1, talk.entity());
            } else if (w[0] == "hostile" && talk.entity() >= 0) {
                scene.entities[static_cast<std::size_t>(talk.entity())].properties["grudge"] = "1";
                talk.end();
            } else if (w[0] == "trade" && talk.entity() >= 0) {
                tradeRequest = talk.entity();
                talk.end();
            } else if (w[0] == "give" && w.size() >= 3 && w[1] == "money") {
                inventory.money += static_cast<int>(std::atof(w[2].c_str()));
            }
        }
    };

    if (demoState().mode != DemoState::Off) {
        // Enemies walk straight lines until the navmesh is ready; when that happens depends on the
        // machine, so a demo starts only with it (the window keeps answering meanwhile).
        while (!navDone && !glfwWindowShouldClose(window)) {
            glfwPollEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        lastTime = fixedDt > 0.0 ? 0.0 : glfwGetTime();
    }
    if (DemoState& demo = demoState(); demo.mode == DemoState::Record) {
        for (const char* name : kDemoCVars)
            if (const engine::CVar* v = cvars.find(name))
                demo.demo.cvars[name] = v->value;
        LOG_INFO("demo", "recording to %s (level %s, seed %u)", demo.file.string().c_str(), demo.demo.level.c_str(), demo.demo.seed);
    }
    // The end of a demo replay: frame statistics (benchmark), then quit or hand over to the player.
    auto finishDemo = [&](bool complete) {
        DemoState& demo = demoState();
        const engine::FrameStats st = engine::frameStats(demo.realFrames);
        std::string result = engine::formatFrameStats(st);
        if (!complete)
            result += " (stopped early)";
        result += demo.desyncFrame < 0 ? " | in sync"
                                       : " | desync at frame " + std::to_string(demo.desyncFrame) + " (" +
                                             std::to_string(demo.desyncDistance) + " m off)";
        LOG_INFO("demo", "%s: %s", demo.file.filename().string().c_str(), result.c_str());
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        std::printf("@bench %s | quality %s | %dx%d\n", result.c_str(),
                    cvars.find("r_quality") ? cvars.find("r_quality")->value.c_str() : "?", width, height);
        std::fflush(stdout);
        if (std::ofstream log(engine::userDirectory("benchmarks") / (demo.file.stem().string() + ".txt"), std::ios::app); log) {
            const std::time_t t = std::time(nullptr);
            char date[32];
            std::strftime(date, sizeof date, "%Y-%m-%d %H:%M", std::localtime(&t));
            log << date << " | " << result << " | quality " << (cvars.find("r_quality") ? cvars.find("r_quality")->value : "?")
                << " | " << width << 'x' << height << " | " << renderer.glInfo() << '\n';
        }
        if (demo.benchmark)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        else if (complete)
            hud.notify(engine::tr("demo.ended"));
        demo.mode = DemoState::Off;
        demo.current = {};
    };

    while (!glfwWindowShouldClose(window) && (frameLimit < 0 || frame < frameLimit) && exitRequest.kind == LevelExit::None) {
        const double now = fixedDt > 0.0 ? lastTime + fixedDt : glfwGetTime();
        double frameTime = now - lastTime;
        lastTime = now;

        glfwPollEvents();
        if (DemoState& demo = demoState(); demo.mode == DemoState::Play) {
            demo.current = demo.demo.frames[demo.next];
            frameTime = demo.current.dt; // the simulation runs the recorded frames
            mouse.delta = demo.current.mouseDelta;
            mouse.scroll = demo.current.scroll;
            const double real = glfwGetTime();
            if (demo.next > 0)
                demo.realFrames.push_back(static_cast<float>(real - demo.lastReal));
            demo.lastReal = real;
        } else if (demo.mode == DemoState::Record) {
            demo.current = {};
            demo.current.dt = static_cast<float>(frameTime);
            frameTime = demo.current.dt; // stored as float: the replay gets bit-identical steps
            demo.current.mouseDelta = mouse.delta;
            demo.current.scroll = static_cast<float>(mouse.scroll);
        }
        if (DebugChannel& dbg = debugChannel(); dbg.on)
            while (auto line = dbg.take())
                dbg.handle(*line, scripts.debugger()); // breakpoints set while running, "dbg pause"
        // Esc: closes the console (handled by it), steps back in a menu, closes the map, else pauses.
        static bool consoleWasOpen = false;
        if (console.open() != consoleWasOpen) {
            consoleWasOpen = console.open();
            escHeld = true; // the Esc that closed the console does nothing else
        }
        const bool esc = keyHeld(window, GLFW_KEY_ESCAPE) && !console.open();
        if (esc && !escHeld) {
            if (menu.active())
                menu.back();
            else if (talk.active())
                talk.end();
            else if (trade.active())
                trade.end();
            else if (mapOpen)
                mapOpen = false;
            else
                menu.open(GameMenu::Screen::Pause);
        }
        escHeld = esc;
        {
            // Menus get the mouse in framebuffer pixels (HiDPI windows differ from window coordinates).
            double cx = 0.0, cy = 0.0;
            int ww = 1, wh = 1, fw = 1, fh = 1;
            cursorPos(window, &cx, &cy);
            glfwGetWindowSize(window, &ww, &wh);
            glfwGetFramebufferSize(window, &fw, &fh);
            const glm::vec2 scale(static_cast<float>(fw) / static_cast<float>(std::max(ww, 1)),
                                  static_cast<float>(fh) / static_cast<float>(std::max(wh, 1)));
            menu.mouse(glm::vec2(static_cast<float>(cx), static_cast<float>(cy)) * scale,
                       !console.open() && keyHeld(window, kMouseBase + GLFW_MOUSE_BUTTON_LEFT),
                       menu.active() ? mouse.scroll : 0.0);
            if (menu.active())
                mouse.scroll = 0.0;
        }
        // The mouse looks around only while playing (no menu, no console, window focused).
        const bool wantCapture =
            !menu.active() && !console.open() && !talk.active() && !trade.active() && keyHeld(window, kDemoFocus);
        if (wantCapture != captured) {
            captured = wantCapture;
            setCaptured(window, captured);
            mouse.first = true;
            mouse.delta = glm::vec2(0.0f);
        }
        engine::InputState padInput;
        const PadState pad = cvars.find("joy_deadzone") ? readGamepad(padInput, cvars.find("joy_deadzone")->number()) : PadState{};
        if (pad.start && !padStartHeld) { // Start works like Esc
            if (menu.active())
                menu.back();
            else
                menu.open(GameMenu::Screen::Pause);
        }
        padStartHeld = pad.start;
        const bool mKey = (keyDown(window, boundKey("in_map")) || pad.back) && captured;
        if (mKey && !mapHeld)
            mapOpen = !mapOpen;
        mapHeld = mKey;
        // Talking: answers by number keys or a click; E at a character starts a talk.
        {
            int pick = -1;
            for (int k = 0; k < 9; ++k) {
                const bool d = keyHeld(window, GLFW_KEY_1 + k) || keyHeld(window, GLFW_KEY_KP_1 + k);
                if (d && !digitHeld[static_cast<std::size_t>(k)])
                    pick = k;
                digitHeld[static_cast<std::size_t>(k)] = d;
            }
            if (talk.active() && !menu.active() && !console.open()) {
                double cx = 0.0, cy = 0.0;
                int ww = 1, wh = 1, fw = 1, fh = 1;
                cursorPos(window, &cx, &cy);
                glfwGetWindowSize(window, &ww, &wh);
                glfwGetFramebufferSize(window, &fw, &fh);
                talkHover = talk.hit({static_cast<float>(cx) * static_cast<float>(fw) / static_cast<float>(std::max(ww, 1)),
                                      static_cast<float>(cy) * static_cast<float>(fh) / static_cast<float>(std::max(wh, 1))});
                const bool click = keyHeld(window, kMouseBase + GLFW_MOUSE_BUTTON_LEFT);
                if (click && !talkClickHeld && talkHover >= 0)
                    pick = talkHover;
                talkClickHeld = click;
                if (pick >= 0 && pick < talk.answers())
                    runTalkActions(talk.choose(pick, quests));
            } else if (trade.active() && !menu.active() && !console.open()) {
                double cx = 0.0, cy = 0.0;
                int ww = 1, wh = 1, fw = 1, fh = 1;
                cursorPos(window, &cx, &cy);
                glfwGetWindowSize(window, &ww, &wh);
                glfwGetFramebufferSize(window, &fw, &fh);
                const glm::vec2 m(static_cast<float>(cx) * static_cast<float>(fw) / static_cast<float>(std::max(ww, 1)),
                                  static_cast<float>(cy) * static_cast<float>(fh) / static_cast<float>(std::max(wh, 1)));
                trade.hover(m);
                const bool click = keyHeld(window, kMouseBase + GLFW_MOUSE_BUTTON_LEFT);
                if (click && !talkClickHeld) {
                    const bool many = keyHeld(window, GLFW_KEY_LEFT_SHIFT) || keyHeld(window, GLFW_KEY_RIGHT_SHIFT);
                    trade.click(m, inventory, weapons, current, items, tradeHooks, many);
                    // The trader keeps what it got (saved with the game).
                    if (trade.entity() >= 0)
                        scene.entities[static_cast<std::size_t>(trade.entity())].properties["stock"] = GameTrade::formatStock(trade.stock());
                }
                talkClickHeld = click;
            } else {
                talkClickHeld = keyHeld(window, kMouseBase + GLFW_MOUSE_BUTTON_LEFT); // the click that ends a talk does not shoot
            }
            auto openTrade = [&](int index) {
                engine::Entity& e = scene.entities[static_cast<std::size_t>(index)];
                trade.begin(index, e.name, e.properties, assetRoot);
                const glm::vec3 to = player.feet() - e.position;
                if (to.x * to.x + to.z * to.z > 1e-4f)
                    e.yawDegrees = glm::degrees(std::atan2(to.z, to.x));
            };
            if (!tradeNameRequest.empty()) {
                for (int t : engine::findEntitiesByName(scene, tradeNameRequest))
                    if (std::atof(scene.entities[static_cast<std::size_t>(t)].properties["trade"].c_str()) >= 0.5) {
                        tradeRequest = t;
                        break;
                    }
                tradeNameRequest.clear();
            }
            if (tradeRequest >= 0 && !talk.active()) {
                openTrade(tradeRequest);
                tradeRequest = -1;
            }
            // Who could be talked to: a character with a dialog, close, in front, in sight, not hostile.
            useTarget = -1;
            if (captured && !mapOpen && !talk.active() && !trade.active() && scripts.playerHealth() > 0.0f) {
                float best = 0.85f; // cos of the widest angle
                for (std::size_t i = 0; i < scene.entities.size(); ++i) {
                    const engine::Entity& e = scene.entities[i];
                    const auto d = e.properties.find("dialog");
                    const auto t = e.properties.find("trade");
                    const bool talks = d != e.properties.end() && !d->second.empty();
                    const bool trades = t != e.properties.end() && std::atof(t->second.c_str()) >= 0.5;
                    if ((!talks && !trades) || !e.alive || e.offline)
                        continue;
                    const glm::vec3 at = e.position + glm::vec3(0.0f, std::min(e.size.y, 1.8f) * 0.75f, 0.0f);
                    const glm::vec3 to = at - camera.position;
                    const float dist = glm::length(to);
                    if (dist > 3.0f || dist < 1e-3f)
                        continue;
                    const float facing = glm::dot(to / dist, camera.forward());
                    if (facing <= best)
                        continue;
                    if (const auto grudge = e.properties.find("grudge"); grudge != e.properties.end() && grudge->second == "1")
                        continue;
                    if (factions.attitude("player", engine::entityFaction(e)) == engine::Attitude::Hostile && !engine::entityFaction(e).empty())
                        continue;
                    if (const auto hit = physics.raycast(camera.position, to / dist, dist); hit && hit->distance < dist - 0.6f)
                        continue; // a wall between
                    best = facing;
                    useTarget = static_cast<int>(i);
                }
            }
            const bool use = captured && !console.open() && keyDown(window, boundKey("in_use"));
            // An artifact in reach when nobody to talk to is: E picks it up.
            artifactTarget = useTarget < 0 && captured && !mapOpen && !talk.active() && !trade.active() && scripts.playerHealth() > 0.0f
                                 ? artifacts.pickable(camera.position, camera.forward())
                                 : -1;
            if (use && !useHeld && artifactTarget >= 0) {
                const std::string id = artifacts.take(artifactTarget);
                inventory.artifacts.push_back(id);
                const bool onBelt = static_cast<int>(inventory.artifacts.size()) <= items.beltSlots;
                hud.notify(engine::trf(onBelt ? "hud.artifact_belt" : "hud.artifact_pack", artifacts.name(id)));
                audio.playAsset("sounds/items/pickup_medkit.sound", nullptr, 0.8f);
                audio.playAsset("sounds/anomalies/artifact_ping.sound", nullptr, 0.6f);
                artifactTarget = -1;
            }
            bool forced = false;
            if (!talkRequest.empty() && !talk.active()) { // console `talk`
                const auto found = engine::findEntitiesByName(scene, talkRequest);
                talkRequest.clear();
                if (!found.empty() && (scene.entities[static_cast<std::size_t>(found.front())].properties.count("dialog") ||
                                       scene.entities[static_cast<std::size_t>(found.front())].properties.count("trade"))) {
                    useTarget = found.front();
                    forced = true;
                }
            }
            if (((use && !useHeld) || forced) && useTarget >= 0 && !talk.active() &&
                scene.entities[static_cast<std::size_t>(useTarget)].properties.count("dialog") == 0) { // a trader without words
                openTrade(useTarget);
            } else if (((use && !useHeld) || forced) && useTarget >= 0 && !talk.active() && !trade.active()) {
                engine::Entity& e = scene.entities[static_cast<std::size_t>(useTarget)];
                std::filesystem::path file = std::filesystem::path(std::u8string(e.properties["dialog"].begin(), e.properties["dialog"].end()));
                if (file.is_relative())
                    file = assetRoot / file;
                std::string why;
                if (talk.begin(file.lexically_normal(), e.name, useTarget, quests, &why)) {
                    const glm::vec3 to = player.feet() - e.position; // turns to the player
                    if (to.x * to.x + to.z * to.z > 1e-4f)
                        e.yawDegrees = glm::degrees(std::atan2(to.z, to.x));
                    runTalkActions(talk.takeGameActions());
                    for (bool& h : digitHeld)
                        h = true; // keys held now do not answer
                } else {
                    hud.notify(engine::trf("hud.nothing_to_say", e.name));
                    LOG_WARN("dialog", "%s: %s", e.name.c_str(), why.c_str());
                }
            }
            useHeld = use;
            // Quest news for the HUD; rewards of completed quests.
            std::string news;
            for (const engine::Quests::Event& ev : quests.takeEvents()) {
                const engine::QuestDef* q = quests.find(ev.quest);
                if (!q)
                    continue;
                std::string line;
                if (ev.kind == engine::Quests::Event::Started) {
                    line = engine::trf("quest.new", engine::localize(q->title));
                } else if (ev.kind == engine::Quests::Event::Stage) {
                    const engine::QuestStage* st = q->stage(quests.stage(ev.quest));
                    line = engine::localize(q->title) + ": " + (st ? engine::localize(st->text) : std::string());
                } else if (ev.kind == engine::Quests::Event::Completed) {
                    line = engine::trf("quest.done", engine::localize(q->title));
                    for (const auto& r : q->rewards)
                        if (const std::string got = giveReward(r); !got.empty())
                            line += ", " + got;
                } else {
                    line = engine::trf("quest.failed", engine::localize(q->title));
                }
                LOG_INFO("quests", "%s", line.c_str());
                news += (news.empty() ? "" : "  |  ") + line;
            }
            if (!news.empty()) {
                hud.notify(news);
                audio.playAsset("sounds/ui/pda_beep.sound", nullptr, 0.8f);
            }
        }
        {
            const bool fKey = captured && !console.open() && keyDown(window, boundKey("in_flashlight"));
            if (fKey && !flashlightHeld && !flashlight.toggle() && scene.player.flashlight)
                hud.notify(engine::tr("hud.battery_empty"));
            flashlightHeld = fKey;
            // Medkit (H): +50 health at once, not when full.
            const bool hKey = captured && !console.open() && keyDown(window, boundKey("in_heal"));
            if (hKey && !healHeld && scripts.playerHealth() > 0.0f) {
                if (inventory.medkits <= 0)
                    hud.notify(engine::tr("hud.no_medkit"));
                else if (scripts.playerHealth() >= scene.player.maxHealth)
                    hud.notify(engine::tr("hud.health_full"));
                else {
                    --inventory.medkits;
                    scripts.setPlayerHealth(std::min(scene.player.maxHealth, scripts.playerHealth() + 50.0f));
                    audio.playAsset("sounds/items/heal.sound", nullptr, 1.0f);
                    hud.notify(engine::trf("hud.medkit_used", std::to_string(inventory.medkits)));
                }
            }
            healHeld = hKey;
        }

        // Look is applied every frame for responsiveness; movement runs at a fixed rate.
        if (cvInvert.boolean())
            mouse.delta.y = -mouse.delta.y;
        if (captured && pad.present) // right stick: like moving the mouse ~900 px/s at full tilt
            mouse.delta += pad.look * glm::vec2(1.0f, cvInvert.boolean() ? -1.0f : 1.0f) * static_cast<float>(frameTime) * 900.0f *
                           cvars.find("joy_sensitivity")->number();
        if (captured)
            camera.look(mouse.delta);
        mouse.delta = glm::vec2(0.0f);
        readInput(window, input);
        for (std::size_t a = 0; a < input.down.size(); ++a)
            input.down[a] = input.down[a] || padInput.down[a];
        if (!captured || console.open())
            input = {};
        if (captured && pad.nextWeapon && !padWeaponHeld && weapons.size() > 1)
            next = (next + 1) % weapons.size();
        padWeaponHeld = pad.nextWeapon;

        // Weapon selection: 1-9 or the mouse wheel.
        for (int i = 0; i < 9; ++i) {
            const bool pressed = keyHeld(window, GLFW_KEY_1 + i);
            const bool down = captured && pressed;
            if (down && !slotHeld[static_cast<std::size_t>(i)] && static_cast<std::size_t>(i) < weapons.size())
                next = static_cast<std::size_t>(i);
            slotHeld[static_cast<std::size_t>(i)] = pressed; // a key still held from answering a dialog does not switch
        }
        if (captured && std::abs(mouse.scroll) >= 1.0 && weapons.size() > 1) {
            const int dir = mouse.scroll > 0 ? -1 : 1;
            next = (next + weapons.size() + static_cast<std::size_t>(dir + static_cast<int>(weapons.size()))) % weapons.size();
        }
        mouse.scroll = 0.0;
        // Quick save (F5) and quick load (F9), rebindable.
        {
            const bool qs = captured && keyDown(window, boundKey("in_quicksave"));
            const bool ql = captured && keyDown(window, boundKey("in_quickload"));
            if (qs && !quickSaveHeld)
                saveTo("quick");
            if (ql && !quickLoadHeld) {
                if (std::filesystem::exists(saveFile("quick")))
                    exitRequest = {LevelExit::LoadSave, saveFile("quick")};
                else
                    hud.notify(engine::tr("save.no_quicksave"));
            }
            quickSaveHeld = qs;
            quickLoadHeld = ql;
        }
        // G: throw a bolt (finds anomalies and sets them off). FPS_ENGINE_BOLT=1 throws one at the start (test hook).
        {
            static bool testBolt = std::getenv("FPS_ENGINE_BOLT") != nullptr;
            const bool g = captured && keyDown(window, boundKey("in_bolt"));
            if ((g && !boltHeld) || (testBolt && frame == 2)) {
                anomalies.throwBolt(player.eye(), camera.forward(), player.velocity());
                testBolt = false;
            }
            boltHeld = g;
        }

        // Free camera: the player stands still, WASD flies the camera.
        if (cvFreecam.boolean() != freecamActive) {
            freecamActive = cvFreecam.boolean();
            freecamPosition = camera.position;
        }
        if (freecamActive) {
            glm::vec3 fly(0.0f);
            if (input.isDown(engine::Action::MoveForward)) fly += camera.forward();
            if (input.isDown(engine::Action::MoveBack)) fly -= camera.forward();
            if (input.isDown(engine::Action::MoveRight)) fly += camera.right();
            if (input.isDown(engine::Action::MoveLeft)) fly -= camera.right();
            if (input.isDown(engine::Action::Jump)) fly.y += 1.0f;
            if (input.isDown(engine::Action::Crouch)) fly.y -= 1.0f;
            if (glm::dot(fly, fly) > 0.0f)
                freecamPosition += glm::normalize(fly) * static_cast<float>(frameTime) * (input.isDown(engine::Action::Sprint) ? 25.0f : 6.0f);
            input = {};
        }
        // Menus pause the game; timescale is a cheat (1 = normal).
        const double simTime = menu.active() || talk.active() || trade.active() ? 0.0 : frameTime * cvTimescale.number();
        const int steps = clock.advance(simTime);
        for (int i = 0; i < steps; ++i) {
            ENGINE_ZONE("Simulation step");
            const float dt = static_cast<float>(clock.step());
            // Debug shapes age per simulation step: "this frame" shapes from scripts stay until the next step redraws them.
            engine::DebugDraw::global().tick(dt);
            if (engine::WaterBody water; engine::waterAt(scene, player.feet(), &water))
                player.setWater(water.level, water.flow);
            player.fixedUpdate(input, camera, dt);
            // Pickups the player touches (ammo, medkits, weapons).
            for (const PickupResult& r : collectPickups(scene, player.feet(), inventory, weapons, current, assetRoot)) {
                if (!r.weapon.empty()) {
                    const std::size_t before = weapons.size();
                    if (auto slot = addWeapon(r.weapon)) {
                        if (weapons.size() > before)
                            inventory.addStartingAmmo(weapons[*slot]);
                        if (!weapons[current].reloading())
                            next = *slot; // take it in hand
                    }
                }
                hud.notify(r.message);
                const glm::vec3 at = scene.entities[r.entity].position;
                audio.playAsset(r.sound, &at, 1.0f);
                scripts.fireOutput(static_cast<int>(r.entity), "OnPickup", -1);
            }
            // Day/night cycle: dayMinutes real minutes per game day.
            if (engine::TimeOfDay& tod = scene.environment.time; tod.enabled && tod.dayMinutes > 0.0f)
                tod.hour = std::fmod(tod.hour + dt * 24.0f / (tod.dayMinutes * 60.0f), 24.0f);

            // Lower the current weapon, swap, raise the new one (0.15 s each way).
            if (next != current) {
                lowered = std::min(1.0f, lowered + dt / 0.15f);
                if (lowered >= 1.0f) {
                    current = next;
                    weapons[current].drawn();
                    audio.play(weapons[current].settings.soundDraw, player.eye(), 1.0f);
                    wasReloading = weapons[current].reloading();
                }
            } else {
                lowered = std::max(0.0f, lowered - dt / 0.15f);
            }
            engine::Weapon& weapon = weapons[current];
            const bool ready = next == current && lowered <= 0.0f;
            // Worn weapons jam: the trigger does nothing until R clears it.
            jammed.resize(weapons.size(), 0);
            unjamTimer = std::max(0.0f, unjamTimer - dt);
            const bool fireHeld = ready && (autoFire || input.isDown(engine::Action::Fire));
            bool reloadHeld = ready && input.isDown(engine::Action::Reload);
            if (jammed[current]) {
                static bool jamFireHeld = false;
                if (fireHeld && !jamFireHeld) {
                    audio.play(weapon.settings.soundDryFire, player.eye());
                    hud.notify(engine::tr("hud.jammed"));
                }
                jamFireHeld = fireHeld;
                if (reloadHeld && unjamTimer <= 0.0f) {
                    jammed[current] = 0;
                    unjamTimer = 0.9f;
                    audio.play(weapon.settings.soundReload, player.eye(), 0.7f);
                }
                reloadHeld = false;
            }
            const int shots = weapon.update(dt, fireHeld && !jammed[current] && unjamTimer <= 0.0f, reloadHeld);
            if (shots > 0) {
                weapon.condition = std::max(0.0f, weapon.condition - weapon.settings.wearPerShot * static_cast<float>(shots));
                if (!cvGod.boolean()) {
                    static std::mt19937 jamRng{engine::seedFor("jam")};
                    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(jamRng) < engine::jamProbability(weapon.settings, weapon.condition))
                        jammed[current] = 1;
                }
            }
            if (next != current)
                weapon.aim(1.0f, false); // switching drops the aim
            weapon.aim(dt, ready && input.isDown(engine::Action::Aim));
            if (shots > 0) {
                audio.play(weapon.settings.soundFire, player.eye() + camera.forward() * 0.5f);
                if (!weapon.settings.melee())
                    scripts.makeNoise(player.eye(), 80.0f); // NPCs and crows hear it
                if (const float duck = cvars.find("snd_duck")->number(); duck > 0.0f && !weapon.settings.melee()) {
                    // Gunfire pushes the ambience and music down for a moment, so the shot and what follows are heard.
                    audio.system().duck(engine::AudioGroup::Ambient, 1.0f - 0.55f * duck, 0.35f, 1.6f);
                    audio.system().duck(engine::AudioGroup::Music, 1.0f - 0.35f * duck, 0.35f, 1.6f);
                }
                if (!weapon.settings.melee()) {
                    const glm::vec3 v = player.velocity();
                    const float speed = player.grounded() ? std::sqrt(v.x * v.x + v.z * v.z) : 0.0f;
                    const glm::vec3 muzzleView =
                        glm::vec3(weaponTransform(weapon, static_cast<float>(now), speed, lowered) * glm::vec4(weapon.settings.muzzle, 1.0f));
                    for (int s = 0; s < shots; ++s)
                        effects.shot(weapon.settings, camera.view(), muzzleView, v);
                }
            }
            if (weapon.reloading() && !wasReloading) {
                if (weapon.reloadingFromEmpty()) {
                    if (input.isDown(engine::Action::Fire))
                        audio.play(weapon.settings.soundDryFire, player.eye());
                    audio.play(weapon.settings.soundReloadEmpty.empty() ? weapon.settings.soundReload
                                                                        : weapon.settings.soundReloadEmpty,
                               player.eye());
                } else {
                    audio.play(weapon.settings.soundReload, player.eye());
                }
            }
            wasReloading = weapon.reloading();
            const glm::vec3 moveVel = player.velocity();
            weapon.animate(dt, player.grounded() ? std::sqrt(moveVel.x * moveVel.x + moveVel.z * moveVel.z) : 0.0f);
            const int rays = weapon.settings.melee() ? shots : shots * weapon.settings.pellets;
            for (int s = 0; s < rays; ++s) {
                glm::vec3 origin = player.eye();
                glm::vec3 dir = weapon.settings.melee() ? camera.forward() : weapon.spreadDirection(camera.forward());
                float range = weapon.settings.range;
                float damage = weapon.settings.damage;
                // A bullet may glance off hard surfaces at a shallow angle (assets/surfaces.cfg `ricochet`)
                // and fly on with less energy; at most two bounces.
                for (int bounce = 0; bounce < 3; ++bounce) {
                const engine::ShotHit hit =
                    weapon.settings.melee()
                        ? engine::traceMelee(scene, physics, origin, dir, range, weapon.settings.spreadDegrees)
                        : engine::traceShot(scene, physics, origin, dir, range);
                if (!hit.hit)
                    break;
                impacts.push_back({hit.point, 0.0f, hit.entity >= 0});
                effects.impact(hit.point, hit.normal, hit.surface, hit.entity >= 0);
                if (hit.entity >= 0)
                    audio.play(weapon.settings.soundHit, hit.point);
                else if (weapon.settings.melee())
                    audio.play(weapon.settings.soundImpact, hit.point);
                else // concrete, metal, wood, dirt... (assets/surfaces.cfg)
                    audio.impact(hit.point, hit.surface, weapon.settings.soundImpact);
                if (hit.entity >= 0) {
                    const bool wasAlive = scene.entities[static_cast<std::size_t>(hit.entity)].alive;
                    scripts.damageEntity(hit.entity, damage, -1);
                    if (wasAlive && !scene.entities[static_cast<std::size_t>(hit.entity)].alive) {
                        ++kills;
                        killShots[static_cast<std::size_t>(hit.entity)] = {hit.point, dir * damage * (weapon.settings.melee() ? 1.2f : 0.8f)};
                    }
                    break;
                }
                if (hit.model >= 0) // physics prop: the bullet (or blow) pushes it
                    physics.applyImpulse(hit.model, hit.point, dir * damage * (weapon.settings.melee() ? 0.8f : 0.4f));
                if (hit.ragdoll >= 0) { // a body on the ground: the shot moves it, then stops
                    physics.ragdollImpulse(hit.ragdoll, hit.point, dir * damage * 0.4f);
                    break;
                }
                if (weapon.settings.melee())
                    break;
                const engine::SurfaceType* surface = audio.surfaces().find(hit.surface);
                if (!surface || !ricochetRoll(engine::ricochetChance(*surface, dir, hit.normal)))
                    break;
                dir = engine::ricochetDirection(dir, hit.normal, ricochetLift());
                range = std::max(0.0f, range - glm::distance(origin, hit.point)) * 0.6f;
                damage *= 0.5f;
                origin = hit.point + hit.normal * 0.02f;
                effects.ricochet(hit.point, dir, *surface);
                audio.play(surface->ricochetSound, hit.point);
                if (range < 1.0f)
                    break;
                }
            }
            for (Impact& impact : impacts)
                impact.age += dt;
            std::erase_if(impacts, [](const Impact& i) { return i.age > (i.onEnemy ? 0.15f : 6.0f); });

            const float healthBefore = scripts.playerHealth();
            scripts.fixedUpdate(dt);
            alife.update(scene, scripts, factions, player.feet(), dt, navSnap);
            for (const engine::ALife::Event& ev : alife.takeEvents())
                if (ev.kind == engine::ALife::Event::Gunfire) // a fight out of sight: shots from that direction
                    audio.playAsset("sounds/ambient/distant_gunfire.sound", &ev.position, 0.9f);
            if (cvars.find("alife_debug")->boolean())
                for (const engine::ALife::Squad& s : alife.squads()) {
                    if (s.living == 0)
                        continue;
                    const glm::vec4 color = s.online ? glm::vec4(0.3f, 1.0f, 0.4f, 1.0f) : glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
                    const glm::vec3 at = s.position + glm::vec3(0.0f, 3.0f, 0.0f);
                    engine::DebugDraw::global().text(at, s.name + " [" + s.faction + "] " + std::to_string(s.living), color);
                    if (s.target >= 0)
                        engine::DebugDraw::global().arrow(at, alife.points()[static_cast<std::size_t>(s.target)].position + glm::vec3(0.0f, 3.0f, 0.0f), color);
                }
            {
                // Anomaly storm: phases, warnings, and damage to everyone outdoors.
                engine::AnomalyStormNow& now = scene.environment.anomalyStormNow;
                const engine::AnomalyStormSettings& ss = scene.environment.anomalyStorm;
                if (now.request >= 0.0f) { // game.start_storm()
                    anomalyStorm.trigger(ss, now.request);
                    now.request = -1.0f;
                    stormPhaseChanged(engine::AnomalyStorm::Phase::Warning);
                }
                if (anomalyStorm.update(ss, dt))
                    stormPhaseChanged(anomalyStorm.phase());
                anomalyStorm.publish(now);
                if (anomalyStorm.danger() > 0.0f) {
                    if (!sheltered && scripts.playerHealth() > 0.0f)
                        scripts.setPlayerHealth(std::max(0.0f, scripts.playerHealth() - ss.damage * anomalyStorm.danger() * dt));
                    if ((stormHurtTimer -= dt) <= 0.0f) { // NPCs outdoors (mutants belong to the Zone and are spared)
                        stormHurtTimer = 0.5f;
                        const auto interiors = engine::interiorVolumes(scene);
                        for (std::size_t i = 0; i < scene.entities.size(); ++i) {
                            const engine::Entity& e = scene.entities[i];
                            if (!e.alive || e.offline || e.helper())
                                continue;
                            const std::string faction = engine::entityFaction(e);
                            if (faction.empty() || faction == "mutant")
                                continue;
                            if (!engine::sheltered(physics, interiors, e.position + glm::vec3(0.0f, 1.9f, 0.0f)))
                                scripts.damageEntity(static_cast<int>(i), ss.npcDamage * anomalyStorm.danger() * 0.5f, -2);
                        }
                    }
                }
            }
            if (std::string change = scripts.takeLevelChange(); !change.empty()) { // game.change_level() from a script
                std::filesystem::path file = change;
                if (file.is_relative())
                    file = std::filesystem::path(argv[0]).parent_path() / "assets" / "levels" / file;
                exitRequest = {LevelExit::Change, file};
            }
            if (cvGod.boolean() && scripts.playerHealth() < healthBefore)
                scripts.setPlayerHealth(healthBefore);
            if (scripts.playerHealth() < healthBefore)
                audio.play2D(scene.player.hurtSound);
            anomalies.fixedUpdate(dt, scene, physics, player, scripts); // forces act on the next movement step
            // Armor takes part of every hit (and wears doing it).
            if (const engine::ItemDef* armor = inventory.armor.empty() ? nullptr : items.find(engine::ItemDef::Kind::Armor, inventory.armor);
                armor && scripts.playerHealth() < healthBefore)
                scripts.setPlayerHealth(healthBefore - engine::armorAbsorb(*armor, inventory.armorCondition, healthBefore - scripts.playerHealth()));
            // Artifacts on the belt: stop a share of what got through, heal or hurt, add or draw out radiation.
            const engine::ArtifactEffects belt = engine::beltEffects(items, inventory.artifacts);
            if (belt.armor > 0.0f && scripts.playerHealth() < healthBefore)
                scripts.setPlayerHealth(healthBefore - (healthBefore - scripts.playerHealth()) * (1.0f - belt.armor));
            if (belt.heal != 0.0f && scripts.playerHealth() > 0.0f && !(belt.heal < 0.0f && cvGod.boolean()))
                scripts.setPlayerHealth(std::clamp(scripts.playerHealth() + belt.heal * dt, 0.5f, std::max(scripts.playerHealth(), scene.player.maxHealth)));
            if (belt.rad != 0.0f && scripts.playerHealth() > 0.0f)
                anomalies.setDose(std::max(0.0f, anomalies.dose() + belt.rad * dt));
            {
                // Load: over the free weight no sprint and slower, near the maximum a crawl.
                const float kg = carriedWeight(inventory, weapons, items) - belt.carry; // artifacts can lighten the load
                const float k = engine::loadSpeedFactor(items, kg);
                player.settings.walkSpeed = scene.player.walkSpeed * k;
                player.settings.sprintSpeed = engine::loadAllowsSprint(items, kg) ? scene.player.sprintSpeed : player.settings.walkSpeed;
                const bool over = kg > items.carryFree;
                if (over && !overloadedShown)
                    hud.notify(engine::trf("hud.overloaded", std::to_string(static_cast<int>(std::lround(kg)))));
                overloadedShown = over;
            }
            if (cvGod.boolean() && scripts.playerHealth() < healthBefore) // god: anomalies do not hurt either
                scripts.setPlayerHealth(healthBefore);
            {
                std::string ground;
                if (player.grounded())
                    if (auto under = physics.raycast(player.feet() + glm::vec3(0.0f, 0.3f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 1.0f))
                        ground = under->surface;
                if (player.waterDepth() > 0.05f)
                    ground = "water"; // wading: splashing steps
                audio.walk(scene.player, player.feet(), player.velocity(), player.grounded(), dt, ground);
                audio.water(player.feet(), player.velocity(), player.waterDepth() > 0.0f, player.swimming(), player.headUnderwater(), dt);
            }
            // Characters that died this step go limp: a ragdoll from their current pose.
            for (std::size_t i = 0; i < scene.entities.size(); ++i) {
                engine::Entity& e = scene.entities[i];
                const bool wants = !e.alive && !e.offline && e.animator.graph && e.animator.graph->ragdollOnDeath && !e.animator.graph->ragdoll.empty();
                const auto it = ragdolls.find(i);
                if (wants && it == ragdolls.end()) {
                    engine::Ragdoll r;
                    if (engine::startRagdoll(physics, e, r)) {
                        if (const auto shot = killShots.find(i); shot != killShots.end())
                            physics.ragdollImpulse(r.handle, shot->second.first, shot->second.second);
                        ragdolls.emplace(i, std::move(r));
                    }
                } else if (!wants && it != ragdolls.end()) { // alive again (a loaded save)
                    engine::stopRagdoll(physics, it->second);
                    e.animator.poseOverride.clear();
                    ragdolls.erase(it);
                }
                killShots.erase(i);
            }
            physics.step(dt);
            physics.syncDynamic(scene); // props the player, shots and blasts moved
            for (auto& [index, ragdoll] : ragdolls) {
                engine::Entity& e = scene.entities[index];
                if (e.model && e.model->skeleton)
                    engine::ragdollPose(physics, ragdoll, *e.model->skeleton, e.animator.poseOverride);
            }
            if (scripts.playerHealth() <= 0.0f && menu.screen() != GameMenu::Screen::Death) {
                LOG_INFO("game", "You died.");
                if (frameLimit > 0) { // test runs carry on
                    player.respawn();
                    scripts.setPlayerHealth(scene.player.maxHealth);
                    anomalies.resetDose();
                } else {
                    menu.open(GameMenu::Screen::Death);
                    break;
                }
            }
        }

        camera.position = freecamActive ? freecamPosition : player.interpolatedEye(static_cast<float>(clock.alpha()));
        // The rest of the frame's update as a task graph: character animation (itself
        // parallel per entity) runs on the workers while the main thread updates sound,
        // particles, weather, anomalies and the flashlight (they share the audio system).
        // Both only read what the other writes; the graph joins before rendering.
        {
            frameGraph.clear();
            const float sim = static_cast<float>(simTime), frame = static_cast<float>(frameTime);
            frameGraph.add("animation", [&] { // per frame for smooth motion; foot IK reads the ground (read-only raycasts)
                engine::updateAnimations(scene, sim, [&](const glm::vec3& from) -> std::optional<float> {
                    if (const auto hit = physics.raycast(from, glm::vec3(0.0f, -1.0f, 0.0f), 2.0f))
                        return hit->point.y;
                    if (scene.terrain && scene.terrain->valid())
                        return scene.terrain->heightAt(from.x, from.z);
                    return std::nullopt;
                });
            });
            frameGraph.add("audio, effects, weather", [&] {
                audio.update(frame, scene, camera.position, camera.forward());
                effects.update(sim, scene, physics, camera.position, audio);
                if ((shelterTimer -= frame) <= 0.0f) { // a roof within 40 m above the head
                    shelterTimer = 0.25f;
                    sheltered = engine::sheltered(physics, engine::interiorVolumes(scene), camera.position); // roof or interior volume
                    audio.setSheltered(sheltered);
                }
                weatherDirector.update(scene.environment, sim);
                weather.update(sim, scene, camera.position, sheltered, audio);
                anomalies.update(sim, physics, camera.position);
                artifacts.update(sim, camera.position);
                flashlight.update(sim, now, camera);
            }, {}, true);
            {
                ENGINE_ZONE("Frame tasks");
                frameGraph.run();
            }
        }
        if ((watchTimer -= static_cast<float>(frameTime)) <= 0.0f) {
            watchTimer = 0.5f;
            const engine::CVar* hot = cvars.find("dev_hot_reload");
            if (hot && hot->boolean() && !watcher)
                watcher = std::make_unique<engine::FileWatcher>(watchRoot, 0.5);
            else if (hot && !hot->boolean())
                watcher.reset();
            if (watcher)
                hotReload();
        }
        if ((memoryTimer -= static_cast<float>(frameTime)) <= 0.0f) {
            memoryTimer = 2.0f;
            std::array<bool, engine::kMemoryCategories> over{};
            for (const engine::MemoryOver& o : engine::overBudget(measureMemory(), scene.memoryBudget)) {
                const auto i = static_cast<std::size_t>(o.category);
                over[i] = true;
                if (!overBudgetWarned[i])
                    LOG_WARN("memory", "%s over budget: %.1f MB of %.1f MB", engine::memoryCategoryName(o.category), o.usedMB, o.limitMB);
            }
            overBudgetWarned = over;
        }
        if (flashlight.justEmptied())
            hud.notify(engine::tr("hud.battery_empty"));

        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width > 0 && height > 0) {
            // Aiming zooms in; mouse sensitivity follows so the aim stays steady.
            {
                const engine::Weapon& w = weapons[current];
                const float zoom = w.canAim() ? smoothAim(w) : 0.0f;
                const float hip = cvFov.number();
                camera.fovDegrees = glm::mix(hip, w.canAim() ? w.settings.aimFov : hip, zoom);
                camera.sensitivity = baseSensitivity * cvSensitivity.number() * camera.fovDegrees / hip;
            }
            static const float testTurn = std::getenv("FPS_ENGINE_TURN") ? std::strtof(std::getenv("FPS_ENGINE_TURN"), nullptr) : 0.0f;
            camera.yawDegrees += testTurn; // test hook: degrees per frame (TAA in motion)
            engine::RenderView view;
            view.view = camera.view();
            view.projection = camera.projection(static_cast<float>(width) / static_cast<float>(height));
            view.cameraPosition = camera.position;
            view.viewportWidth = width;
            view.viewportHeight = height;
            view.time = static_cast<float>(now);
            view.wetness = weather.wetness();
            view.lightning = weather.flash();
            view.deferPresent = true; // the weapon layer is drawn before post-processing
            view.temporal = true;     // one continuous view: TAA can use the earlier frames
            engine::OverlayPass effectsOverlay; // muzzle flashes, drawn with the weapon below
            effects.fill(view, view.view, effectsOverlay);
            anomalies.fill(view, view.view);
            artifacts.fill(view, scripts.time());
            flashlight.fill(view);
            addCharacterFlashlights(scene, view);
            // Debug lines from code and scripts (DebugDraw), with their texts drawn in the HUD below.
            engine::DebugGeometry debugLines;
            std::vector<engine::DebugDraw::Text> debugTexts;
            if (cvDebugDraw.boolean()) {
                engine::DebugDraw::global().collect(debugLines, &debugTexts);
                if (!debugLines.lines.empty())
                    view.debug.push_back(&debugLines);
            }
            streamer.update(scene, &physics, camera.position);
            renderer.render(scene, view);

            // First-person layer: weapon, muzzle flash and crosshair.
            engine::OverlayPass overlay;
            const float aspect = static_cast<float>(width) / static_cast<float>(height);
            const float aimed = smoothAim(weapons[current]);
            overlay.projection = glm::perspective(glm::radians(glm::mix(60.0f, 50.0f, aimed)), aspect, 0.01f, 10.0f);
            engine::SkyState sky; // the moon lights the weapon at night
            engine::environmentAt(scene.environment, scene.sunDirection, &sky);
            overlay.sunDirectionViewSpace = glm::mat3(view.view) * sky.lightDirection;
            const engine::Weapon& weapon = weapons[current];
            overlay.model = weapon.viewModel.get();
            const glm::vec3 vel = player.velocity();
            const float moveSpeed = player.grounded() ? std::sqrt(vel.x * vel.x + vel.z * vel.z) : 0.0f;
            overlay.modelTransform = weaponTransform(weapon, static_cast<float>(now), moveSpeed, lowered);
            overlay.animator = &weapon.animator;
            overlay.sprites = std::move(effectsOverlay.sprites);
            if (weapon.flash() && !weapon.settings.melee() && weapon.settings.effectFlash.empty()) {
                const glm::vec3 muzzle = glm::vec3(overlay.modelTransform * glm::vec4(weapon.settings.muzzle, 1.0f));
                overlay.boxes.push_back({muzzle, glm::vec3(0.05f), glm::vec3(1.0f, 0.85f, 0.35f), true});
            }
            // Crosshair only from the hip: aimed, the sights are the reticle.
            if (aimed < 0.3f && !menu.active()) {
                const float gap = 0.008f, len = 0.012f, thick = 0.0016f;
                const glm::vec3 white(0.95f);
                overlay.boxes.push_back({{0, gap + len * 0.5f, -1}, {thick, len, thick}, white, true});
                overlay.boxes.push_back({{0, -gap - len * 0.5f, -1}, {thick, len, thick}, white, true});
                overlay.boxes.push_back({{gap + len * 0.5f, 0, -1}, {len, thick, thick}, white, true});
                overlay.boxes.push_back({{-gap - len * 0.5f, 0, -1}, {len, thick, thick}, white, true});
            }
            renderer.renderOverlay(view, overlay);
            renderer.present();
            // HUD: minimap in the corner, or the full map (M).
            ui.begin(width, height);
            if (menu.active()) {
                // menus cover the game: no map
            } else {
                // PDA tasks: active ones first (newest first), then done and failed.
                std::vector<QuestView> questViews;
                std::vector<std::pair<double, const std::string*>> order;
                for (const auto& [id, entry] : quests.entries())
                    order.emplace_back((entry.status == engine::Quests::Status::Active ? 1e9 : 0.0) + entry.started, &id);
                std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                for (const auto& [key, id] : order) {
                    const engine::QuestDef* q = quests.find(*id);
                    if (!q)
                        continue;
                    QuestView v;
                    v.title = q->title;
                    v.description = engine::localize(q->description);
                    v.active = quests.status(*id) == engine::Quests::Status::Active;
                    v.failed = quests.status(*id) == engine::Quests::Status::Failed;
                    if (const engine::QuestStage* st = v.active ? q->stage(quests.stage(*id)) : nullptr) {
                        v.stage = st->text;
                        if (!st->target.empty())
                            for (int t : engine::findEntitiesByName(scene, st->target))
                                v.targets.push_back({scene.entities[static_cast<std::size_t>(t)].position.x,
                                                     scene.entities[static_cast<std::size_t>(t)].position.z});
                    }
                    questViews.push_back(std::move(v));
                }
                if (mapOpen) {
                    map.drawFullMap(ui, width, height, player.feet(), camera.flatForward(), questViews);
                    // PDA status: money, load, condition of the weapon in hand and the armor.
                    const float th = std::max(14.0f, static_cast<float>(height) * 0.018f);
                    const float kg = carriedWeight(inventory, weapons, items);
                    std::ostringstream st;
                    st << inventory.money << " RU   " << std::fixed << std::setprecision(1) << kg << " / " << items.carryFree << " kg   "
                       << engine::localize(weapons[current].settings.name) << " " << static_cast<int>(std::lround(weapons[current].condition * 100.0f)) << " %";
                    if (const engine::ItemDef* ar = inventory.armor.empty() ? nullptr : items.find(engine::ItemDef::Kind::Armor, inventory.armor))
                        st << "   " << engine::localize(ar->name) << " " << static_cast<int>(std::lround(inventory.armorCondition * 100.0f)) << " %";
                    if (!inventory.artifacts.empty()) {
                        st << "   " << engine::tr("hud.belt") << ":";
                        for (std::size_t a = 0; a < inventory.artifacts.size(); ++a)
                            st << (a == static_cast<std::size_t>(std::max(0, items.beltSlots)) ? "  | " + engine::tr("hud.backpack") + ":" : "") << " " << artifacts.name(inventory.artifacts[a]);
                    }
                    const glm::vec4 col = kg > items.carryFree ? glm::vec4(1.0f, 0.45f, 0.3f, 1.0f) : glm::vec4(0.95f, 0.8f, 0.45f, 1.0f);
                    ui.text(st.str(), {th, static_cast<float>(height) - th * 2.0f}, th, col, true);
                } else
                    map.drawMinimap(ui, width, height, player.feet(), camera.flatForward(), questViews);
            }
            hud.update(static_cast<float>(frameTime), scripts.playerHealth());
            menu.setCanSave(scripts.playerHealth() > 0.0f);
            if (!mapOpen && !menu.active()) {
                anomalies.drawHud(ui, width, height);
                artifacts.drawHud(ui, width, height);
                const engine::Weapon& w = weapons[current];
                HudState hs;
                hs.health = scripts.playerHealth();
                hs.maxHealth = scene.player.maxHealth;
                hs.weapon = w.settings.name;
                hs.ammo = w.ammo();
                hs.magazine = w.reserve() >= 0 ? w.reserve() : w.settings.magazineSize; // stock of the ammo type, else the magazine
                hs.medkits = inventory.medkits;
                hs.melee = w.settings.melee();
                hs.reload = w.reloading() ? w.reloadProgress() : -1.0f;
                hs.battery = flashlight.hasBattery() ? flashlight.charge() : -1.0f;
                hs.flashlightOn = flashlight.on();
                hud.draw(ui, width, height, hs);
                if (trade.active()) {
                    trade.draw(ui, width, height, inventory, weapons, current, items);
                } else if (talk.active()) {
                    talk.draw(ui, width, height, talkHover);
                } else if (artifactTarget >= 0 && artifactTarget < static_cast<int>(artifacts.lying().size())) {
                    const float H = static_cast<float>(height), size = uiSize(H, 0.022f, 12.0f, 26.0f);
                    const engine::CVar* key = cvars.find("in_use");
                    const std::string line = (key ? key->value : std::string("E")) + "   Sebrat: " +
                                             artifacts.name(artifacts.lying()[static_cast<std::size_t>(artifactTarget)].id);
                    const float w = ui.textWidth(line, size, true);
                    const glm::vec2 at(std::round((static_cast<float>(width) - w) * 0.5f), std::round(H * 0.6f));
                    ui.rect(at - glm::vec2(size * 0.6f, size * 0.3f), {w + size * 1.2f, size * 1.6f}, {0.05f, 0.055f, 0.045f, 0.7f});
                    ui.text(line, at, size, {0.6f, 0.9f, 1.0f, 1.0f}, true);
                } else if (useTarget >= 0) { // "E  Mluvit: Sidor"
                    const float H = static_cast<float>(height), size = uiSize(H, 0.022f, 12.0f, 26.0f);
                    const engine::CVar* key = cvars.find("in_use");
                    const engine::Entity& target = scene.entities[static_cast<std::size_t>(useTarget)];
                    std::string name = target.name;
                    if (const auto d = target.properties.find("dialog"); d != target.properties.end()) {
                        std::filesystem::path file = std::filesystem::path(std::u8string(d->second.begin(), d->second.end()));
                        if (file.is_relative())
                            file = assetRoot / file;
                        name = talk.speakerOf(file.lexically_normal(), name);
                    }
                    const std::string line = (key ? key->value : std::string("E")) + "   " +
                                             engine::trf(target.properties.count("dialog") ? "hud.talk_to" : "hud.trade_with", name);
                    const float w = ui.textWidth(line, size, true);
                    const glm::vec2 at(std::round((static_cast<float>(width) - w) * 0.5f), std::round(H * 0.6f));
                    ui.rect(at - glm::vec2(size * 0.6f, size * 0.3f), {w + size * 1.2f, size * 1.6f}, {0.05f, 0.055f, 0.045f, 0.7f});
                    ui.text(line, at, size, {0.93f, 0.84f, 0.6f, 1.0f}, true);
                }
                if (const auto phase = anomalyStorm.phase();
                    phase == engine::AnomalyStorm::Phase::Warning || phase == engine::AnomalyStorm::Phase::Storm) {
                    // Storm banner: countdown to the storm / how long it lasts, red while outdoors.
                    const float H = static_cast<float>(height), text = uiSize(H, 0.024f, 12.0f, 28.0f);
                    const int s = static_cast<int>(std::ceil(anomalyStorm.secondsLeft()));
                    char clock[32];
                    std::snprintf(clock, sizeof clock, "%d:%02d", s / 60, s % 60);
                    const std::string line = engine::trf(phase == engine::AnomalyStorm::Phase::Warning ? "storm.in" : "storm.left", clock);
                    const std::string where = engine::tr(sheltered ? "storm.sheltered" : "storm.outside");
                    const float w = std::max(ui.textWidth(line, text, true), ui.textWidth(where, text * 0.8f, true)) + text * 1.6f;
                    const glm::vec2 at(std::round((static_cast<float>(width) - w) * 0.5f), std::round(H * 0.11f));
                    const float pulse = sheltered ? 0.0f : 0.5f + 0.5f * std::sin(static_cast<float>(scripts.time()) * 6.0f);
                    ui.rect(at, {w, text * 2.6f}, glm::vec4(0.12f + 0.25f * pulse, 0.04f, 0.03f, 0.8f));
                    ui.text(line, {at.x + text * 0.8f, at.y + text * 0.3f}, text, glm::vec4(1.0f, 0.75f, 0.55f, 1.0f), true);
                    ui.text(where, {at.x + text * 0.8f, at.y + text * 1.45f}, text * 0.8f,
                            sheltered ? glm::vec4(0.7f, 0.85f, 0.7f, 1.0f) : glm::vec4(1.0f, 0.45f, 0.35f, 1.0f), true);
                }
            }
            if (cvShowFps.boolean()) {
                char fps[32];
                std::snprintf(fps, sizeof fps, "%.0f FPS", frameTime > 0.0 ? 1.0 / frameTime : 0.0);
                ui.text(fps, {10.0f, 8.0f}, 18.0f, {0.95f, 0.9f, 0.6f, 1.0f}, true);
            }
            if (!debugTexts.empty()) {
                const glm::mat4 viewProj = view.projection * view.view;
                for (const engine::DebugDraw::Text& t : debugTexts) {
                    const glm::vec4 clip = viewProj * glm::vec4(t.position, 1.0f);
                    if (clip.w <= 0.1f || glm::distance(t.position, camera.position) > 80.0f)
                        continue;
                    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
                    if (std::abs(ndc.x) > 1.1f || std::abs(ndc.y) > 1.1f)
                        continue;
                    const glm::vec2 px((ndc.x * 0.5f + 0.5f) * width, (0.5f - ndc.y * 0.5f) * height);
                    const float size = 15.0f;
                    ui.text(t.text, px - glm::vec2(ui.textWidth(t.text, size) * 0.5f, size), size, t.color, true);
                }
            }
            menu.draw(ui, width, height, now);
            console.draw(ui, width, height, now);
            ui.end();
            switch (menu.takeAction()) {
            case GameMenu::Action::Play:
            case GameMenu::Action::Resume:
                menu.setStarted(true);
                menu.close();
                break;
            case GameMenu::Action::Respawn:
                player.respawn();
                scripts.setPlayerHealth(scene.player.maxHealth);
                anomalies.resetDose();
                menu.close();
                break;
            case GameMenu::Action::MainMenu: menu.open(GameMenu::Screen::Main); break;
            case GameMenu::Action::Restart: exitRequest = {LevelExit::Load, levelPath}; break;
            case GameMenu::Action::LoadLevel: exitRequest = {LevelExit::Load, menu.chosenLevel()}; break;
            case GameMenu::Action::LoadSave: exitRequest = {LevelExit::LoadSave, menu.chosenSave()}; break;
            case GameMenu::Action::SaveSlot:
                if (saveTo(menu.chosenSlot()))
                    menu.close();
                break;
            case GameMenu::Action::Quit: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
            case GameMenu::Action::None: break;
            }
            if (screenshotFile && frameLimit > 0 && frame + 1 == frameLimit) { // with the HUD
                const std::vector<std::uint8_t> rgb = renderer.readPixels(width, height);
                std::ofstream shot(screenshotFile, std::ios::binary);
                shot << "P6\n" << width << ' ' << height << "\n255\n";
                shot.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
            }
        }
        {
            ENGINE_ZONE("Swap buffers");
            glfwSwapBuffers(window);
        }
        ENGINE_FRAME_MARK;
        if (const int maxFps = cvFpsMax.integer(); maxFps > 0) { // frame limiter (sleep, then spin the last bit)
            const double target = now + 1.0 / maxFps;
            while (glfwGetTime() < target - 0.0015)
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            while (glfwGetTime() < target) {
            }
        }

        if (DemoState& demo = demoState(); demo.mode == DemoState::Record) {
            demo.current.player = player.feet();
            demo.demo.frames.push_back(demo.current);
        } else if (demo.mode == DemoState::Play) {
            const float off = glm::distance(player.feet(), demo.current.player);
            demo.maxDistance = std::max(demo.maxDistance, off);
            if (off > 0.05f && demo.desyncFrame < 0) {
                demo.desyncFrame = static_cast<long>(demo.next);
                demo.desyncDistance = off;
                LOG_WARN("demo", "desync at frame %zu: player %.2f m from the recorded path", demo.next, off);
            }
            if (++demo.next >= demo.demo.frames.size())
                finishDemo(true);
        }
        ++frame;
        ++fpsFrames;
        fpsTimer += frameTime;
        if (fpsTimer >= 0.5) {
            char title[256];
            char ammo[32];
            if (weapons[current].settings.melee())
                std::snprintf(ammo, sizeof ammo, "melee");
            else
                std::snprintf(ammo, sizeof ammo, "%d/%d%s", weapons[current].ammo(), weapons[current].settings.magazineSize,
                              weapons[current].reloading() ? " (reloading)" : "");
            std::snprintf(title, sizeof title, "FPS Engine | HP %.0f | RAD %.0f | [%zu] %s %s | Kills %d | %.0f FPS | %.2f ms | pos %.1f %.1f %.1f%s",
                          scripts.playerHealth(), anomalies.dose(), current + 1, weapons[current].settings.name.c_str(), ammo, kills,
                          fpsFrames / fpsTimer, 1000.0 * fpsTimer / fpsFrames,
                          player.feet().x, player.feet().y, player.feet().z, player.grounded() ? "" : " (air)");
            // FPS_ENGINE_STATS=1 prints frame statistics (profiling, comparing optimizations).
            if (std::getenv("FPS_ENGINE_STATS")) {
                const engine::Renderer::FrameStats& st = renderer.frameStats();
                char line[512];
                int n = std::snprintf(line, sizeof line, "%.1f FPS, %.2f ms | %d draws (%d meshes), %lld tris, %d instanced, %d culled, %d occluded, %d uploads | CPU %.2f ms, GPU %.2f ms |",
                                      fpsFrames / fpsTimer, 1000.0 * fpsTimer / fpsFrames, st.drawCalls, st.meshDraws, st.triangles, st.instanced, st.culled, st.occluded, st.uploads,
                                      st.cpuTotalMs, st.gpuTotalMs);
                n += std::snprintf(line + n, sizeof line - static_cast<std::size_t>(n), " cull %.2f (wait %.2f) |", st.cullMs, st.cullWaitMs);
                for (int p = 0; p < engine::Renderer::PassCount && n > 0 && n < static_cast<int>(sizeof line); ++p)
                    n += std::snprintf(line + n, sizeof line - static_cast<std::size_t>(n), " %s %.2f/%.2f", engine::Renderer::passName(p), st.cpuMs[p], st.gpuMs[p]);
                // Frame update tasks (last frame): what runs side by side before rendering.
                for (std::size_t t = 0; t < frameGraph.size() && n > 0 && n < static_cast<int>(sizeof line); ++t)
                    n += std::snprintf(line + n, sizeof line - static_cast<std::size_t>(n), "%s %s %.2f", t == 0 ? " | tasks:" : ",",
                                       frameGraph.taskName(t), frameGraph.taskMs(t));
                LOG_INFO("stats", "%s", line);
            }
            glfwSetWindowTitle(window, title);
            if (windowMode.mode == 0 && !glfwGetWindowAttrib(window, GLFW_ICONIFIED)) { // remembered for the next start
                int ww = 0, wh = 0;
                glfwGetWindowSize(window, &ww, &wh);
                if (ww > 0 && wh > 0 && (ww != cvars.find("r_width")->integer() || wh != cvars.find("r_height")->integer())) {
                    cvars.set("r_width", std::to_string(ww));
                    cvars.set("r_height", std::to_string(wh));
                }
            }
            fpsTimer = 0.0;
            fpsFrames = 0;
        }
    }

    if (frameLimit >= 0) // smoke tests: where the run ended
        LOG_INFO("game", "End: pos %.2f %.2f %.2f | HP %.1f | dose %.1f rad | kills %d", player.feet().x, player.feet().y, player.feet().z,
                    scripts.playerHealth(), anomalies.dose(), kills);
    if (frameLimit >= 0 && scene.streaming.enabled) {
        const engine::WorldStreamer::Stats s = streamer.stats();
        LOG_INFO("game", "Streaming: %d/%d cells loaded (%d loading), %d models in memory, %d deferred, %.1f MB meshes",
                 s.loadedCells, s.cells, s.loadingCells, s.residentModels, s.deferredModels,
                 static_cast<double>(s.meshBytes) / (1024.0 * 1024.0));
    }
    if (DemoState& demo = demoState(); demo.mode == DemoState::Record) {
        // The recording covers this level session; a level change ends it.
        std::string demoError;
        if (engine::saveDemo(demo.demo, demo.file, &demoError))
            LOG_INFO("demo", "saved %s: %zu frames, %.1f s", demo.file.string().c_str(), demo.demo.frames.size(), demo.demo.duration());
        else
            LOG_ERROR("demo", "%s", demoError.c_str());
        demo.mode = DemoState::Off;
    } else if (demo.mode == DemoState::Play) {
        finishDemo(false);
    }
    if (!demoState().replayedSettings) // a replay's settings are the recording's, not the player's
        cvars.saveConfig((engine::userDirectory("config") / "game.cfg").string());
    navCancel = true;
    if (navBuilder.joinable())
        navBuilder.join();
    map.release(); // GL objects go while the context exists
    // Console bindings that point at this level's objects go with it.
    for (const char* command : {"quit", "pos", "config_save", "give", "teleport", "map", "changelevel", "save", "load", "flashlight"})
        cvars.removeCommand(command);
    cvars.clearCallbacks();
    engine::DebugDraw::global().clear();
    if (exitRequest.kind == LevelExit::None)
        exitRequest.kind = LevelExit::Quit;
    if (exitRequest.kind == LevelExit::Change) { // what the player takes along
        carryOut.health = scripts.playerHealth();
        carryOut.weapon = current;
        carryOut.ammo.clear();
        for (const engine::Weapon& w : weapons)
            carryOut.ammo.push_back(w.ammo());
        carryOut.weaponFiles = inventory.weaponFiles;
        carryOut.stock = inventory.ammo;
        carryOut.medkits = inventory.medkits;
        carryOut.money = inventory.money;
        carryOut.condition.clear();
        for (const engine::Weapon& w : weapons)
            carryOut.condition.push_back(w.condition);
        carryOut.armor = inventory.armor;
        carryOut.armorCondition = inventory.armorCondition;
        carryOut.artifacts = inventory.artifacts;
    }
    return exitRequest;
    };

    // Levels one after another: restart and level change load a fresh session.
    std::optional<Carry> carry;
    std::optional<SaveGame> loading;
    for (;;) {
        Carry carryOut;
        const LevelExit result = playLevel(levelPath, carry ? &*carry : nullptr, carryOut, loading ? &*loading : nullptr);
        firstLevel = false;
        if (result.kind == LevelExit::Quit || glfwWindowShouldClose(window) || (frameLimit >= 0 && frame >= frameLimit))
            break;
        carry.reset();
        loading.reset();
        if (result.kind == LevelExit::LoadSave) {
            SaveGame save;
            std::string saveError;
            const bool readOk = save.read(result.level, &saveError);
            if (readOk && save.level.is_relative())
                save.level = std::filesystem::path(argv[0]).parent_path() / save.level;
            if (!readOk || !engine::fileExists(save.level)) {
                LOG_ERROR("save", "%s: %s", result.level.string().c_str(), saveError.empty() ? "level file missing" : saveError.c_str());
                continue; // the same level again
            }
            levelPath = save.level;
            loading = std::move(save);
        } else {
            levelPath = result.level;
        }
        if (result.kind == LevelExit::Change)
            carry = carryOut;
        LOG_INFO("game", "Loading %s", levelPath.string().c_str());
    }
    ui.shutdown();
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    engine::markCleanExit();
    // Smoke tests (FPS_ENGINE_FRAMES) fail when anything logged an error: missing assets, shader or GL errors, script errors.
    if (frameLimit > 0) {
        const std::size_t errors = engine::logCount(engine::LogLevel::Error) + engine::logCount(engine::LogLevel::Fatal);
        const std::size_t warnings = engine::logCount(engine::LogLevel::Warning);
        std::printf("smoke: %ld frames, %zu errors, %zu warnings\n", frame, errors, warnings);
        if (errors > 0)
            return 1;
    }
    return 0;
}
