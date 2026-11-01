// Game runtime: GLFW window, fixed 60 Hz simulation, first-person player.
// Usage: fps_game [level-file]
#include <engine/Log.h>
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
#include <engine/PlayerController.h>
#include <engine/Renderer.h>
#include <engine/Scene.h>
#include <engine/Ui2D.h>

#include "GameAnomalies.h"
#include "GameAudio.h"
#include "GameEffects.h"
#include "GameConsole.h"
#include "GameHud.h"
#include "GameMenu.h"
#include "KeyNames.h"
#include "MapHud.h"
#include <engine/Scripting.h>
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
#include <map>
#include <optional>
#include <thread>
#include <chrono>
#include <functional>
#include <array>
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
    enum Kind { None, Quit, Load, Change } kind = None;
    std::filesystem::path level;
};
struct Carry {
    float health = 100.0f;
    std::size_t weapon = 0;
    std::vector<int> ammo;
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
        std::ifstream in(model.parent_path() / "CREDITS.md");
        const std::string name = model.filename().string();
        for (std::string line; std::getline(in, line);)
            if (line.find(name) != std::string::npos && printed.insert(line).second)
                LOG_INFO("credits", "%s\n", line.c_str());
        std::fflush(stdout);
    }
}

// Player's weapons from the level loadout (falls back to the AK-74M).
std::vector<engine::Weapon> loadLoadout(const engine::Scene& scene, const std::filesystem::path& exeDir)
{
    std::vector<std::filesystem::path> files = scene.loadout;
    if (files.empty())
        files.push_back(exeDir / "assets/weapons/ak74.weapon");
    std::vector<engine::Weapon> weapons;
    std::vector<std::pair<std::filesystem::path, std::shared_ptr<const engine::Model>>> models;
    for (const auto& file : files) {
        engine::WeaponSettings settings;
        std::string error;
        if (!engine::loadWeaponFile(settings, file, &error)) {
            LOG_WARN("weapon", "%s\n", error.c_str());
            continue;
        }
        engine::Weapon weapon(settings);
        auto cached = std::find_if(models.begin(), models.end(), [&](const auto& m) { return m.first == settings.modelPath; });
        if (cached != models.end()) {
            weapon.viewModel = cached->second;
        } else if (!settings.modelPath.empty()) {
            // Skinned arms + weapon keep their skeleton and clips; static guns are baked.
            if (auto model = engine::loadCharacterModel(settings.modelPath, &error); model && model->skeleton) {
                weapon.viewModel = model;
                models.emplace_back(settings.modelPath, model);
            } else if (auto baked = std::make_shared<engine::Model>(); engine::loadModelFile(*baked, settings.modelPath, &error)) {
                weapon.viewModel = baked;
                models.emplace_back(settings.modelPath, baked);
            } else {
                LOG_WARN("weapon", "%s\n", error.c_str());
            }
        }
        weapons.push_back(std::move(weapon));
    }
    printModelCredits(files, weapons);
    if (weapons.empty())
        weapons.emplace_back(); // bare default so the game stays playable
    return weapons;
}

// Ricochet dice (gameplay randomness, not reproducible on purpose).
std::mt19937& ricochetRng()
{
    static std::mt19937 rng{std::random_device{}()};
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

bool keyDown(GLFWwindow* window, int code)
{
    if (code < 0)
        return false;
    if (code >= kMouseBase)
        return glfwGetMouseButton(window, code - kMouseBase) == GLFW_PRESS;
    return glfwGetKey(window, code) == GLFW_PRESS;
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

glm::mat4 weaponTransform(const engine::Weapon& weapon, float time, float moveSpeed, float lowered)
{
    // Aiming brings the sights to the eye and steadies the walk bob.
    const float aim = smoothAim(weapon);
    const float bob = std::min(moveSpeed / 5.0f, 1.5f) * (1.0f - 0.85f * aim);
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
    if (std::getenv("FPS_ENGINE_NO_INSTANCING") || std::getenv("FPS_ENGINE_NO_OCCLUSION")) {
        engine::RenderQuality q = renderer.quality();
        q.instancing = !std::getenv("FPS_ENGINE_NO_INSTANCING");
        q.occlusionCulling = !std::getenv("FPS_ENGINE_NO_OCCLUSION");
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
        } else if (arg == "--shaders" && i + 1 < argc) {
            if (!renderer.reloadShaders(argv[++i], &error))
                LOG_WARN("shaders", "%s\n", error.c_str());
        } else if (arg.rfind("--", 0) != 0) { // options like --no-menu are read elsewhere
            levelPath = arg;
        }
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
        if (auto* m = static_cast<Mouse*>(glfwGetWindowUserPointer(w)); m->console)
            m->console->onChar(codepoint);
    });
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int mods) {
        auto* m = static_cast<Mouse*>(glfwGetWindowUserPointer(w));
        if (!m->console)
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
    // FPS_ENGINE_FRAMES=N quits after N frames (used for smoke tests in CI).
    const char* frameLimitEnv = std::getenv("FPS_ENGINE_FRAMES");
    const long frameLimit = frameLimitEnv ? std::strtol(frameLimitEnv, nullptr, 10) : -1;
    long frame = 0;
    bool firstLevel = true;
    WindowMode windowMode;

    // One level from loading to leaving it (quit, restart, another level). Everything
    // level-scoped lives in here; the window, renderer, UI, console and menu outlive it.
    auto playLevel = [&](const std::filesystem::path& levelPath, const Carry* carry, Carry& carryOut) -> LevelExit {
    LevelExit exitRequest; // set to leave the level (menu, console `map`, game.change_level)
    engine::setCrashContext("level", levelPath.string());
    engine::Scene scene;
    engine::PhysicsWorld physics;
    MapHud map;
    std::string levelTitle = levelPath.stem().string();
    {
        const engine::LoadingScreen look = engine::readLoadingScreen(levelPath);
        int imageW = 0, imageH = 0;
        const unsigned background = look.image.empty() ? 0u : engine::Ui2D::loadImage(look.image, &imageW, &imageH);
        const std::string title = look.title.empty() ? levelPath.stem().string() : look.title;
        levelTitle = title;
        engine::LoadProgress progress;
        std::atomic<bool> finished{false};
        std::string loadError;
        bool loaded = false;
        std::thread loader([&] {
            loaded = engine::loadScene(scene, levelPath, &loadError, &progress);
            if (!loaded)
                scene = engine::makeDefaultScene();
            progress.setStep("physics");
            physics.setLevel(scene);
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
            if (!look.hint.empty())
                ui.text(look.hint, {margin, barY - textSize * 1.9f}, textSize, dim);
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
            ui.text("Loading " + step, {margin, barY + barH + textSize * 0.5f}, textSize, dim);
            ui.end();
            glfwSwapBuffers(window);
            glfwPollEvents(); // the window stays responsive (moving, resizing)
        };
        while (!finished)
            drawLoading(0.85f * progress.fraction(), progress.step());
        loader.join();
        if (!loaded)
            LOG_ERROR("level", "%s, using the built-in level", loadError.c_str());
        std::size_t cursor = 0;
        for (float uploaded = 0.0f; uploaded < 1.0f;) {
            uploaded = renderer.preload(scene, cursor, 12.0, scene.playerStart); // full textures near the start
            drawLoading(0.85f + 0.15f * uploaded, "textures to the GPU");
        }
        drawLoading(1.0f, "map");
        map.init(renderer, scene);
        LOG_INFO("level", "%s loaded in %.1f s", levelPath.filename().string().c_str(), glfwGetTime() - loadStart);
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

    // Audio: mixing runs on miniaudio's device thread, files decode on its job thread.
    GameAudio audio;
    audio.init(std::filesystem::path(argv[0]).parent_path() / "assets");

    // Navigation mesh: built from the level collision in the background (tiles in
    // parallel); enemies walk straight lines until it is ready.
    engine::NavMesh navmesh;
    std::thread navBuilder;
    std::atomic<bool> navCancel{false};
    if (scene.nav.enabled)
        navBuilder = std::thread([&] {
            std::string navError;
            if (navmesh.build(scene, scene.nav, &navError, nullptr, &navCancel))
                LOG_INFO("navmesh", "%d tiles, %d polygons in %.1f s\n", navmesh.tileCount(), navmesh.polygonCount(),
                            navmesh.buildSeconds());
            else if (!navCancel) // leaving the level early is not a problem
                LOG_WARN("navmesh", "%s", navError.c_str());
            std::fflush(stdout);
        });

    engine::ScriptSystem scripts(scene, physics, player);
    scripts.setNavMesh(&navmesh);
    scripts.setPlayerHealth(scene.player.maxHealth);
    scripts.setSoundHandler([&audio](const std::string& sound, const glm::vec3* position, float volume) {
        audio.playAsset(sound, position, volume);
    });

    // Weapons (credits of the models: CREDITS.md next to each model)
    std::vector<engine::Weapon> weapons = loadLoadout(scene, std::filesystem::path(argv[0]).parent_path());
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
    // Anomalies (gravity traps, discharges, vents, acid, rifts, radiation), detector and Geiger counter.
    GameAnomalies anomalies;
    anomalies.init(std::filesystem::path(argv[0]).parent_path() / "assets", audio);
    anomalies.startLevel(scene);
    bool boltHeld = false;
    bool wasReloading = false;
    if (const int errors = scripts.start())
        LOG_ERROR("script", "%d script error(s), see above", errors);
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
        cvars.add("r_quality", CVarType::Int, "2", "Graphics preset: 0 low, 1 medium, 2 high, 3 ultra", engine::CVarConfig, 0.0, 3.0)
            .onChange = [&renderer, &cvars](const engine::CVar& v) {
                engine::RenderQuality q = engine::RenderQuality::preset(v.integer());
                q.renderScale = cvars.find("r_scale") ? cvars.find("r_scale")->number() : 1.0f;
                renderer.setQuality(q);
            };
        cvars.add("r_scale", CVarType::Float, "1", "Render resolution (0.5 = half; menus and HUD stay sharp)", engine::CVarConfig, 0.5, 1.0)
            .onChange = [&renderer](const engine::CVar& v) {
                engine::RenderQuality q = renderer.quality();
                q.renderScale = v.number();
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
        cvars.add("cl_showfps", CVarType::Bool, "0", "Frame rate in the corner", engine::CVarConfig);
        cvars.addCommand("quit", "Leaves the game", [window](const std::vector<std::string>&) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return engine::ConsoleResult{true, "bye"};
        });
        cvars.addCommand("pos", "Prints the player's position and view", [&player, &camera](const std::vector<std::string>&) {
            char buf[160];
            const glm::vec3 p = player.feet();
            std::snprintf(buf, sizeof buf, "feet %.2f %.2f %.2f, yaw %.1f, pitch %.1f", p.x, p.y, p.z, camera.yawDegrees, camera.pitchDegrees);
            return engine::ConsoleResult{true, buf};
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
        auto levelFile = [&argv](const std::string& name) {
            std::filesystem::path file = name;
            if (file.extension().empty())
                file += ".level";
            if (file.is_relative())
                file = std::filesystem::path(argv[0]).parent_path() / "assets" / "levels" / file;
            return file;
        };
        cvars.addCommand("map", "map NAME: loads assets/levels/NAME.level (without anything carried over)",
                         [&exitRequest, levelFile](const std::vector<std::string>& a) {
            if (a.empty())
                return engine::ConsoleResult{false, "usage: map NAME"};
            const std::filesystem::path file = levelFile(a[0]);
            if (!std::filesystem::exists(file))
                return engine::ConsoleResult{false, "no level " + file.string()};
            exitRequest = {LevelExit::Load, file};
            return engine::ConsoleResult{true, "loading " + file.filename().string()};
        });
        cvars.addCommand("changelevel", "changelevel NAME: goes to another level with health and weapons",
                         [&exitRequest, levelFile](const std::vector<std::string>& a) {
            if (a.empty())
                return engine::ConsoleResult{false, "usage: changelevel NAME"};
            const std::filesystem::path file = levelFile(a[0]);
            if (!std::filesystem::exists(file))
                return engine::ConsoleResult{false, "no level " + file.string()};
            exitRequest = {LevelExit::Change, file};
            return engine::ConsoleResult{true, "changing to " + file.filename().string()};
        });
        cvars.addCommand("give", "give health|ammo|all", [&](const std::vector<std::string>& a) {
            const std::string what = a.empty() ? "all" : a[0];
            if (what == "health" || what == "all")
                scripts.setPlayerHealth(scene.player.maxHealth);
            if (what == "ammo" || what == "all")
                for (engine::Weapon& w : weapons)
                    w.refill();
            if (what != "health" && what != "ammo" && what != "all")
                return engine::ConsoleResult{false, "usage: give health|ammo|all"};
            return engine::ConsoleResult{true, "gave " + what};
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
    menu.setTitle(levelTitle, "Prototyp zóny");
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

    // Test helpers: FPS_ENGINE_AUTOFIRE=1 holds the trigger, FPS_ENGINE_SCREENSHOT=file.ppm saves the last frame.
    const bool autoFire = std::getenv("FPS_ENGINE_AUTOFIRE") != nullptr;
    const char* screenshotFile = std::getenv("FPS_ENGINE_SCREENSHOT");
    if (firstLevel) { // test hooks
        if (const char* pos = std::getenv("FPS_ENGINE_POS")) { // "x y z" feet position (test hook)
            glm::vec3 p(0.0f);
            if (std::sscanf(pos, "%f %f %f", &p.x, &p.y, &p.z) == 3)
                player.spawn(physics, p);
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
    // Carried over from the previous level (level change): health, weapon, magazines.
    if (carry) {
        scripts.setPlayerHealth(std::min(carry->health, scene.player.maxHealth));
        for (std::size_t w = 0; w < weapons.size() && w < carry->ammo.size(); ++w)
            weapons[w].setAmmo(carry->ammo[w]);
        if (carry->weapon < weapons.size()) {
            current = next = carry->weapon;
            weapons[current].drawn();
        }
    }

    double lastTime = glfwGetTime();
    double fpsTimer = 0.0;
    int fpsFrames = 0;
    bool escHeld = false;
    bool padStartHeld = false, padWeaponHeld = false;
    bool mapOpen = false, mapHeld = false;

    while (!glfwWindowShouldClose(window) && (frameLimit < 0 || frame < frameLimit) && exitRequest.kind == LevelExit::None) {
        const double now = glfwGetTime();
        const double frameTime = now - lastTime;
        lastTime = now;

        glfwPollEvents();
        // Esc: closes the console (handled by it), steps back in a menu, closes the map, else pauses.
        static bool consoleWasOpen = false;
        if (console.open() != consoleWasOpen) {
            consoleWasOpen = console.open();
            escHeld = true; // the Esc that closed the console does nothing else
        }
        const bool esc = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !console.open();
        if (esc && !escHeld) {
            if (menu.active())
                menu.back();
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
            glfwGetCursorPos(window, &cx, &cy);
            glfwGetWindowSize(window, &ww, &wh);
            glfwGetFramebufferSize(window, &fw, &fh);
            const glm::vec2 scale(static_cast<float>(fw) / static_cast<float>(std::max(ww, 1)),
                                  static_cast<float>(fh) / static_cast<float>(std::max(wh, 1)));
            menu.mouse(glm::vec2(static_cast<float>(cx), static_cast<float>(cy)) * scale,
                       !console.open() && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS,
                       menu.active() ? mouse.scroll : 0.0);
            if (menu.active())
                mouse.scroll = 0.0;
        }
        // The mouse looks around only while playing (no menu, no console, window focused).
        const bool wantCapture = !menu.active() && !console.open() && glfwGetWindowAttrib(window, GLFW_FOCUSED);
        if (wantCapture != captured) {
            captured = wantCapture;
            setCaptured(window, captured);
            mouse.first = true;
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
            const bool down = captured && glfwGetKey(window, GLFW_KEY_1 + i) == GLFW_PRESS;
            if (down && !slotHeld[static_cast<std::size_t>(i)] && static_cast<std::size_t>(i) < weapons.size())
                next = static_cast<std::size_t>(i);
            slotHeld[static_cast<std::size_t>(i)] = down;
        }
        if (captured && std::abs(mouse.scroll) >= 1.0 && weapons.size() > 1) {
            const int dir = mouse.scroll > 0 ? -1 : 1;
            next = (next + weapons.size() + static_cast<std::size_t>(dir + static_cast<int>(weapons.size()))) % weapons.size();
        }
        mouse.scroll = 0.0;
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
        const double simTime = menu.active() ? 0.0 : frameTime * cvTimescale.number();
        const int steps = clock.advance(simTime);
        for (int i = 0; i < steps; ++i) {
            const float dt = static_cast<float>(clock.step());
            // Debug shapes age per simulation step: "this frame" shapes from scripts stay until the next step redraws them.
            engine::DebugDraw::global().tick(dt);
            player.fixedUpdate(input, camera, dt);

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
            const int shots = weapon.update(dt, ready && (autoFire || input.isDown(engine::Action::Fire)),
                                            ready && input.isDown(engine::Action::Reload));
            if (next != current)
                weapon.aim(1.0f, false); // switching drops the aim
            weapon.aim(dt, ready && input.isDown(engine::Action::Aim));
            if (shots > 0) {
                audio.play(weapon.settings.soundFire, player.eye() + camera.forward() * 0.5f);
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
                    scripts.damageEntity(hit.entity, damage);
                    if (wasAlive && !scene.entities[static_cast<std::size_t>(hit.entity)].alive)
                        ++kills;
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
            if (cvGod.boolean() && scripts.playerHealth() < healthBefore) // god: anomalies do not hurt either
                scripts.setPlayerHealth(healthBefore);
            {
                std::string ground;
                if (player.grounded())
                    if (auto under = physics.raycast(player.feet() + glm::vec3(0.0f, 0.3f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 1.0f))
                        ground = under->surface;
                audio.walk(scene.player, player.feet(), player.velocity(), player.grounded(), dt, ground);
            }
            physics.step(dt);
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
        engine::updateAnimations(scene, static_cast<float>(simTime)); // per frame for smooth motion
        audio.update(static_cast<float>(frameTime), scene, camera.position, camera.forward());
        effects.update(static_cast<float>(simTime), scene, physics, camera.position, audio);
        anomalies.update(static_cast<float>(simTime), physics, camera.position);

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
            engine::RenderView view;
            view.view = camera.view();
            view.projection = camera.projection(static_cast<float>(width) / static_cast<float>(height));
            view.cameraPosition = camera.position;
            view.viewportWidth = width;
            view.viewportHeight = height;
            view.time = static_cast<float>(now);
            view.deferPresent = true; // the weapon layer is drawn before post-processing
            engine::OverlayPass effectsOverlay; // muzzle flashes, drawn with the weapon below
            effects.fill(view, view.view, effectsOverlay);
            anomalies.fill(view, view.view);
            // Debug lines from code and scripts (DebugDraw), with their texts drawn in the HUD below.
            engine::DebugGeometry debugLines;
            std::vector<engine::DebugDraw::Text> debugTexts;
            if (cvDebugDraw.boolean()) {
                engine::DebugDraw::global().collect(debugLines, &debugTexts);
                if (!debugLines.lines.empty())
                    view.debug.push_back(&debugLines);
            }
            renderer.render(scene, view);

            // First-person layer: weapon, muzzle flash and crosshair.
            engine::OverlayPass overlay;
            const float aspect = static_cast<float>(width) / static_cast<float>(height);
            const float aimed = smoothAim(weapons[current]);
            overlay.projection = glm::perspective(glm::radians(glm::mix(60.0f, 50.0f, aimed)), aspect, 0.01f, 10.0f);
            overlay.sunDirectionViewSpace = glm::mat3(view.view) * scene.sunDirection;
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
            } else if (mapOpen)
                map.drawFullMap(ui, width, height, player.feet(), camera.flatForward());
            else
                map.drawMinimap(ui, width, height, player.feet(), camera.flatForward());
            hud.update(static_cast<float>(frameTime), scripts.playerHealth());
            if (!mapOpen && !menu.active()) {
                anomalies.drawHud(ui, width, height);
                const engine::Weapon& w = weapons[current];
                HudState hs;
                hs.health = scripts.playerHealth();
                hs.maxHealth = scene.player.maxHealth;
                hs.weapon = w.settings.name;
                hs.ammo = w.ammo();
                hs.magazine = w.settings.magazineSize;
                hs.melee = w.settings.melee();
                hs.reload = w.reloading() ? w.reloadProgress() : -1.0f;
                hud.draw(ui, width, height, hs);
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
        glfwSwapBuffers(window);
        if (const int maxFps = cvFpsMax.integer(); maxFps > 0) { // frame limiter (sleep, then spin the last bit)
            const double target = now + 1.0 / maxFps;
            while (glfwGetTime() < target - 0.0015)
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            while (glfwGetTime() < target) {
            }
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
                int n = std::snprintf(line, sizeof line, "%.1f FPS, %.2f ms | %d draws, %lld tris, %d instanced, %d culled, %d occluded, %d uploads | CPU %.2f ms, GPU %.2f ms |",
                                      fpsFrames / fpsTimer, 1000.0 * fpsTimer / fpsFrames, st.drawCalls, st.triangles, st.instanced, st.culled, st.occluded, st.uploads,
                                      st.cpuTotalMs, st.gpuTotalMs);
                for (int p = 0; p < engine::Renderer::PassCount && n > 0 && n < static_cast<int>(sizeof line); ++p)
                    n += std::snprintf(line + n, sizeof line - static_cast<std::size_t>(n), " %s %.2f/%.2f", engine::Renderer::passName(p), st.cpuMs[p], st.gpuMs[p]);
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
    cvars.saveConfig((engine::userDirectory("config") / "game.cfg").string());
    navCancel = true;
    if (navBuilder.joinable())
        navBuilder.join();
    map.release(); // GL objects go while the context exists
    // Console bindings that point at this level's objects go with it.
    for (const char* command : {"quit", "pos", "config_save", "give", "teleport", "map", "changelevel"})
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
    }
    return exitRequest;
    };

    // Levels one after another: restart and level change load a fresh session.
    std::optional<Carry> carry;
    for (;;) {
        Carry carryOut;
        const LevelExit result = playLevel(levelPath, carry ? &*carry : nullptr, carryOut);
        firstLevel = false;
        if (result.kind == LevelExit::Quit || glfwWindowShouldClose(window) || (frameLimit >= 0 && frame >= frameLimit))
            break;
        levelPath = result.level;
        carry.reset();
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
