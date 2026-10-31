// Headless tests for the engine core (no OpenGL needed).
#include <engine/Camera.h>
#include <engine/FixedStepClock.h>
#include <engine/PlayerController.h>
#include <engine/Scene.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const std::string& what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

engine::Scene flatFloor()
{
    engine::Scene s;
    s.boxes.push_back({"Floor", {0, -0.5f, 0}, {100, 1, 100}, {1, 1, 1}});
    return s;
}

// Runs the player for `seconds` holding the given actions, camera facing -Z.
void simulate(engine::PlayerController& p, const engine::Scene& s, std::vector<engine::Action> held, float seconds)
{
    engine::FpsCamera cam; // yaw -90 => forward is -Z
    engine::InputState in;
    for (auto a : held)
        in.set(a, true);
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < static_cast<int>(seconds / dt); ++i)
        p.fixedUpdate(in, cam, s, dt);
}

void testFixedStep()
{
    engine::FixedStepClock c(0.01, 8);
    check(c.advance(0.035) == 3, "35 ms at 10 ms steps gives 3 steps");
    check(std::abs(c.alpha() - 0.5) < 1e-6, "leftover 5 ms gives alpha 0.5");
    check(c.advance(10.0) == 8, "huge frames are clamped to max steps");
}

void testFallsAndLands()
{
    auto s = flatFloor();
    engine::PlayerController p;
    p.spawn({0, 3, 0});
    simulate(p, s, {}, 2.0f);
    check(p.grounded(), "player lands on the floor");
    check(std::abs(p.feet().y) < 0.01f, "feet rest at floor height");
}

void testWallBlocks()
{
    auto s = flatFloor();
    s.boxes.push_back({"Wall", {0, 2, -5}, {10, 4, 1}, {1, 1, 1}});
    engine::PlayerController p;
    p.spawn({0, 0, 0});
    simulate(p, s, {engine::Action::MoveForward}, 3.0f);
    const float wallFront = -4.5f + p.settings.halfWidth;
    check(p.feet().z >= wallFront - 0.01f, "wall stops the player");
    check(p.feet().z < wallFront + 0.05f, "player reaches the wall");
}

void testClimbsSteps()
{
    auto s = flatFloor();
    for (int i = 0; i < 5; ++i) {
        const float h = 0.3f * static_cast<float>(i + 1);
        s.boxes.push_back({"Step", {0, h * 0.5f, -2.0f - i * 0.6f}, {3, h, 0.6f}, {1, 1, 1}});
    }
    s.boxes.push_back({"Landing", {0, 0.75f, -10.0f}, {3, 1.5f, 10}, {1, 1, 1}});
    engine::PlayerController p;
    p.spawn({0, 0, 0});
    simulate(p, s, {engine::Action::MoveForward}, 3.0f);
    check(std::abs(p.feet().y - 1.5f) < 0.01f, "player walks up 0.3 m steps onto the landing");
}

void testJump()
{
    auto s = flatFloor();
    engine::PlayerController p;
    p.spawn({0, 0, 0});
    simulate(p, s, {}, 0.2f);
    float peak = 0.0f;
    engine::FpsCamera cam;
    engine::InputState in;
    in.set(engine::Action::Jump, true);
    for (int i = 0; i < 90; ++i) {
        p.fixedUpdate(in, cam, s, 1.0f / 60.0f);
        peak = std::max(peak, p.feet().y);
    }
    // v^2 / 2g = 5.5^2 / 36 = 0.84 m
    check(peak > 0.75f && peak < 0.9f, "jump height is about 0.84 m");
    check(p.grounded(), "player lands after the jump");
}

void testSceneRoundTrip()
{
    engine::Scene s = engine::makeDefaultScene();
    s.boxes[0].name = "Name with \"quotes\" and spaces";
    std::string err;
    check(engine::saveScene(s, "roundtrip.level", &err), "save: " + err);
    engine::Scene t;
    check(engine::loadScene(t, "roundtrip.level", &err), "load: " + err);
    check(t.boxes.size() == s.boxes.size(), "same number of boxes");
    check(!t.boxes.empty() && t.boxes[0].name == s.boxes[0].name, "names survive quoting");
}

void testPick()
{
    auto s = flatFloor();
    s.boxes.push_back({"Target", {0, 1, -10}, {1, 1, 1}, {1, 1, 1}});
    check(engine::pickBox(s, {0, 1, 0}, {0, 0, -1}) == 1, "ray hits the box in front");
    check(engine::pickBox(s, {0, 5, 0}, {0, 1, 0}) == -1, "ray into the sky hits nothing");
}
} // namespace

int main()
{
    testFixedStep();
    testFallsAndLands();
    testWallBlocks();
    testClimbsSteps();
    testJump();
    testSceneRoundTrip();
    testPick();
    std::printf(failures ? "%d test(s) failed\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
