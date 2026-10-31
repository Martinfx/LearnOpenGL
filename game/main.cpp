// Game runtime: GLFW window, fixed 60 Hz simulation, first-person player.
// Usage: fps_game [level-file]
#include <engine/Camera.h>
#include <engine/FixedStepClock.h>
#include <engine/Input.h>
#include <engine/Physics.h>
#include <engine/PlayerController.h>
#include <engine/Renderer.h>
#include <engine/Scene.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

struct Mouse {
    double lastX = 0.0, lastY = 0.0;
    bool first = true;
    glm::vec2 delta{0.0f};
};

void readInput(GLFWwindow* window, engine::InputState& input)
{
    using engine::Action;
    input.set(Action::MoveForward, glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS);
    input.set(Action::MoveBack, glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
    input.set(Action::MoveLeft, glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS);
    input.set(Action::MoveRight, glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS);
    input.set(Action::Jump, glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
    input.set(Action::Sprint, glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);
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

    // Level: command line argument, else the bundled sample, else built-in default.
    engine::Scene scene;
    std::filesystem::path levelPath = argc > 1 ? std::filesystem::path(argv[1])
                                               : std::filesystem::path(argv[0]).parent_path() / "assets/levels/test.level";
    if (!engine::loadScene(scene, levelPath, &error)) {
        std::fprintf(stderr, "%s, using the built-in level\n", error.c_str());
        scene = engine::makeDefaultScene();
    }

    engine::FpsCamera camera;
    engine::PhysicsWorld physics;
    physics.setLevel(scene);
    engine::PlayerController player;
    player.spawn(physics, scene.playerStart);
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

        const int steps = clock.advance(frameTime);
        for (int i = 0; i < steps; ++i) {
            const float dt = static_cast<float>(clock.step());
            player.fixedUpdate(input, camera, dt);
            physics.step(dt);
        }

        camera.position = player.interpolatedEye(static_cast<float>(clock.alpha()));

        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width > 0 && height > 0) {
            engine::RenderView view;
            view.view = camera.view();
            view.projection = camera.projection(static_cast<float>(width) / static_cast<float>(height));
            view.cameraPosition = camera.position;
            view.viewportWidth = width;
            view.viewportHeight = height;
            renderer.render(scene, view);
        }
        glfwSwapBuffers(window);

        ++frame;
        ++fpsFrames;
        fpsTimer += frameTime;
        if (fpsTimer >= 0.5) {
            char title[160];
            std::snprintf(title, sizeof title, "FPS Engine | %.0f FPS | %.2f ms | pos %.1f %.1f %.1f%s",
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
