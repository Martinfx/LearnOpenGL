// Game runtime: GLFW window, fixed 60 Hz simulation, first-person player.
// Usage: fps_game [level-file]
#include <engine/Camera.h>
#include <engine/FixedStepClock.h>
#include <engine/Input.h>
#include <engine/Physics.h>
#include <engine/PlayerController.h>
#include <engine/Renderer.h>
#include <engine/Scene.h>
#include <engine/Scripting.h>
#include <engine/Weapon.h>

#include <glm/gtc/matrix_transform.hpp>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

struct Mouse {
    double lastX = 0.0, lastY = 0.0;
    bool first = true;
    glm::vec2 delta{0.0f};
    double scroll = 0.0;
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
                std::printf("Credits: %s\n", line.c_str());
        std::fflush(stdout);
    }
}

// Player's weapons from the level loadout (falls back to the bundled repeater).
std::vector<engine::Weapon> loadLoadout(const engine::Scene& scene, const std::filesystem::path& exeDir)
{
    std::vector<std::filesystem::path> files = scene.loadout;
    if (files.empty())
        files.push_back(exeDir / "assets/weapons/repeater.weapon");
    std::vector<engine::Weapon> weapons;
    std::vector<std::pair<std::filesystem::path, std::shared_ptr<const engine::Model>>> models;
    for (const auto& file : files) {
        engine::WeaponSettings settings;
        std::string error;
        if (!engine::loadWeaponFile(settings, file, &error)) {
            std::fprintf(stderr, "Weapon: %s\n", error.c_str());
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
                std::fprintf(stderr, "Weapon model: %s\n", error.c_str());
            }
        }
        weapons.push_back(std::move(weapon));
    }
    printModelCredits(files, weapons);
    if (weapons.empty())
        weapons.emplace_back(); // bare default so the game stays playable
    return weapons;
}

void readInput(GLFWwindow* window, engine::InputState& input)
{
    using engine::Action;
    input.set(Action::MoveForward, glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS);
    input.set(Action::MoveBack, glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
    input.set(Action::MoveLeft, glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS);
    input.set(Action::MoveRight, glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS);
    input.set(Action::Jump, glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
    input.set(Action::Sprint, glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);
    input.set(Action::Fire, glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    input.set(Action::Reload, glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS);
}

struct Impact {
    glm::vec3 point;
    float age = 0.0f;
    bool onEnemy = false;
};

// View-space transform of the first-person weapon: idle offset, walk bob,
// recoil kick and the reload dip.
glm::mat4 weaponTransform(const engine::Weapon& weapon, float time, float moveSpeed, float lowered)
{
    const float bob = std::min(moveSpeed / 5.0f, 1.5f);
    glm::vec3 offset = weapon.settings.viewOffset;
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
    const glm::vec3 r = weapon.settings.viewRotation;
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
        if (GLFWwindow* w = glfwCreateWindow(1280, 720, "FPS Engine", nullptr, nullptr))
            return w;
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    glfwSetErrorCallback([](int code, const char* message) { std::fprintf(stderr, "GLFW %d: %s\n", code, message); });
    if (!glfwInit())
        return 1;

    GLFWwindow* window = createWindow();
    if (!window) {
        std::fprintf(stderr, "Could not create an OpenGL 4.5+ window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // uncapped, to see real frame times

    engine::Renderer renderer;
    std::string error;
    if (!renderer.init(reinterpret_cast<engine::GlLoader>(glfwGetProcAddress), &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        glfwTerminate();
        return 1;
    }
    std::printf("%s\n", renderer.glInfo().c_str());

    // Command line: [level] [--shaders DIR] (shaders edited in the editor replace the built-in ones).
    std::filesystem::path levelPath = std::filesystem::path(argv[0]).parent_path() / "assets/levels/test.level";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--shaders" && i + 1 < argc) {
            if (!renderer.reloadShaders(argv[++i], &error))
                std::fprintf(stderr, "Shaders: %s\n", error.c_str());
        } else {
            levelPath = arg;
        }
    }

    // Level: command line argument, else the bundled sample, else built-in default.
    engine::Scene scene;
    if (!engine::loadScene(scene, levelPath, &error)) {
        std::fprintf(stderr, "%s, using the built-in level\n", error.c_str());
        scene = engine::makeDefaultScene();
    }

    engine::FpsCamera camera;
    engine::PhysicsWorld physics;
    physics.setLevel(scene);
    engine::PlayerController player;
    player.settings = scene.player; // movement and health come from the level (editor: Level > Player)
    player.spawn(physics, scene.playerStart);

    engine::ScriptSystem scripts(scene, physics, player);
    scripts.setPlayerHealth(scene.player.maxHealth);

    // Weapons (credits of the models: CREDITS.md next to each model)
    std::vector<engine::Weapon> weapons = loadLoadout(scene, std::filesystem::path(argv[0]).parent_path());
    std::size_t current = 0;
    std::size_t next = 0;     // weapon being switched to
    float lowered = 1.0f;     // 0 = ready, 1 = fully lowered (switch animation)
    std::array<bool, 9> slotHeld{};
    std::vector<Impact> impacts;
    int kills = 0;
    if (const int errors = scripts.start())
        std::fprintf(stderr, "%d script error(s), see above\n", errors);
    engine::FixedStepClock clock;
    engine::InputState input;

    Mouse mouse;
    glfwSetWindowUserPointer(window, &mouse);
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        auto* m = static_cast<Mouse*>(glfwGetWindowUserPointer(w));
        if (!m->first)
            m->delta += glm::vec2(static_cast<float>(x - m->lastX), static_cast<float>(y - m->lastY));
        m->first = false;
        m->lastX = x;
        m->lastY = y;
    });
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double y) {
        static_cast<Mouse*>(glfwGetWindowUserPointer(w))->scroll += y;
    });
    bool captured = true;
    setCaptured(window, captured);

    // FPS_ENGINE_FRAMES=N quits after N frames (used for smoke tests in CI).
    const char* frameLimitEnv = std::getenv("FPS_ENGINE_FRAMES");
    const long frameLimit = frameLimitEnv ? std::strtol(frameLimitEnv, nullptr, 10) : -1;

    double lastTime = glfwGetTime();
    double fpsTimer = 0.0;
    int fpsFrames = 0;
    long frame = 0;
    bool escHeld = false;

    while (!glfwWindowShouldClose(window) && (frameLimit < 0 || frame < frameLimit)) {
        const double now = glfwGetTime();
        const double frameTime = now - lastTime;
        lastTime = now;

        glfwPollEvents();
        const bool esc = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (esc && !escHeld) {
            captured = !captured;
            setCaptured(window, captured);
            mouse.first = true;
        }
        escHeld = esc;
        if (!captured && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            captured = true;
            setCaptured(window, captured);
            mouse.first = true;
        }

        // Look is applied every frame for responsiveness; movement runs at a fixed rate.
        if (captured)
            camera.look(mouse.delta);
        mouse.delta = glm::vec2(0.0f);
        readInput(window, input);
        if (!captured)
            input = {};

        // Weapon selection: 1-9 or the mouse wheel.
        for (int i = 0; i < 9; ++i) {
            const bool down = glfwGetKey(window, GLFW_KEY_1 + i) == GLFW_PRESS;
            if (down && !slotHeld[static_cast<std::size_t>(i)] && static_cast<std::size_t>(i) < weapons.size())
                next = static_cast<std::size_t>(i);
            slotHeld[static_cast<std::size_t>(i)] = down;
        }
        if (captured && std::abs(mouse.scroll) >= 1.0 && weapons.size() > 1) {
            const int dir = mouse.scroll > 0 ? -1 : 1;
            next = (next + weapons.size() + static_cast<std::size_t>(dir + static_cast<int>(weapons.size()))) % weapons.size();
        }
        mouse.scroll = 0.0;

        const int steps = clock.advance(frameTime);
        for (int i = 0; i < steps; ++i) {
            const float dt = static_cast<float>(clock.step());
            player.fixedUpdate(input, camera, dt);

            // Lower the current weapon, swap, raise the new one (0.15 s each way).
            if (next != current) {
                lowered = std::min(1.0f, lowered + dt / 0.15f);
                if (lowered >= 1.0f) {
                    current = next;
                    weapons[current].drawn();
                }
            } else {
                lowered = std::max(0.0f, lowered - dt / 0.15f);
            }
            engine::Weapon& weapon = weapons[current];
            const bool ready = next == current && lowered <= 0.0f;
            const int shots = weapon.update(dt, ready && input.isDown(engine::Action::Fire),
                                            ready && input.isDown(engine::Action::Reload));
            const glm::vec3 moveVel = player.velocity();
            weapon.animate(dt, player.grounded() ? std::sqrt(moveVel.x * moveVel.x + moveVel.z * moveVel.z) : 0.0f);
            const int rays = weapon.settings.melee() ? shots : shots * weapon.settings.pellets;
            for (int s = 0; s < rays; ++s) {
                const glm::vec3 eye = player.eye();
                const engine::ShotHit hit =
                    weapon.settings.melee()
                        ? engine::traceMelee(scene, physics, eye, camera.forward(), weapon.settings.range,
                                             weapon.settings.spreadDegrees)
                        : engine::traceShot(scene, physics, eye, weapon.spreadDirection(camera.forward()),
                                            weapon.settings.range);
                if (!hit.hit)
                    continue;
                impacts.push_back({hit.point, 0.0f, hit.entity >= 0});
                if (hit.entity >= 0) {
                    const bool wasAlive = scene.entities[static_cast<std::size_t>(hit.entity)].alive;
                    scripts.damageEntity(hit.entity, weapon.settings.damage);
                    if (wasAlive && !scene.entities[static_cast<std::size_t>(hit.entity)].alive)
                        ++kills;
                }
            }
            for (Impact& impact : impacts)
                impact.age += dt;
            std::erase_if(impacts, [](const Impact& i) { return i.age > (i.onEnemy ? 0.15f : 6.0f); });

            scripts.fixedUpdate(dt);
            physics.step(dt);
            if (scripts.playerHealth() <= 0.0f) {
                std::printf("You died. Respawning.\n");
                player.respawn();
                scripts.setPlayerHealth(scene.player.maxHealth);
            }
        }

        camera.position = player.interpolatedEye(static_cast<float>(clock.alpha()));
        engine::updateAnimations(scene, static_cast<float>(frameTime)); // per frame for smooth motion

        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width > 0 && height > 0) {
            engine::RenderView view;
            view.view = camera.view();
            view.projection = camera.projection(static_cast<float>(width) / static_cast<float>(height));
            view.cameraPosition = camera.position;
            view.viewportWidth = width;
            view.viewportHeight = height;
            view.time = static_cast<float>(now);
            view.deferPresent = true; // the weapon layer is drawn before post-processing
            for (const Impact& impact : impacts)
                view.worldBoxes.push_back({impact.point, glm::vec3(impact.onEnemy ? 0.12f : 0.06f),
                                           impact.onEnemy ? glm::vec3(1.0f, 0.2f, 0.1f) : glm::vec3(0.08f), impact.onEnemy});
            renderer.render(scene, view);

            // First-person layer: weapon, muzzle flash and crosshair.
            engine::OverlayPass overlay;
            const float aspect = static_cast<float>(width) / static_cast<float>(height);
            overlay.projection = glm::perspective(glm::radians(60.0f), aspect, 0.01f, 10.0f);
            overlay.sunDirectionViewSpace = glm::mat3(view.view) * scene.sunDirection;
            const engine::Weapon& weapon = weapons[current];
            overlay.model = weapon.viewModel.get();
            const glm::vec3 vel = player.velocity();
            const float moveSpeed = player.grounded() ? std::sqrt(vel.x * vel.x + vel.z * vel.z) : 0.0f;
            overlay.modelTransform = weaponTransform(weapon, static_cast<float>(now), moveSpeed, lowered);
            overlay.animator = &weapon.animator;
            if (weapon.flash() && !weapon.settings.melee()) {
                const glm::vec3 muzzle = glm::vec3(overlay.modelTransform * glm::vec4(weapon.settings.muzzle, 1.0f));
                overlay.boxes.push_back({muzzle, glm::vec3(0.05f), glm::vec3(1.0f, 0.85f, 0.35f), true});
            }
            const float gap = 0.008f, len = 0.012f, thick = 0.0016f;
            const glm::vec3 white(0.95f);
            overlay.boxes.push_back({{0, gap + len * 0.5f, -1}, {thick, len, thick}, white, true});
            overlay.boxes.push_back({{0, -gap - len * 0.5f, -1}, {thick, len, thick}, white, true});
            overlay.boxes.push_back({{gap + len * 0.5f, 0, -1}, {len, thick, thick}, white, true});
            overlay.boxes.push_back({{-gap - len * 0.5f, 0, -1}, {len, thick, thick}, white, true});
            renderer.renderOverlay(view, overlay);
            renderer.present();
        }
        glfwSwapBuffers(window);

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
            std::snprintf(title, sizeof title, "FPS Engine | HP %.0f | [%zu] %s %s | Kills %d | %.0f FPS | %.2f ms | pos %.1f %.1f %.1f%s",
                          scripts.playerHealth(), current + 1, weapons[current].settings.name.c_str(), ammo, kills,
                          fpsFrames / fpsTimer, 1000.0 * fpsTimer / fpsFrames,
                          player.feet().x, player.feet().y, player.feet().z, player.grounded() ? "" : " (air)");
            glfwSetWindowTitle(window, title);
            fpsTimer = 0.0;
            fpsFrames = 0;
        }
    }

    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
