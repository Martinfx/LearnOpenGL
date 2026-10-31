// Headless tests for the engine core (no OpenGL needed).
#include <engine/Camera.h>
#include <engine/FixedStepClock.h>
#include <engine/Physics.h>
#include <engine/PlayerController.h>
#include <engine/Scene.h>
#include <engine/Scripting.h>

#include <fstream>

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
void simulate(engine::PlayerController& p, std::vector<engine::Action> held, float seconds)
{
    engine::FpsCamera cam; // yaw -90 => forward is -Z
    engine::InputState in;
    for (auto a : held)
        in.set(a, true);
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < static_cast<int>(seconds / dt); ++i)
        p.fixedUpdate(in, cam, dt);
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
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController p;
    p.spawn(world, {0, 3, 0});
    simulate(p, {}, 2.0f);
    check(p.grounded(), "player lands on the floor");
    check(std::abs(p.feet().y) < 0.05f, "feet rest at floor height");
}

void testWallBlocks()
{
    auto s = flatFloor();
    s.boxes.push_back({"Wall", {0, 2, -5}, {10, 4, 1}, {1, 1, 1}});
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController p;
    p.spawn(world, {0, 0, 0});
    simulate(p, {engine::Action::MoveForward}, 3.0f);
    const float wallFront = -4.5f + p.settings.radius;
    check(p.feet().z >= wallFront - 0.001f, "wall stops the player");
    check(p.feet().z < wallFront + 0.1f, "player reaches the wall");
}

void testClimbsSteps()
{
    auto s = flatFloor();
    for (int i = 0; i < 5; ++i) {
        const float h = 0.3f * static_cast<float>(i + 1);
        s.boxes.push_back({"Step", {0, h * 0.5f, -2.0f - i * 0.6f}, {3, h, 0.6f}, {1, 1, 1}});
    }
    s.boxes.push_back({"Landing", {0, 0.75f, -10.0f}, {3, 1.5f, 10}, {1, 1, 1}});
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController p;
    p.spawn(world, {0, 0, 0});
    simulate(p, {engine::Action::MoveForward}, 3.0f);
    check(std::abs(p.feet().y - 1.5f) < 0.05f, "player walks up 0.3 m steps onto the landing");
}

void testJump()
{
    auto s = flatFloor();
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController p;
    p.spawn(world, {0, 0, 0});
    simulate(p, {}, 0.2f);
    float peak = 0.0f;
    engine::FpsCamera cam;
    engine::InputState in;
    in.set(engine::Action::Jump, true);
    for (int i = 0; i < 90; ++i) {
        p.fixedUpdate(in, cam, 1.0f / 60.0f);
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

void testGltfRamp()
{
    engine::Model ramp;
    std::string err;
    check(engine::loadModelFile(ramp, ENGINE_TEST_ASSETS "/models/ramp.gltf", &err), "load ramp.gltf: " + err);
    check(ramp.parts.size() == 1 && ramp.parts[0].indices.size() == 36, "ramp has 12 triangles");
    check(std::abs(ramp.localBounds.max.y - 2.0f) < 1e-4f, "ramp is 2 m high");

    // Walk up the 18 degree glTF ramp: collision comes from Jolt's MeshShape.
    auto s = flatFloor();
    s.models.push_back(ramp);
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController p;
    p.spawn(world, {0, 0, 1});
    simulate(p, {engine::Action::MoveForward}, 2.0f); // ~9 m: ramp is 6 m, landing ends at 10 m
    check(std::abs(p.feet().y - 2.0f) < 0.05f, "player walks up the ramp onto the landing");
}

void testLevelWithModel()
{
    engine::Scene s;
    std::string err;
    check(engine::loadScene(s, ENGINE_TEST_ASSETS "/levels/test.level", &err), "load test.level: " + err);
    check(s.models.size() == 1, "test level references one glTF model");
    check(engine::saveScene(s, "with_model.level", &err), "save: " + err);
    engine::Scene t;
    // Saved next to the test binary: the model path must be rewritten relative to it.
    check(engine::loadScene(t, "with_model.level", &err), "reload: " + err);
    check(t.models.size() == 1 && !t.models[0].parts.empty(), "model path survives save + load");
}

void testRaycast()
{
    auto s = flatFloor();
    engine::PhysicsWorld world;
    world.setLevel(s);
    auto hit = world.raycast({0, 5, 0}, {0, -1, 0}, 100.0f);
    check(hit && std::abs(hit->distance - 5.0f) < 1e-3f, "raycast hits the floor 5 m below");
    check(!world.raycast({0, 5, 0}, {0, 1, 0}, 100.0f), "raycast upward hits nothing");
}

engine::Entity enemyAt(glm::vec3 pos, float yaw)
{
    engine::Entity e;
    e.name = "Guard";
    e.type = "enemy";
    e.position = pos;
    e.yawDegrees = yaw;
    e.scriptPath = ENGINE_TEST_ASSETS "/scripts/enemy.lua";
    return e;
}

// Runs player (standing still) + scripts for `seconds`.
float runAi(engine::Scene& s, float seconds, std::vector<std::string>* log = nullptr)
{
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController player;
    player.spawn(world, s.playerStart);
    engine::ScriptSystem scripts(s, world, player, [log](const std::string& m) {
        if (log)
            log->push_back(m);
    });
    check(scripts.start() == 0, "enemy.lua starts without errors");
    engine::FpsCamera cam;
    engine::InputState idle;
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) {
        player.fixedUpdate(idle, cam, 1.0f / 60.0f);
        scripts.fixedUpdate(1.0f / 60.0f);
    }
    return scripts.playerHealth();
}

void testScriptApi()
{
    const char* path = "api_test.lua";
    std::ofstream(path) << R"(
        function on_start(self)
            self:enable_character()
            steps = 0
        end
        function on_update(self, dt)
            steps = steps + 1
            local to = game.player_position() - self.position
            self:move(to, 3.0)
            self:face(game.player_position())
            assert(vec3(1, 2, 3):length() > 3.7)
        end
    )";
    auto s = flatFloor();
    s.playerStart = {0, 0, 0};
    engine::Entity e;
    e.position = {10, 0, 0};
    e.scriptPath = std::filesystem::absolute(path);
    s.entities.push_back(e);
    runAi(s, 1.0f);
    check(s.entities[0].position.x < 7.5f, "scripted entity walks toward the player");
    check(std::abs(s.entities[0].yawDegrees - 180.0f) < 1.0f, "face() turns it toward the player");
}

void testScriptErrorIsReported()
{
    const char* path = "broken.lua";
    std::ofstream(path) << "function on_update(self, dt) local x = nil + 1 end";
    auto s = flatFloor();
    engine::Entity e;
    e.name = "Broken";
    e.scriptPath = std::filesystem::absolute(path);
    s.entities.push_back(e);
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController player;
    player.spawn(world, {0, 0, 0});
    std::vector<std::string> log;
    engine::ScriptSystem scripts(s, world, player, [&](const std::string& m) { log.push_back(m); });
    scripts.start();
    for (int i = 0; i < 10; ++i)
        scripts.fixedUpdate(1.0f / 60.0f);
    check(scripts.errorCount() == 1, "a failing on_update is reported once, then disabled");
    check(!log.empty() && log[0].find("Broken") != std::string::npos, "error names the entity");
}

void testEnemyChasesAndAttacks()
{
    auto s = flatFloor();
    s.playerStart = {0, 0, 0};
    s.entities.push_back(enemyAt({8, 0, 0}, 180.0f)); // facing the player, 8 m away
    std::vector<std::string> log;
    const float health = runAi(s, 4.0f, &log);
    check(health < 100.0f, "enemy that sees the player chases and hits him");
    bool chased = false;
    for (const auto& l : log)
        chased = chased || l.find("patrol -> chase") != std::string::npos;
    check(chased, "enemy switches from patrol to chase");
}

void testEnemyBlockedBySight()
{
    auto s = flatFloor();
    s.playerStart = {0, 0, 0};
    // Tall wall between them, enemy facing away from where the player could be seen around it.
    s.boxes.push_back({"Wall", {4, 2, 0}, {0.5f, 4, 30}, {1, 1, 1}});
    s.entities.push_back(enemyAt({8, 0, 0}, 180.0f));
    const float health = runAi(s, 2.0f);
    check(health == 100.0f, "enemy behind a wall does not notice the player");
}

void testPick()
{
    auto s = flatFloor();
    s.boxes.push_back({"Target", {0, 1, -10}, {1, 1, 1}, {1, 1, 1}});
    check(engine::pickObject(s, {0, 1, 0}, {0, 0, -1}) == 1, "ray hits the box in front");
    check(engine::pickObject(s, {0, 5, 0}, {0, 1, 0}) == -1, "ray into the sky hits nothing");
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
    testGltfRamp();
    testLevelWithModel();
    testRaycast();
    testScriptApi();
    testScriptErrorIsReported();
    testEnemyChasesAndAttacks();
    testEnemyBlockedBySight();
    testPick();
    std::printf(failures ? "%d test(s) failed\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
