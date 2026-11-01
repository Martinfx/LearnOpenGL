// Headless tests for the engine core (no OpenGL needed).
#include <engine/Log.h>
#include <set>
#include <engine/Assert.h>
#include <engine/Anomaly.h>
#include <engine/Lights.h>
#include <engine/EntityIO.h>
#include <engine/Ambience.h>
#include <engine/AssetPalette.h>
#include <engine/Particles.h>
#include <engine/Audio.h>
#include <engine/NavMesh.h>
#include <engine/Surfaces.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <engine/Gizmo.h>
#include <engine/Camera.h>
#include <engine/FixedStepClock.h>
#include <engine/Physics.h>
#include <engine/PlayerController.h>
#include <engine/Scene.h>
#include <engine/Scripting.h>
#include <engine/JobSystem.h>
#include <engine/Material.h>
#include <engine/TextureCook.h>
#include <algorithm>
#include <filesystem>
#include <engine/SceneGraph.h>
#include <engine/Weapon.h>

#include <atomic>
#include <fstream>
#include <glm/geometric.hpp>

#include <cmath>
#include <cstdio>
#include <engine/CVars.h>
#include <engine/Water.h>
#include <engine/PlayerController.h>

#include <functional>
#include <string>
#include <vector>
#include <map>

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
    s.environment.autoExposure = 0.75f;
    s.environment.exposureMaxEv = 1.5f;
    s.environment.temperature = -0.3f;
    s.environment.lift = {0.01f, 0.0f, 0.02f};
    s.environment.lut = std::filesystem::absolute("luts/look.png");
    s.environment.lutStrength = 0.6f;
    s.player.flashlightBattery = 120.0f;
    s.player.flashlightColor = {1.0f, 0.9f, 0.7f};
    std::string err;
    check(engine::saveScene(s, "roundtrip.level", &err), "save: " + err);
    engine::Scene t;
    check(engine::loadScene(t, "roundtrip.level", &err), "load: " + err);
    check(t.boxes.size() == s.boxes.size(), "same number of boxes");
    check(!t.boxes.empty() && t.boxes[0].name == s.boxes[0].name, "names survive quoting");
    const engine::Environment& e = t.environment;
    check(e.autoExposure == 0.75f && e.exposureMaxEv == 1.5f && e.temperature == -0.3f && e.lift == s.environment.lift &&
              e.lutStrength == 0.6f && e.lut.filename() == "look.png",
          "auto-exposure and grading settings survive save/load");
    check(t.player.flashlightBattery == 120.0f && t.player.flashlightColor == s.player.flashlightColor, "flashlight settings survive save/load");
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
    check(s.models.size() > 40 && engine::findObject(s, "Ramp") >= 0, "test level references the ramp and the zone street");
    const int panel = engine::findObject(s, "Panel block");
    check(panel >= 0 && s.model(panel).lod && s.model(panel).lodDistance == 90.0f, "panel block has its LOD loaded");
    const std::size_t modelCount = s.models.size();
    check(engine::saveScene(s, "with_model.level", &err), "save: " + err);
    engine::Scene t;
    // Saved next to the test binary: the model path must be rewritten relative to it.
    check(engine::loadScene(t, "with_model.level", &err), "reload: " + err);
    check(t.models.size() == modelCount && !t.models[0].parts.empty(), "model path survives save + load");
    const int panelAgain = engine::findObject(t, "Panel block");
    check(panelAgain >= 0 && t.model(panelAgain).lod && !t.model(panelAgain).lodPath.empty(), "LOD survives save + load");

    // A farther LOD level: picked by distance, saved as a second `lod` line.
    engine::Model& m = t.model(panelAgain);
    engine::Model::FarLod far;
    far.path = m.lodPath;
    far.model = m.lod;
    far.distance = 300.0f;
    m.farLods.push_back(far);
    check(&m.lodFor(10.0f) == &m && &m.lodFor(100.0f) == m.lod.get() && &m.lodFor(400.0f) == m.farLods[0].model.get() &&
              &m.lodFor(150.0f, 2.0f) == &m,
          "LOD level by distance and lod scale");
    check(engine::saveScene(t, "with_far_lod.level", &err), "save far LOD: " + err);
    engine::Scene u;
    check(engine::loadScene(u, "with_far_lod.level", &err), "reload far LOD: " + err);
    const int panelThird = engine::findObject(u, "Panel block");
    check(panelThird >= 0 && u.model(panelThird).farLods.size() == 1 && u.model(panelThird).farLods[0].model &&
              u.model(panelThird).farLods[0].distance == 300.0f && u.model(panelThird).lodDistance == 90.0f,
          "far LOD survives save + load");
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

void testModelCollisionFlags()
{
    // Copies share one Jolt mesh; rotation and scale come from the body.
    std::string err;
    engine::Scene s;
    engine::Model ramp;
    check(engine::loadModelFile(ramp, ENGINE_TEST_ASSETS "/models/ramp.gltf", &err), "load ramp: " + err);
    ramp.position = {0, 0, 0};
    s.models.push_back(ramp);
    ramp.position = {20, 0, 0};
    ramp.rotation = {0, 90, 0};
    ramp.scale = 2.0f;
    s.models.push_back(ramp);
    engine::PhysicsWorld world;
    world.setLevel(s);
    bool ok = true;
    for (const engine::Model& m : s.models) {
        const engine::Aabb b = m.bounds();
        const glm::vec3 c = (b.min + b.max) * 0.5f;
        auto hit = world.raycast({c.x, b.max.y + 5.0f, c.z}, {0, -1, 0}, 50.0f);
        const float y = b.max.y + 5.0f - (hit ? hit->distance : 0.0f);
        ok = ok && hit && y >= b.min.y - 1e-3f && y <= b.max.y + 1e-3f;
    }
    check(ok, "rotated and scaled model copies collide where they are drawn");
    s.models[1].collision = false;
    s.models[1].cullDistance = 120.0f;
    s.models[1].castShadows = false;
    world.setLevel(s);
    const engine::Aabb b = s.models[1].bounds();
    check(!world.raycast({(b.min.x + b.max.x) * 0.5f, b.max.y + 5.0f, (b.min.z + b.max.z) * 0.5f}, {0, -1, 0}, 50.0f),
          "collision 0: rays pass through");
    check(engine::saveScene(s, "flags.level", &err), "save flags level: " + err);
    engine::Scene again;
    check(engine::loadScene(again, "flags.level", &err) && again.models.size() == 2 && !again.models[1].collision &&
              again.models[1].cullDistance == 120.0f && again.models[0].collision && again.models[0].cullDistance == 0.0f &&
              !again.models[1].castShadows && again.models[0].castShadows,
          "collision and cull_distance round trip: " + err);
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

void testScriptSaveRestore()
{
    const char* path = "saver.lua";
    std::ofstream(path) << "local count = 0\n"
                           "function on_update(self, dt) count = count + 1 end\n"
                           "function on_save(self) return { n = count, s = 'a\\\"b\\n', v = vec3(1, 2, 3), t = { 1, 2 }, f = print } end\n"
                           "function on_load(self, d) count = d.n; self.health = d.n + d.v.y + #d.t; got = d.s end\n";
    auto s = flatFloor();
    engine::Entity e;
    e.name = "Saver";
    e.id = 0x1234;
    e.scriptPath = std::filesystem::absolute(path);
    s.entities.push_back(e);
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController player;
    player.spawn(world, {0, 0, 0});
    std::vector<engine::ScriptSystem::EntityState> saved;
    {
        auto copy = s;
        engine::ScriptSystem scripts(copy, world, player, [](const std::string&) {});
        scripts.start();
        for (int i = 0; i < 5; ++i)
            scripts.fixedUpdate(1.0f / 60.0f);
        copy.entities[0].position = {3, 0, 4};
        saved = scripts.saveEntities();
    }
    check(saved.size() == 1 && saved[0].id == 0x1234 && saved[0].data.find("vec3(") != std::string::npos &&
              saved[0].data.find("function") == std::string::npos,
          "on_save table becomes a Lua literal (functions left out)");
    auto fresh = s;
    engine::ScriptSystem scripts(fresh, world, player, [](const std::string&) {});
    scripts.start();
    scripts.restoreEntities(saved);
    check(std::abs(fresh.entities[0].health - 9.0f) < 1e-4f, "on_load gets the saved table back");
    check(glm::distance(fresh.entities[0].position, glm::vec3(3, 0, 4)) < 1e-4f, "restored entity position");
}

void testEnemyChasesAndAttacks()
{
    auto s = flatFloor();
    s.playerStart = {0, 0, 0};
    s.entities.push_back(enemyAt({8, 0, 0}, 180.0f)); // facing the player, 8 m away
    s.entities[0].properties["accuracy"] = "10"; // every shot hits: no random outcome in the test
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

void testWeaponFireRateAndReload()
{
    engine::Weapon w;
    int shots = 0;
    for (int i = 0; i < 60; ++i) // hold the trigger for 1 s
        shots += w.update(1.0f / 60.0f, true, false);
    check(shots >= 9 && shots <= 10, "about 9 shots per second at 0.11 s interval");
    check(w.ammo() == w.settings.magazineSize - shots, "each shot uses one round");
    w.update(1.0f / 60.0f, false, true);
    check(w.reloading(), "R starts a reload");
    for (int i = 0; i < 100; ++i)
        w.update(1.0f / 60.0f, false, false);
    check(!w.reloading() && w.ammo() == w.settings.magazineSize, "reload refills the magazine");
}

void testShotHitsEnemyNotWallBehind()
{
    auto s = flatFloor();
    s.boxes.push_back({"Wall", {0, 2, -10}, {10, 4, 1}, {1, 1, 1}});
    engine::Entity e;
    e.position = {0, 0, -5};
    s.entities.push_back(e);
    engine::PhysicsWorld world;
    world.setLevel(s);
    auto hit = engine::traceShot(s, world, {0, 1.6f, 0}, {0, 0, -1}, 100.0f);
    check(hit.hit && hit.entity == 0, "shot hits the entity in front of the wall");
    s.entities[0].alive = false;
    hit = engine::traceShot(s, world, {0, 1.6f, 0}, {0, 0, -1}, 100.0f);
    check(hit.hit && hit.entity == -1 && std::abs(hit.distance - 9.5f) < 0.01f, "dead entities are ignored");
}

void testEnemyDiesAndReactsToDamage()
{
    auto s = flatFloor();
    s.playerStart = {0, 0, 0};
    s.entities.push_back(enemyAt({15, 0, 0}, 0.0f)); // facing away, out of hearing range
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController player;
    player.spawn(world, s.playerStart);
    std::vector<std::string> log;
    engine::ScriptSystem scripts(s, world, player, [&](const std::string& m) { log.push_back(m); });
    scripts.start();
    scripts.fixedUpdate(1.0f / 60.0f);
    scripts.damageEntity(0, 20.0f);
    bool alerted = false;
    for (const auto& l : log)
        alerted = alerted || l.find("-> chase") != std::string::npos;
    check(alerted, "a shot from behind makes the enemy chase");
    check(s.entities[0].health == 80.0f, "damage lowers health");
    for (int i = 0; i < 4; ++i)
        scripts.damageEntity(0, 20.0f);
    check(!s.entities[0].alive, "five hits kill the enemy");
    bool died = false;
    for (const auto& l : log)
        died = died || l.find("destroyed") != std::string::npos;
    check(died, "on_death runs");
    const glm::vec3 before = s.entities[0].position;
    for (int i = 0; i < 30; ++i)
        scripts.fixedUpdate(1.0f / 60.0f);
    check(s.entities[0].position == before, "dead enemies stop moving");
}

void testTexturedWeaponModel()
{
    engine::Model m;
    std::string err;
    check(engine::loadModelFile(m, ENGINE_TEST_ASSETS "/models/weapons/fps/fp_ak74.glb", &err), "load weapon: " + err);
    bool textured = false;
    for (const auto& p : m.parts)
        textured = textured || (p.texture && p.texture->width >= 256 && p.texcoords.size() == p.positions.size());
    check(textured, "weapon has its color texture and UVs");
}

void testWeaponFilesAndLoadout()
{
    engine::Scene level;
    std::string err;
    check(engine::loadScene(level, ENGINE_TEST_ASSETS "/levels/test.level", &err), "load level: " + err);
    check(level.loadout.size() == 4, "test level gives the player AK-74M, pistol, knife and SMG");
    engine::WeaponSettings ak;
    check(engine::loadWeaponFile(ak, level.loadout.at(0), &err), "load ak74.weapon: " + err);
    check(ak.name == "AK-74M" && ak.animReloadEmpty == "reload_empty" && ak.reloadEmptyTime > ak.reloadTime && ak.animWalk == "walk",
          "AK-74M is the main weapon, with an empty-magazine reload and a walk clip");
    engine::WeaponSettings sidearm;
    check(engine::loadWeaponFile(sidearm, level.loadout.at(1), &err), "load pistol.weapon: " + err);
    check(sidearm.magazineSize == 8 && !sidearm.automatic, "Makarov PM is semi-automatic with 8 rounds");
    check(std::filesystem::exists(sidearm.modelPath), "weapon model path resolves next to the .weapon file");

    sidearm.damage = 11.5f;
    check(engine::saveWeaponFile(sidearm, "copy.weapon", &err), "save weapon: " + err);
    engine::WeaponSettings copy;
    check(engine::loadWeaponFile(copy, "copy.weapon", &err) && copy.damage == 11.5f, "weapon file round trip");
    check(std::filesystem::equivalent(copy.modelPath, sidearm.modelPath), "model path rewritten relative to the new file");
}

void testKnifeAndGuns()
{
    std::string err;
    engine::WeaponSettings knife, pistol, smg;
    check(engine::loadWeaponFile(knife, ENGINE_TEST_ASSETS "/weapons/knife.weapon", &err), "load knife: " + err);
    check(engine::loadWeaponFile(pistol, ENGINE_TEST_ASSETS "/weapons/pistol.weapon", &err), "load pistol: " + err);
    check(engine::loadWeaponFile(smg, ENGINE_TEST_ASSETS "/weapons/smg.weapon", &err), "load smg: " + err);
    check(knife.melee() && !pistol.melee() && !smg.melee(), "knife is melee, guns are firearms");
    check(!pistol.automatic && smg.automatic, "pistol is semi-automatic, SMG automatic");
    check(std::filesystem::exists(knife.modelPath), "knife model exists");

    // Melee: swings without ammo or reloads.
    engine::Weapon w(knife);
    int swings = 0;
    for (int i = 0; i < 120; ++i) // 2 s holding the trigger, swing time 0.6 s
        swings += w.update(1.0f / 60.0f, true, i == 30);
    check(swings == 4, "knife swings every 0.6 s while held (" + std::to_string(swings) + ")");
    check(!w.reloading(), "knife never reloads");
    w.aim(1.0f, true);
    check(w.aimAmount() == 0.0f, "knife cannot aim");

    // Aiming down the sights: raised over aim_time, tighter spread, dropped while reloading.
    engine::Weapon gun(pistol);
    check(gun.canAim() && pistol.aimFov > 0.0f && pistol.aimSpread < 1.0f, "pistol can aim");
    gun.aim(pistol.aimTime * 0.5f, true);
    check(gun.aimAmount() > 0.4f && gun.aimAmount() < 0.6f, "aim rises over aim_time");
    gun.aim(1.0f, true);
    check(gun.aimAmount() == 1.0f, "fully aimed");
    float hip = 0.0f, aimed = 0.0f;
    for (int i = 0; i < 400; ++i)
        aimed = std::max(aimed, glm::degrees(std::acos(glm::clamp(gun.spreadDirection({0, 0, -1}).z * -1.0f, -1.0f, 1.0f))));
    gun.aim(1.0f, false);
    for (int i = 0; i < 400; ++i)
        hip = std::max(hip, glm::degrees(std::acos(glm::clamp(gun.spreadDirection({0, 0, -1}).z * -1.0f, -1.0f, 1.0f))));
    check(aimed < hip * 0.6f && hip <= pistol.spreadDegrees + 0.01f, "aiming tightens the spread");
    gun.aim(1.0f, true);
    gun.update(1.0f / 60.0f, true, false); // one shot, then reload
    gun.update(1.0f / 60.0f, false, true);
    gun.aim(1.0f, true);
    check(gun.reloading() && gun.aimAmount() == 0.0f, "no aiming while reloading");
    w.update(1.0f / 60.0f, false, false);
    check(w.swing() > 0.0f && w.swing() < 1.0f, "swing progress during a swing");

    // Melee reach and arc: hits a guard slightly to the side, not one out of reach.
    auto s = flatFloor();
    engine::Entity e;
    e.position = {0.5f, 0, -1.2f};
    s.entities.push_back(e);
    engine::PhysicsWorld world;
    world.setLevel(s);
    auto hit = engine::traceMelee(s, world, {0, 1.2f, 0}, {0, 0, -1}, knife.range, knife.spreadDegrees);
    check(hit.hit && hit.entity == 0, "knife arc catches a guard off to the side");
    s.entities[0].position = {0, 0, -4};
    hit = engine::traceMelee(s, world, {0, 1.2f, 0}, {0, 0, -1}, knife.range, knife.spreadDegrees);
    check(hit.entity < 0, "knife does not reach 4 m");

    // New keys round-trip.
    knife.viewRotation = {1, 2, 3};
    knife.animReload = "Reload";
    check(engine::saveWeaponFile(knife, "copy.weapon", &err), "save knife: " + err);
    engine::WeaponSettings copy;
    check(engine::loadWeaponFile(copy, "copy.weapon", &err) && copy.melee() && copy.viewRotation == glm::vec3(1, 2, 3) &&
              copy.animReload == "Reload",
          "melee type, view rotation and clips round-trip");
}

void testHierarchyAndDependencies()
{
    std::string err;
    engine::Scene s;
    check(engine::loadScene(s, ENGINE_TEST_ASSETS "/levels/test.level", &err), "load level: " + err);
    check(s.groups.size() == 10 && engine::nodeParent(s, "Crate B") == "Crate A", "test level has a hierarchy");
    // Start from a flat scene.
    s.groups.clear();
    for (int i = 0; i < s.objectCount(); ++i)
        engine::reparentChildren(s, engine::objectParent(s, i), "");
    s.groups.push_back({"Arena", ""});
    s.groups.push_back({"Enemies", "Arena"});
    check(engine::setNodeParent(s, "Guard A", "Enemies"), "guard goes into the Enemies group");
    check(engine::setNodeParent(s, "Crate B", "Crate A"), "crate B becomes a child of crate A");
    check(!engine::setNodeParent(s, "Arena", "Enemies"), "a group cannot move into its own child");
    check(!engine::setNodeParent(s, "Crate A", "Nope"), "unknown parents are refused");
    const auto under = engine::descendantObjects(s, "Arena");
    check(under.size() == 1 && engine::objectName(s, under[0]) == "Guard A", "descendants of a group");

    // Moving a parent moves the subtree (as the editor does).
    const int crateA = engine::findObject(s, "Crate A");
    const int crateB = engine::findObject(s, "Crate B");
    const glm::vec3 before = engine::objectPosition(s, crateB);
    for (int child : engine::descendantObjects(s, "Crate A"))
        engine::translateObject(s, child, {1, 0, 0});
    engine::translateObject(s, crateA, {1, 0, 0});
    check(engine::objectPosition(s, crateB) == before + glm::vec3(1, 0, 0), "child follows its parent");

    check(engine::renameNode(s, "Crate A", "Big crate") && engine::nodeParent(s, "Crate B") == "Big crate",
          "renaming keeps children attached");
    check(!engine::renameNode(s, "Big crate", "Floor"), "names stay unique");
    check(engine::uniqueNodeName(s, "Floor") == "Floor 2", "unique name for duplicates");

    check(engine::saveScene(s, "hierarchy.level", &err), "save: " + err);
    engine::Scene copy;
    check(engine::loadScene(copy, "hierarchy.level", &err), "reload: " + err);
    check(copy.groups.size() == 2 && engine::nodeParent(copy, "Guard A") == "Enemies" &&
              engine::nodeParent(copy, "Crate B") == "Big crate" && engine::nodeParent(copy, "Enemies") == "Arena",
          "hierarchy round-trips through the level file");

    engine::removeGroup(copy, "Enemies");
    check(engine::nodeParent(copy, "Guard A") == "Arena", "removing a group moves its children up");

    // Dependencies and where-used.
    const int floor = engine::findObject(copy, "Floor");
    const auto deps = engine::objectDependencies(copy, floor);
    bool material = false, texture = false;
    for (const auto& d : deps) {
        material |= d.kind == engine::AssetKind::Material && d.path.filename() == "concrete.material";
        texture |= d.kind == engine::AssetKind::Texture && d.depth == 1;
    }
    check(material && texture, "floor uses the concrete material and its textures");
    const int guard = engine::findObject(copy, "Guard A");
    bool script = false, character = false;
    for (const auto& d : engine::objectDependencies(copy, guard)) {
        script |= d.kind == engine::AssetKind::Script;
        character |= d.kind == engine::AssetKind::Character;
    }
    check(script && character, "guard uses a script and a character model");
    const auto users = engine::objectsUsing(copy, ENGINE_TEST_ASSETS "/scripts/enemy.lua");
    check(users.size() == 2, "both guards use enemy.lua (" + std::to_string(users.size()) + ")");
}

void testJobSystem()
{
    engine::JobSystem jobs(3);
    auto f = jobs.submit([] { return 6 * 7; });
    check(f.get() == 42, "job result comes back through the future");
    std::vector<int> data(10000, 1);
    std::atomic<long> sum{0};
    jobs.parallelFor(data.size(), 128, [&](std::size_t b, std::size_t e) {
        long local = 0;
        for (std::size_t i = b; i < e; ++i)
            local += data[i];
        sum += local;
    });
    check(sum == 10000, "parallelFor covers every element once");
    engine::MainThreadQueue main;
    std::atomic<int> posted{0};
    std::vector<std::future<void>> waits;
    for (int i = 0; i < 8; ++i)
        waits.push_back(jobs.submit([&] { main.post([&] { ++posted; }); }));
    for (auto& w : waits)
        w.get();
    check(main.drain(100.0) == 8 && posted == 8, "results posted to the main thread run there");

    // Loading assets in parallel from several threads (editor background loaders).
    std::vector<std::future<bool>> loads;
    for (int i = 0; i < 6; ++i)
        loads.push_back(jobs.submit([] {
            return engine::loadMaterial(ENGINE_TEST_ASSETS "/materials/bricks.material") != nullptr;
        }));
    bool all = true;
    for (auto& l : loads)
        all = all && l.get();
    check(all, "materials load from several threads at once");
}

void testSemiAutomatic()
{
    engine::WeaponSettings s;
    s.automatic = false;
    s.fireInterval = 0.1f;
    engine::Weapon w(s);
    int held = 0;
    for (int i = 0; i < 60; ++i)
        held += w.update(1.0f / 60.0f, true, false);
    check(held == 1, "holding a semi-auto trigger fires once");
    int tapped = 0;
    for (int i = 0; i < 60; ++i)
        tapped += w.update(1.0f / 60.0f, (i / 10) % 2 == 0, false); // pull/release every 10 steps
    check(tapped == 2, "each new pull fires once (the first block continues the earlier hold)");
}

void testScriptPropertiesFromEditor()
{
    std::vector<engine::ScriptProperty> props;
    std::string err;
    check(engine::readScriptProperties(ENGINE_TEST_ASSETS "/scripts/enemy.lua", props, &err), "read enemy.lua properties: " + err);
    check(props.size() == 27 && props[0].name == "health" && props[0].defaultValue == "100", "enemy.lua declares 27 tunables, health first");

    // An editor override changes the behaviour: 40 hp dies after two 20-damage hits.
    auto s = flatFloor();
    s.entities.push_back(enemyAt({15, 0, 0}, 0.0f));
    s.entities[0].properties["health"] = "40";
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController player;
    player.spawn(world, {0, 0, 0});
    engine::ScriptSystem scripts(s, world, player, [](const std::string&) {});
    scripts.start();
    scripts.damageEntity(0, 20.0f);
    check(s.entities[0].alive, "alive after the first hit");
    scripts.damageEntity(0, 20.0f);
    check(!s.entities[0].alive, "editor value health=40 is used by the script");

    // Properties and player settings survive save + load.
    s.player.jumpSpeed = 7.25f;
    check(engine::saveScene(s, "props.level", &err), "save: " + err);
    engine::Scene t;
    check(engine::loadScene(t, "props.level", &err), "load: " + err);
    check(t.entities.size() == 1 && t.entities[0].properties["health"] == "40", "entity properties are saved");
    check(t.player.jumpSpeed == 7.25f, "player settings are saved");
}

void testPick()
{
    auto s = flatFloor();
    s.boxes.push_back({"Target", {0, 1, -10}, {1, 1, 1}, {1, 1, 1}});
    check(engine::pickObject(s, {0, 1, 0}, {0, 0, -1}) == 1, "ray hits the box in front");
    check(engine::pickObject(s, {0, 5, 0}, {0, 1, 0}) == -1, "ray into the sky hits nothing");
}
void testAnimatedCharacter()
{
    std::string err;
    auto model = engine::loadCharacterModel(ENGINE_TEST_ASSETS "/models/characters/library/ual_mannequin.glb", &err);
    check(model && model->skeleton, "load animated mannequin: " + err);
    if (!model)
        return;
    const engine::Skeleton& sk = *model->skeleton;
    check(sk.clips.size() == 46 && sk.findClip("Walk_Loop") >= 0 && sk.findClip("Death01") >= 0, "mannequin has its 46 clips");
    check(!sk.skins.empty(), "mannequin is skinned");
    check(model->localBounds.max.y - model->localBounds.min.y > 1.0f, "rest-pose bounds");
    check(engine::loadCharacterModel(ENGINE_TEST_ASSETS "/models/characters/library/ual_mannequin.glb") == model, "entities share one loaded model");

    engine::Animator a;
    std::vector<glm::mat4> p0, p1;
    check(a.play(sk, "Walk_Loop"), "play Walk_Loop");
    engine::evaluatePose(sk, &a, p0);
    a.update(sk, 0.3f);
    engine::evaluatePose(sk, &a, p1);
    float diff = 0.0f;
    for (std::size_t i = 0; i < p0.size(); ++i)
        diff += glm::length(glm::vec3(p0[i][3]) - glm::vec3(p1[i][3]));
    check(diff > 0.01f, "walking moves the bones");
    check(!a.play(sk, "NoSuchClip") && a.clipName(sk) == "Walk_Loop", "unknown clip is ignored");

    a.play(sk, "Death01", false, 1.0f, 0.2f);
    check(a.fadeDuration > 0.0f && a.previous.clip >= 0, "switching clips crossfades");
    a.update(sk, 0.5f);
    check(a.fadeDuration == 0.0f && !a.finished(sk), "fade ends, death still playing");
    a.update(sk, 2.0f);
    check(a.finished(sk), "non-looping clip finishes and holds");

    engine::Entity e;
    e.model = model;
    e.position = {2, 0, 3};
    e.size = {0.6f, 1.8f, 0.6f};
    const glm::mat4 m = e.modelTransform();
    const glm::vec3 feet(m * glm::vec4(0.5f * (model->localBounds.min.x + model->localBounds.max.x), model->localBounds.min.y,
                                       0.5f * (model->localBounds.min.z + model->localBounds.max.z), 1.0f));
    const glm::vec3 top(m * glm::vec4(0.5f * (model->localBounds.min.x + model->localBounds.max.x), model->localBounds.max.y,
                                      0.5f * (model->localBounds.min.z + model->localBounds.max.z), 1.0f));
    check(glm::length(feet - e.position) < 1e-3f && std::abs(top.y - 1.8f) < 1e-3f, "model stands on the entity and fits its height");
}

void testEnemyAnimations()
{
    auto s = flatFloor();
    s.playerStart = {0, 0, 0};
    engine::Entity e = enemyAt({26, 0, 0}, 180.0f); // beyond shoot_range: runs closer first
    e.modelPath = ENGINE_TEST_ASSETS "/models/characters/library/ual_mannequin.glb";
    e.model = engine::loadCharacterModel(e.modelPath);
    e.animation = "Idle_Loop";
    s.entities.push_back(e);
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController player;
    player.spawn(world, s.playerStart);
    engine::ScriptSystem scripts(s, world, player, [](const std::string&) {});
    scripts.start();
    const engine::Skeleton& sk = *s.entities[0].model->skeleton;
    bool ran = false, punched = false;
    for (int i = 0; i < 240; ++i) {
        scripts.fixedUpdate(1.0f / 60.0f);
        engine::updateAnimations(s, 1.0f / 60.0f);
        const std::string clip = s.entities[0].animator.clipName(sk);
        ran = ran || clip == "Jog_Fwd_Loop";
        punched = punched || clip == "Pistol_Shoot";
    }
    check(ran, "chasing enemy plays Jog_Fwd_Loop");
    check(punched, "attacking enemy plays Pistol_Shoot");
    for (int i = 0; i < 10; ++i)
        scripts.damageEntity(0, 30.0f);
    check(!s.entities[0].alive && s.entities[0].animator.clipName(sk) == "Death01", "killed enemy plays Death01");
    check(s.entities[0].flash > 0.0f, "hits flash the model");
    for (int i = 0; i < 180; ++i)
        engine::updateAnimations(s, 1.0f / 60.0f);
    check(s.entities[0].animator.finished(sk), "death animation holds its last frame");

    // Round trip of the character settings through a level file.
    s.entities[0].modelScale = 1.25f;
    s.entities[0].modelYaw = 15.0f;
    s.entities[0].animation = "Wave";
    std::string err;
    check(engine::saveScene(s, "character.level", &err), "save level with character: " + err);
    engine::Scene loaded;
    check(engine::loadScene(loaded, "character.level", &err), "load level with character: " + err);
    check(loaded.entities.size() == 1 && loaded.entities[0].model && loaded.entities[0].modelScale == 1.25f
              && loaded.entities[0].modelYaw == 15.0f && loaded.entities[0].animation == "Wave",
          "character model, scale, yaw and clip are saved");
}

void testScriptSyntaxCheck()
{
    const engine::ScriptCheck ok = engine::checkScriptSyntax("function on_update(self, dt)\n  self:move(vec3(1,0,0), 2)\nend\n");
    check(ok.ok, "valid script passes the syntax check");
    const engine::ScriptCheck bad = engine::checkScriptSyntax("local x = 1\nif x then\n  print(x)\n", "bad.lua");
    check(!bad.ok && bad.line == 4 && bad.message.find("'end' expected") != std::string::npos,
          "missing end is reported with its line: " + std::to_string(bad.line) + " " + bad.message);
    const engine::ScriptCheck bad2 = engine::checkScriptSyntax("x = = 2", "eq.lua");
    check(!bad2.ok && bad2.line == 1, "error on line 1");
}

void testMaterialsAndTextures()
{
    std::string err;
    auto m = engine::loadMaterial(ENGINE_TEST_ASSETS "/materials/bricks.material", &err);
    check(m && m->map(engine::MapKind::Albedo) && m->map(engine::MapKind::Albedo)->width == 512, "load bricks material with texture: " + err);
    if (!m)
        return;
    check(engine::loadMaterial(ENGINE_TEST_ASSETS "/materials/bricks.material") == m, "materials are shared");
    check(std::abs(m->tiling.x - 0.5f) < 1e-6f, "tiling read");

    // Texture adjustments rebuild the pixels and give the GPU a new id.
    engine::TextureAsset& tex = *m->map(engine::MapKind::Albedo);
    engine::applyTextureSettings(tex); // the asset may come from the cook cache without pixels
    check(!tex.source.empty() && tex.texture->format == engine::TextureFormat::BC1, "bricks decoded and compressed to BC1");
    const std::uint64_t before = tex.texture->id;
    const std::uint8_t r0 = tex.texture->rgba[0];
    tex.settings.brightness = 0.3f;
    engine::applyTextureSettings(tex);
    check(tex.texture->id != before && tex.texture->rgba[0] > r0, "brightness brightens and re-uploads");
    tex.settings = {};
    engine::applyTextureSettings(tex);
    check(tex.texture->rgba[0] == r0, "default settings restore the original pixels");

    // Material file round trip (texture path stays relative to the material).
    engine::Material copy = *m;
    copy.name = "Copy";
    copy.roughness = 0.35f;
    copy.metallic = 1.0f;
    copy.emissive = {0.1f, 0.2f, 0.3f};
    copy.alphaMode = engine::AlphaMode::Mask;
    copy.alphaCutoff = 0.4f;
    copy.doubleSided = true;
    check(engine::saveMaterial(copy, "copy.material", &err), "save material: " + err);
    auto loaded = engine::loadMaterial("copy.material", &err);
    check(loaded && loaded->alphaMode == engine::AlphaMode::Mask && std::abs(loaded->alphaCutoff - 0.4f) < 1e-6f &&
              loaded->doubleSided,
          "material alpha mask and double-sided round trip");
    check(loaded && loaded->name == "Copy" && std::abs(loaded->roughness - 0.35f) < 1e-6f && loaded->metallic == 1.0f
              && loaded->map(engine::MapKind::Albedo) == m->map(engine::MapKind::Albedo),
          "material round trip shares the texture: " + err);

    // Level with box materials and a model slot override.
    engine::Scene s;
    check(engine::loadScene(s, ENGINE_TEST_ASSETS "/levels/test.level", &err), "load test level: " + err);
    check(!s.boxes.empty() && s.boxes[0].material && s.boxes[0].material->name == "Concrete", "floor uses the concrete material");
    engine::Model& ramp = s.models[0];
    const auto slots = ramp.materialSlots();
    if (!slots.empty()) {
        ramp.materialOverrides[slots[0]] = m->sourcePath;
        ramp.overrideMaterials[slots[0]] = m;
    }
    check(engine::saveScene(s, "materials.level", &err), "save level with materials: " + err);
    engine::Scene again;
    check(engine::loadScene(again, "materials.level", &err), "reload level with materials: " + err);
    check(again.boxes[0].material == s.boxes[0].material, "box material survives the round trip");
    check(slots.empty() || again.models[0].materialFor(again.models[0].parts[0]) == m.get(), "model slot override survives");
}

} // namespace

void testRotatedBox()
{
    engine::Scene s;
    engine::Box plank("Plank", {0, 1, 0}, {4, 0.2f, 0.4f}, {1, 1, 1});
    plank.rotation = {0, 90, 0}; // now runs along Z
    s.boxes.push_back(plank);
    const engine::Aabb b = s.boxes[0].bounds();
    check(std::abs(b.max.z - 2.0f) < 1e-4f && std::abs(b.max.x - 0.2f) < 1e-4f, "rotated box bounds follow the rotation");
    check(engine::pickObject(s, {0, 5, 1.8f}, {0, -1, 0}) == 0, "ray picks the rotated box along its new axis");
    check(engine::pickObject(s, {1.8f, 5, 0}, {0, -1, 0}) == -1, "and misses where it used to be");
    engine::PhysicsWorld world;
    world.setLevel(s);
    check(world.raycast({0, 5, 1.8f}, {0, -1, 0}, 10.0f).has_value(), "physics uses the rotated box");
    check(!world.raycast({1.8f, 5, 0}, {0, -1, 0}, 10.0f).has_value(), "physics box is not axis-aligned any more");
    std::string err;
    check(engine::saveScene(s, "rotated.level", &err), "save rotated box: " + err);
    engine::Scene loaded;
    check(engine::loadScene(loaded, "rotated.level", &err) && loaded.boxes.size() == 1 && loaded.boxes[0].rotation.y == 90.0f,
          "box rotation is saved: " + err);

    // Texture placement from the editor's Modify Texture tools.
    s.boxes[0].uvScale = {2.0f, 0.5f};
    s.boxes[0].uvOffset = {0.25f, -0.5f};
    s.boxes[0].uvRotation = 90.0f;
    check(engine::saveScene(s, "rotated.level", &err), "save box uv: " + err);
    engine::Scene uv;
    check(engine::loadScene(uv, "rotated.level", &err) && uv.boxes[0].uvScale == glm::vec2(2.0f, 0.5f) &&
              uv.boxes[0].uvOffset == glm::vec2(0.25f, -0.5f) && uv.boxes[0].uvRotation == 90.0f,
          "box texture scale, shift and rotation are saved: " + err);
}

void testRotateGizmo()
{
    const glm::vec3 c(2, 1, -3);
    const float r = 1.5f;
    // Ring around Y seen from above: a ray straight down onto the ring hits, the center misses.
    auto hit = engine::pickRing(c + glm::vec3(r, 10, 0), {0, -1, 0}, c, engine::gizmoAxis(1), r, 0.1f);
    check(hit && std::abs(*hit - 10.0f) < 0.05f, "ray onto the Y ring hits it");
    check(!engine::pickRing(c + glm::vec3(0, 10, 0), {0, -1, 0}, c, engine::gizmoAxis(1), r, 0.1f), "center of the ring is not the ring");
    // Seen exactly edge-on (from the side) the ring is still grabbable.
    check(engine::pickRing(c + glm::vec3(0, 0.05f, 10), {0, 0, -1}, c, engine::gizmoAxis(1), r, 0.1f).has_value(),
          "edge-on ring can be grabbed");
    check(!engine::pickRing(c + glm::vec3(0, 0.5f, 10), {0, 0, -1}, c, engine::gizmoAxis(1), r, 0.1f), "above the edge-on ring misses");
    // Angle around Z seen from the front: quarter turn from +X to +Y is +90 degrees (right-handed).
    const auto a0 = engine::ringAngle(c + glm::vec3(r, 0, 10), {0, 0, -1}, c, engine::gizmoAxis(2));
    const auto a1 = engine::ringAngle(c + glm::vec3(0, r, 10), {0, 0, -1}, c, engine::gizmoAxis(2));
    check(a0 && a1 && std::abs(glm::degrees(engine::wrapAngle(*a1 - *a0)) - 90.0f) < 0.01f, "ring angle follows the cursor");
    check(!engine::ringAngle(c + glm::vec3(0, 0, 10), {0, 0, -1}, c, engine::gizmoAxis(1)), "edge-on plane gives no angle");
    check(std::abs(engine::wrapAngle(glm::radians(350.0f)) - glm::radians(-10.0f)) < 1e-4f, "angles wrap across 180 degrees");
    for (int axis = 0; axis < 3; ++axis)
        for (const glm::vec3& p : engine::ringPoints(c, engine::gizmoAxis(axis), r, 16))
            check(std::abs(glm::length(p - c) - r) < 1e-4f && std::abs((p - c)[axis]) < 1e-4f, "ring points lie on the circle");
}

void testTextureCooking()
{
    // 64x32 opaque gradient -> BC1, 7 mips (64x32 .. 1x1), 8 bytes per 4x4 block.
    std::vector<std::uint8_t> px(64 * 32 * 4);
    for (int i = 0; i < 64 * 32; ++i) {
        px[i * 4] = static_cast<std::uint8_t>(i % 64 * 4);
        px[i * 4 + 1] = static_cast<std::uint8_t>(i / 64 * 8);
        px[i * 4 + 2] = 128;
        px[i * 4 + 3] = 255;
    }
    engine::CookOptions o;
    engine::CookedTexture c = engine::cookTexture(px.data(), 64, 32, o);
    check(c.format == engine::TextureFormat::BC1 && c.mips.size() == 7 && c.mips[0].size == 16 * 8 * 8 && c.mips[6].size == 8,
          "cook: BC1 mip chain");
    px[3] = 10; // any alpha -> BC3
    check(engine::cookTexture(px.data(), 64, 32, o).format == engine::TextureFormat::BC3, "cook: alpha uses BC3");
    o.usage = engine::TextureUsage::Normal;
    check(engine::cookTexture(px.data(), 64, 32, o).format == engine::TextureFormat::BC5, "cook: normal maps use BC5");
    o.usage = engine::TextureUsage::Color;
    o.maxSize = 16;
    const engine::CookedTexture small = engine::cookTexture(px.data(), 64, 32, o);
    check(small.width == 16 && small.height == 8 && small.mips.size() == 5, "cook: max size drops the top mips");

    // Alpha-tested grass: a fifth of the pixels opaque, scattered. Plain averaging drops the
    // 8x8 mip below the 0.5 cutoff everywhere; coverage-preserving mips keep ~20 %.
    {
        std::vector<std::uint8_t> grass(64 * 64 * 4, 200);
        for (int i = 0; i < 64 * 64; ++i)
            grass[i * 4 + 3] = (static_cast<unsigned>(i) * 2654435761u >> 16) % 5 == 0 ? 255 : 0;
        engine::CookOptions g;
        g.compress = false;
        auto coverage8 = [&](const engine::CookedTexture& t) {
            const engine::TextureMip& m = t.mips[3]; // 8x8
            int above = 0;
            for (std::size_t i = 0; i < m.size / 4; ++i)
                above += t.data[m.offset + i * 4 + 3] > 127 ? 1 : 0;
            return static_cast<float>(above) / static_cast<float>(m.size / 4);
        };
        const float plain = coverage8(engine::cookTexture(grass.data(), 64, 64, g));
        g.alphaCoverage = 0.5f;
        const float kept = coverage8(engine::cookTexture(grass.data(), 64, 64, g));
        check(plain < 0.01f && kept > 0.1f && kept < 0.35f, "cook: coverage-preserving alpha mips");
    }

    // Cache file round trip, also reading only the small mips (streaming).
    check(engine::writeCookedTexture("cooked_test.ctex", c), "cook: write cache file");
    engine::CookedTexture all, tail;
    check(engine::readCookedTexture("cooked_test.ctex", all) && all.data == c.data && all.mips.size() == 7, "cook: read cache file");
    check(engine::readCookedTexture("cooked_test.ctex", tail, 2) && tail.width == 16 && tail.mips.size() == 5 &&
              tail.mips[0].offset == 0 &&
              std::equal(tail.data.begin(), tail.data.end(), c.data.begin() + static_cast<long>(c.mips[2].offset)),
          "cook: read from mip 2");
}

void testTerrain()
{
    auto t = std::make_shared<engine::Terrain>();
    t->create({-32.0f, -32.0f}, 64.0f, 65, 64);
    check(t->valid() && std::abs(t->heightAt(3.0f, 4.0f)) < 1e-6f && t->layerWeight(0, 0.0f, 0.0f) > 0.99f, "terrain: flat, layer 0");
    for (int i = 0; i < 4; ++i)
        t->sculpt({0, 0, 0}, 8.0f, 1.0f, engine::Terrain::Sculpt::Raise);
    check(t->heightAt(0.0f, 0.0f) > 1.9f && std::abs(t->heightAt(20.0f, 20.0f)) < 1e-6f, "terrain: raise brush");
    float hit = 0.0f;
    check(t->raycast({0, 10, 0}, {0, -1, 0}, 50.0f, hit) && std::abs(10.0f - hit - t->heightAt(0, 0)) < 0.01f, "terrain: raycast");
    for (int i = 0; i < 10; ++i)
        t->paintLayer({10, 0, 10}, 4.0f, 0.5f, 2);
    check(t->layerWeight(2, 10.0f, 10.0f) > 0.9f && t->layerWeight(0, -20.0f, -20.0f) > 0.99f, "terrain: paint layer 2");
    for (int i = 0; i < 10; ++i)
        t->paintLayer({-20, 0, 10}, 3.0f, 0.5f, 6);
    check(t->layerWeight(6, -20.0f, 10.0f) > 0.9f && t->layerWeight(2, 10.0f, 10.0f) > 0.9f, "terrain: paint layer 7 of 8");
    engine::FoliageType grass;
    grass.density = 2.0f;
    t->foliageTypes.push_back(grass);
    std::mt19937 rng(1);
    const int added = t->paintFoliage({-10, 0, -10}, 3.0f, 0, rng);
    check(added > 20 && t->paintFoliage({-10, 0, -10}, 3.0f, 0, rng) < added / 4, "terrain: foliage fills to its density");
    engine::Scene s;
    s.terrain = t;
    std::string err;
    check(engine::saveScene(s, "terrain_test.level", &err), "terrain: save " + err);
    engine::Scene again;
    check(engine::loadScene(again, "terrain_test.level", &err) && again.terrain &&
              std::abs(again.terrain->heightAt(0, 0) - t->heightAt(0, 0)) < 1e-5f &&
              again.terrain->foliage.size() == t->foliage.size() && again.terrain->foliageTypes.size() == 1 &&
              again.terrain->layerWeight(2, 10.0f, 10.0f) > 0.9f && again.terrain->layerWeight(6, -20.0f, 10.0f) > 0.9f,
          "terrain: level round trip " + err);
    // Brush edits are listed for partial uploads; resize keeps heights and paint where they were.
    {
        engine::Terrain g = *t;
        const std::uint64_t v0 = g.meshVersion;
        g.sculpt({5, 0, 5}, 3.0f, 1.0f, engine::Terrain::Sculpt::Raise);
        g.sculpt({6, 0, 5}, 3.0f, 1.0f, engine::Terrain::Sculpt::Smooth);
        check(g.meshEdits.size() >= 2 && g.meshEdits[g.meshEdits.size() - 2].before == v0 && g.meshEdits.back().after == g.meshVersion,
              "terrain: edit list for partial uploads");
        const float h0 = g.heightAt(0.0f, 0.0f), road = g.layerWeight(2, 10.0f, 10.0f);
        const float edge = g.heightAt(31.9f, 0.0f);
        check(g.resize(128.0f, {-1, -1}) && std::abs(g.size - 128.0f) < 1e-3f && g.resolution == 129 && g.splatResolution == 128 &&
                  g.origin == glm::vec2(-32.0f) && std::abs(g.heightAt(0.0f, 0.0f) - h0) < 1e-4f &&
                  std::abs(g.layerWeight(2, 10.0f, 10.0f) - road) < 0.02f && g.layerWeight(0, 80.0f, 80.0f) > 0.99f &&
                  std::abs(g.heightAt(60.0f, 0.0f) - edge) < 0.05f && g.meshEdits.empty(),
              "terrain: grow toward +X +Z");
        check(g.resize(64.0f, {1, 1}) && g.origin == glm::vec2(32.0f) && g.layerWeight(0, 80.0f, 80.0f) > 0.99f,
              "terrain: crop keeping the +X +Z corner");
        check(!g.resize(100000.0f, {0, 0}), "terrain: resize limit");
    }
    check(t->eraseFoliage({-10, 0, -10}, 5.0f) == static_cast<int>(t->foliage.size() + 0) || t->foliage.empty(), "terrain: erase foliage");

    // Physics: the height field collides where it is drawn; trees with collision block rays.
    {
        engine::PhysicsWorld world;
        world.setLevel(s);
        bool ok = true;
        for (glm::vec2 p : {glm::vec2(0.0f), glm::vec2(3.5f, -2.25f), glm::vec2(20.0f, 20.0f)}) {
            auto h = world.raycast({p.x, 30.0f, p.y}, {0, -1, 0}, 60.0f);
            ok = ok && h && std::abs(h->point.y - t->heightAt(p.x, p.y)) < 0.05f;
        }
        check(ok, "terrain: height field collision");
        engine::Model ramp;
        check(engine::loadModelFile(ramp, ENGINE_TEST_ASSETS "/models/ramp.gltf", &err), "load ramp: " + err);
        engine::FoliageType tree;
        tree.model = std::make_shared<engine::Model>(ramp);
        tree.collision = true;
        auto withTree = std::make_shared<engine::Terrain>(*t);
        withTree->foliageTypes = {tree};
        withTree->foliage = {engine::FoliageInstance{{20.0f, 0.0f, 20.0f}, 0.0f, 1.0f, 0.0f, 0.0f, 0}};
        engine::Scene forest;
        forest.terrain = withTree;
        world.setLevel(forest);
        const engine::Aabb rb = ramp.localBounds;
        const glm::vec3 top = glm::vec3(20.0f, 0.0f, 20.0f) + glm::vec3((rb.min.x + rb.max.x) * 0.5f, 0.0f, rb.min.z + 1.0f);
        auto h = world.raycast({top.x, 30.0f, top.z}, {0, -1, 0}, 60.0f);
        check(h && h->point.y > 1.5f, "terrain: foliage with collision");
    }
    // Foliage LOD levels and thinning survive save + load.
    {
        auto lodded = std::make_shared<engine::Terrain>(*t);
        engine::FoliageType::Lod lod;
        lod.modelPath = std::filesystem::absolute(ENGINE_TEST_ASSETS "/models/ramp.gltf").lexically_normal();
        lod.distance = 12.5f;
        lodded->foliageTypes[0].modelPath = lod.modelPath;
        lodded->foliageTypes[0].lods = {lod};
        lodded->foliageTypes[0].farDensity = 0.4f;
        lodded->foliageTypes[0].fade = 0.25f;
        lodded->foliageTypes[0].layerDensity = {1.0f, 0.5f, 0.0f, 0.75f, 1.0f, 1.0f, 0.25f, 1.0f};
        engine::Scene ls;
        ls.terrain = lodded;
        check(engine::saveScene(ls, "terrain_lod.level", &err), "terrain lod: save " + err);
        engine::Scene back;
        check(engine::loadScene(back, "terrain_lod.level", &err) && back.terrain && back.terrain->foliageTypes.size() == 1 &&
                  back.terrain->foliageTypes[0].lods.size() == 1 && back.terrain->foliageTypes[0].lods[0].model &&
                  back.terrain->foliageTypes[0].lods[0].distance == 12.5f &&
                  std::abs(back.terrain->foliageTypes[0].farDensity - 0.4f) < 1e-4f && back.terrain->foliageTypes[0].fade == 0.25f &&
                  back.terrain->foliageTypes[0].layerDensity[1] == 0.5f && back.terrain->foliageTypes[0].layerDensity[2] == 0.0f &&
                  back.terrain->foliageTypes[0].layerDensity[3] == 0.75f,
              "terrain: foliage LOD, far density, fade and layer density round trip " + err);
    }
    // Density by layers: none on layer 2 (painted around 10,10), full on layer 0.
    {
        auto layered = std::make_shared<engine::Terrain>(*t);
        layered->foliage.clear();
        layered->foliageTypes[0].layerDensity = {1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        std::mt19937 r2(7);
        const int onGrass = layered->paintFoliage({-20, 0, 20}, 3.0f, 0, r2);
        const int onRoad = layered->paintFoliage({10, 0, 10}, 2.0f, 0, r2);
        check(onGrass > 20 && onRoad == 0, "terrain: brush density by layer");
        const auto scattered = layered->scatterFoliage(0, 3u);
        int nearRoad = 0;
        for (const engine::FoliageInstance& f : scattered)
            nearRoad += std::hypot(f.position.x - 10.0f, f.position.z - 10.0f) < 1.5f;
        const float expected = 2.0f * 64.0f * 64.0f; // density x area, minus the layer-2 spot
        check(nearRoad == 0 && scattered.size() > expected * 0.85f && scattered.size() < expected * 1.05f,
              "terrain: fill by layers (" + std::to_string(scattered.size()) + ")");
        check(layered->scatterFoliage(0, 3u).size() == scattered.size(), "terrain: fill is deterministic");
    }
}

void testSharedMeshes()
{
    // Copies of a model share their vertices; editing a copy detaches it.
    std::string err;
    engine::Model a;
    check(engine::loadModelFile(a, ENGINE_TEST_ASSETS "/models/ramp.gltf", &err), "load ramp: " + err);
    engine::Model spruce;
    check(engine::loadModelFile(spruce, ENGINE_TEST_ASSETS "/models/zone/vegetation/spruce_a_lod1.glb", &err), "load spruce: " + err);
    check(std::any_of(spruce.parts.begin(), spruce.parts.end(),
                      [](const engine::MeshPart& p) { return p.alphaMode == engine::AlphaMode::Mask && p.alphaCutoff > 0.0f; }),
          "glTF alphaMode MASK is read");
    engine::Model b = a;
    check(b.parts.sharedWith(a.parts) && &b.parts[0] == &a.parts[0], "model copies share mesh parts");
    b.parts.push_back(engine::MeshPart{});
    check(!b.parts.sharedWith(a.parts) && b.parts.size() == a.parts.size() + 1, "changing a copy detaches it");
    engine::Scene s;
    check(engine::loadScene(s, ENGINE_TEST_ASSETS "/levels/test.level", &err), "load test level: " + err);
    int shared = 0;
    for (std::size_t i = 0; i < s.models.size(); ++i)
        for (std::size_t j = i + 1; j < s.models.size(); ++j)
            if (s.models[i].sourcePath == s.models[j].sourcePath && s.models[i].parts.sharedWith(s.models[j].parts))
                ++shared;
    check(shared > 0, "level instances of one file share meshes");
}

void testObjectIds()
{
    namespace fs = std::filesystem;
    engine::Scene scene = engine::makeDefaultScene();
    engine::assignObjectIds(scene);
    bool allSet = true;
    std::set<engine::ObjectId> ids;
    for (const auto& b : scene.boxes)
        allSet &= b.id != 0 && ids.insert(b.id).second;
    for (const auto& e : scene.entities)
        allSet &= e.id != 0 && ids.insert(e.id).second;
    check(allSet && !ids.empty(), "every object gets a unique ID");
    // A copy (duplicate / paste) keeps the original's ID until assigned: then only the copy changes.
    const engine::ObjectId first = scene.boxes[0].id;
    scene.boxes.push_back(scene.boxes[0]);
    check(engine::assignObjectIds(scene) == 1 && scene.boxes[0].id == first && scene.boxes.back().id != first,
          "a duplicated object gets a new ID, the original keeps its own");
    scene.boxes[0].name = "Renamed";
    const fs::path file = fs::temp_directory_path() / "ids_roundtrip.level";
    engine::Scene back;
    check(engine::saveScene(scene, file) && engine::loadScene(back, file) && back.boxes[0].id == first &&
              engine::findObjectById(back, first) == 0,
          "IDs survive save, load and renaming");
    check(engine::objectIdFromString(engine::objectIdToString(0x0123456789abcdefull)) == 0x0123456789abcdefull &&
              engine::objectIdFromString("xyz") == 0,
          "ID text round trip");
    // Old levels (version 5, no id lines) get IDs on load.
    std::ofstream(fs::temp_directory_path() / "old_v5.level") << "version 5\nbox \"A\" 0 0 0 1 1 1 0.5 0.5 0.5\n";
    engine::Scene old;
    check(engine::loadScene(old, fs::temp_directory_path() / "old_v5.level") && old.boxes.size() == 1 && old.boxes[0].id != 0,
          "version 5 level migrates to IDs");
}

void testLights()
{
    engine::Entity e;
    e.type = "light";
    e.id = 42;
    check(e.helper(), "lights are editor helpers (not drawn or hit in the game)");
    engine::LocalLight l;
    check(engine::evaluateLight(e, 0.0, l) && l.type == engine::LightType::Point, "a new light is a lit point light");
    check(e.properties.empty(), "defaults need no properties");
    engine::LightSettings s;
    s.type = engine::LightType::Spot;
    s.range = 5.0f;
    s.pitch = 0.0f;
    s.flicker = engine::LightFlicker::Fluorescent;
    s.castShadows = true;
    engine::setLightSettings(e, s);
    const engine::LightSettings back = engine::lightSettings(e);
    check(back.type == engine::LightType::Spot && back.range == 5.0f && back.castShadows &&
              back.flicker == engine::LightFlicker::Fluorescent && !e.properties.count("intensity"),
          "light settings round-trip through properties, defaults left out");
    e.yawDegrees = 90.0f;
    engine::setLightSettings(e, engine::LightSettings{});
    e.properties["light"] = "spot";
    e.properties["pitch"] = "0";
    check(engine::evaluateLight(e, 0.0, l) && glm::length(l.direction - glm::vec3(0.0f, 0.0f, 1.0f)) < 1e-4f, "spot axis follows yaw 90 = +Z");
    e.properties["enabled"] = "0";
    check(!engine::evaluateLight(e, 0.0, l), "a switched-off light gives no light");
    for (int f = 0; f <= static_cast<int>(engine::LightFlicker::Pulse); ++f)
        for (int i = 0; i < 2000; ++i) {
            const float v = engine::lightFlickerFactor(static_cast<engine::LightFlicker>(f), 1.0f, i * 0.013, 7);
            if (v < 0.0f || v > 1.0f) {
                check(false, "flicker stays within 0..1");
                return;
            }
        }
}

void testCVars()
{
    engine::CVarRegistry r;
    int changes = 0;
    r.add("t_float", engine::CVarType::Float, "1", "test", engine::CVarConfig, 0.0, 2.0).onChange =
        [&](const engine::CVar&) { ++changes; };
    r.add("t_bool", engine::CVarType::Bool, "0", "test", engine::CVarConfig);
    r.add("t_cheat", engine::CVarType::Int, "0", "test", engine::CVarCheat);
    r.add("t_ro", engine::CVarType::String, "x", "test", engine::CVarReadOnly);
    check(r.set("t_float", "5") && r.find("t_float")->number() == 2.0f && changes == 1, "cvar clamps to max and fires onChange");
    check(!r.set("t_float", "abc") && r.find("t_float")->number() == 2.0f, "cvar rejects a non-number");
    check(r.set("t_bool", "on") && r.find("t_bool")->boolean() && r.set("t_bool", "false") && !r.find("t_bool")->boolean(),
          "bool cvar parses on/false");
    check(!r.set("t_cheat", "1"), "cheat cvar locked without cheats");
    check(!r.set("t_ro", "y"), "read-only cvar cannot be set");
    const engine::ConsoleResult show = r.execute("t_float");
    check(show.ok && show.text.find('2') != std::string::npos, "console shows a value");
    check(!r.execute("no_such_thing").ok, "unknown console word fails");
    const auto rest = r.applyCommandLine({"+t_bool", "1", "level.level"});
    check(r.find("t_bool")->boolean() && rest.size() == 1 && rest[0] == "level.level", "command line +name value");
    const std::string file = (std::filesystem::temp_directory_path() / "cvars_test.cfg").string();
    r.set("t_float", "0.25");
    check(r.saveConfig(file), "config saves");
    engine::CVarRegistry r2;
    r2.add("t_float", engine::CVarType::Float, "1", "test", engine::CVarConfig, 0.0, 2.0);
    r2.add("t_bool", engine::CVarType::Bool, "0", "test", engine::CVarConfig);
    check(r2.loadConfig(file) && r2.find("t_float")->number() == 0.25f && r2.find("t_bool")->boolean(), "config round trip");
    const auto words = engine::splitConsoleLine("say \"hello world\" 3");
    check(words.size() == 3 && words[1] == "hello world", "console line keeps quoted words");
}

void testLog()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "engine_log_test";
    fs::remove_all(dir);
    engine::setLogConsole(false);
    int heard = 0;
    const int id = engine::addLogListener([&](const engine::LogLine& l) { heard += l.category == "test"; });
    check(engine::openLogFile("unit", dir), "log file opens");
    LOG_INFO("test", "hello %d", 42);
    LOG_DEBUG("test", "hidden at the default level");
    LOG_ERROR("test", "boom\n");
    engine::removeLogListener(id);
    LOG_INFO("test", "after the listener is gone");
    const auto lines = engine::recentLog(3);
    check(lines.size() == 3 && lines[0].text == "hello 42" && lines[1].text == "boom" && lines[1].level == engine::LogLevel::Error,
          "log ring keeps the last lines, trailing newline dropped");
    check(heard == 2, "log listener hears its lines, debug is filtered");
    engine::openLogFile("unit", dir); // second run: the first file is kept as unit.1.log
    engine::closeLogFile();
    std::ifstream first(dir / "unit.1.log");
    std::string all((std::istreambuf_iterator<char>(first)), std::istreambuf_iterator<char>());
    check(all.find("hello 42") != std::string::npos && all.find("boom") != std::string::npos, "previous log rotated to unit.1.log");
    engine::setLogConsole(true);
}

void testEnsure()
{
    engine::setLogConsole(false);
    int evaluated = 0;
    auto f = [&](int v) { return ENGINE_VERIFY(++evaluated && v > 0); };
    check(f(1) && !f(0) && !f(-1) && evaluated == 3, "ENGINE_VERIFY evaluates and returns the condition");
    const auto lines = engine::recentLog(5);
    int reports = 0;
    for (const auto& l : lines)
        reports += l.text.find("ensure failed") != std::string::npos;
    check(reports == 1, "a failed ENSURE is logged once per call site");
    engine::setLogConsole(true);
}

void testSurfaces()
{
    check(engine::guessSurface("Rusty_Metal_02") == "metal", "guess metal");
    check(engine::guessSurface("old_planks") == "wood", "guess wood");
    check(engine::guessSurface("forest_ground") == "dirt", "guess dirt");
    check(engine::guessSurface("xyz").empty(), "no guess");
    engine::Material m;
    m.name = "Wall";
    check(engine::surfaceOf(&m) == "concrete", "material guessed by name");
    m.surface = "wood";
    check(engine::surfaceOf(&m) == "wood", "material's own surface wins");

    const auto dir = std::filesystem::temp_directory_path() / "engine_surfaces";
    std::filesystem::create_directories(dir);
    engine::SurfaceTable t;
    t.fallback = "concrete";
    engine::SurfaceType c;
    c.name = "concrete";
    c.footstep = dir / "sounds" / "step.sound";
    engine::SurfaceType w;
    w.name = "wood";
    w.impact = dir / "impact wood.sound";
    w.decalSize = 0.2f;
    c.ricochet = 0.6f;
    c.ricochetAngle = 25.0f;
    c.ricochetSound = dir / "ricochet.sound";
    t.types = {c, w};
    std::string error;
    check(engine::saveSurfaces(t, dir / "surfaces.cfg", &error), "save surfaces " + error);
    engine::SurfaceTable back;
    check(engine::loadSurfaces(back, dir / "surfaces.cfg", &error), "load surfaces " + error);
    check(back.types.size() == 2 && back.types[0].footstep == c.footstep && back.types[1].impact == w.impact &&
              std::abs(back.types[1].decalSize - 0.2f) < 1e-5f,
          "surfaces round trip");
    check(back.types[0].ricochet == 0.6f && back.types[0].ricochetAngle == 25.0f && back.types[0].ricochetSound == c.ricochetSound,
          "ricochet settings round trip");
    // Ricochets: grazing shots glance off, steep ones never; the new path leaves the surface.
    const glm::vec3 up(0, 1, 0);
    const glm::vec3 grazing = glm::normalize(glm::vec3(1, -0.05f, 0)), steep = glm::normalize(glm::vec3(1, -1, 0));
    check(engine::ricochetChance(c, grazing, up) > 0.5f && engine::ricochetChance(c, steep, up) == 0.0f &&
              engine::ricochetChance(w, grazing, up) == 0.0f,
          "ricochet chance depends on angle and surface");
    const glm::vec3 out = engine::ricochetDirection(grazing, up, 3.0f);
    check(out.y > 0.0f && out.x > 0.9f, "ricochet flies on, away from the surface");
    check(back.find("glass") && back.find("glass")->name == "concrete", "unknown surface falls back");

    // The physics ray reports the surface of what it hit, and its normal.
    engine::Scene scene = flatFloor();
    scene.boxes.push_back({"Metal wall", {0, 1, -3}, {4, 2, 0.2f}, {1, 1, 1}});
    engine::PhysicsWorld physics;
    physics.setLevel(scene);
    auto floor = physics.raycast({0, 1, 0}, {0, -1, 0}, 5);
    check(floor && floor->surface == "concrete" && floor->normal.y > 0.9f, "floor surface");
    auto wall = physics.raycast({0, 1, 0}, {0, 0, -1}, 10);
    check(wall && wall->surface == "metal" && wall->normal.z > 0.9f, "wall surface: " + (wall ? wall->surface : std::string("miss")));
    std::filesystem::remove_all(dir);
}

void testNavMesh()
{
    // A 40 x 40 m floor with a wall across the middle that has a gap at one end:
    // the path from one side to the other must go around through the gap.
    engine::Scene scene;
    engine::Box floor;
    floor.position = {0.0f, -0.5f, 0.0f};
    floor.size = {40.0f, 1.0f, 40.0f};
    scene.boxes.push_back(floor);
    engine::Box wall;
    wall.position = {-3.0f, 1.5f, 0.0f};
    wall.size = {34.0f, 3.0f, 0.5f}; // x from -20 to 14: gap from 14 to 20
    scene.boxes.push_back(wall);
    engine::NavMesh nav;
    std::string error;
    std::atomic<float> progress{0.0f};
    check(nav.build(scene, scene.nav, &error, &progress) && nav.ready() && nav.polygonCount() > 0 && progress == 1.0f,
          "navmesh builds from boxes " + error);
    const auto path = nav.findPath({-10.0f, 0.0f, -10.0f}, {-10.0f, 0.0f, 10.0f});
    bool aroundGap = false;
    for (const glm::vec3& p : path)
        aroundGap = aroundGap || p.x > 13.5f;
    check(path.size() >= 3 && aroundGap && glm::distance(path.back(), glm::vec3(-10.0f, 0.0f, 10.0f)) < 0.5f,
          "path goes around the wall through the gap");
    check(!nav.clearPath({-10.0f, 0.0f, -10.0f}, {-10.0f, 0.0f, 10.0f}) && nav.clearPath({-10.0f, 0.0f, -10.0f}, {5.0f, 0.0f, -10.0f}),
          "navmesh ray is blocked by the wall only");
    const auto onTop = nav.nearestPoint({0.0f, 5.0f, 0.0f}, {1.0f, 10.0f, 1.0f});
    check(!onTop || onTop->y < 1.0f, "the 3 m wall top is not walkable (too small, too high)");
    for (int i = 0; i < 20; ++i) {
        const auto p = nav.randomPointAround({0.0f, 0.0f, -10.0f}, 5.0f);
        if (!p || std::abs(p->y) > 0.3f) {
            check(false, "random patrol points lie on the floor");
            break;
        }
    }
    // Round trip of the level settings.
    scene.nav.agentRadius = 0.5f;
    scene.nav.enabled = false;
    const auto file = std::filesystem::temp_directory_path() / "nav_roundtrip.level";
    engine::Scene back;
    check(engine::saveScene(scene, file) && engine::loadScene(back, file) && back.nav.agentRadius == 0.5f && !back.nav.enabled,
          "navmesh settings round trip");
}

void testAudio()
{
    namespace fs = std::filesystem;
    // .sound files: variations relative to the file, settings round trip.
    const fs::path assets = ENGINE_TEST_ASSETS;
    engine::SoundDef rifle;
    std::string error;
    check(engine::loadSoundFile(rifle, assets / "sounds/weapons/rifle_shot.sound", &error) && rifle.files.size() >= 2 &&
              fs::exists(rifle.files[0]) && rifle.spatial && !rifle.loop && rifle.group == engine::AudioGroup::Sfx,
          "rifle_shot.sound loads its variations " + error);
    engine::SoundDef wind;
    check(engine::loadSoundFile(wind, assets / "sounds/ambient/wind.sound") && wind.loop && wind.stream && !wind.spatial &&
              wind.group == engine::AudioGroup::Ambient,
          "wind.sound is a streamed ambient loop");
    const fs::path tmp = fs::temp_directory_path() / "fps_engine_test.sound";
    engine::SoundDef copy = rifle;
    copy.pitchMin = 0.8f;
    copy.maxDistance = 123.0f;
    copy.group = engine::AudioGroup::Music;
    engine::SoundDef back;
    check(engine::saveSoundFile(copy, tmp) && engine::loadSoundFile(back, tmp) && back.files == copy.files &&
              back.pitchMin == 0.8f && back.maxDistance == 123.0f && back.group == engine::AudioGroup::Music,
          ".sound save/load round trip");
    fs::remove(tmp);

    // Ambience sets: layers and random sounds, day / night.
    {
        engine::AmbienceSet set;
        check(engine::loadAmbienceFile(set, assets / "sounds/ambience/zone_outdoor.ambience", &error) && set.layers.size() >= 2 &&
                  set.randoms.size() >= 2 && fs::exists(set.layers[0].sound) && fs::exists(set.randoms[0].sound),
              "zone_outdoor.ambience loads with existing sounds " + error);
        const fs::path amb = fs::temp_directory_path() / "fps_engine_test.ambience";
        engine::AmbienceSet back;
        check(engine::saveAmbienceFile(set, amb) && engine::loadAmbienceFile(back, amb) && back.layers.size() == set.layers.size() &&
                  back.randoms[1].sound == set.randoms[1].sound && back.randoms[1].duck == set.randoms[1].duck &&
                  back.layers[2].night == set.layers[2].night,
              ".ambience save/load round trip");
        fs::remove(amb);
        check(engine::nightFactor({0, -1, 0}) == 0.0f && engine::nightFactor({0, 1, 0}) == 1.0f &&
                  std::abs(engine::nightFactor({1, 0, 0}) - 0.5f) < 1e-4f,
              "night factor from the sun direction (twilight at the horizon)");
        engine::AmbienceRandom r;
        r.day = 0.2f;
        r.night = 1.0f;
        engine::AmbienceLayer birds;
        birds.night = 0.0f;
        check(std::abs(engine::ambienceChance(r, 0.5f) - 0.6f) < 1e-4f && engine::ambienceLayerVolume(birds, 1.0f) == 0.0f,
              "day / night chance and layer volume blend");
    }

    // Zones: full inside, smooth fade outside.
    check(engine::zoneGain({0, 0, 0}, {10, 10, 10}, 5, {5, 5, 5}) == 1.0f &&
              engine::zoneGain({0, 0, 0}, {10, 10, 10}, 5, {12.5f, 5, 5}) == 0.5f &&
              engine::zoneGain({0, 0, 0}, {10, 10, 10}, 5, {16, 5, 5}) == 0.0f,
          "ambient zone gain fades over the fade distance");

    // Offline mixing: a sound plays, is heard, ends and frees its voice; 3D distance attenuates.
    engine::AudioSystem audio;
    engine::AudioOptions options;
    options.noDevice = true;
    check(audio.init(options, &error) && !audio.hasDevice(), "audio system starts without a device " + error);
    audio.setListener({0, 0, 0}, {0, 0, -1});
    auto shot = audio.loadSound(assets / "sounds/weapons/pistol_shot.sound", &error);
    check(shot != nullptr, "pistol_shot.sound loads " + error);
    if (!shot)
        return;
    audio.preload(*shot); // decodes in the background; a sound played before that misses its start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto loudness = [&](glm::vec3 at) {
        // Decoding is asynchronous: wait for the data (the mixer outputs silence until then).
        float peak = 0.0f;
        const auto handle = audio.play(*shot, at);
        std::vector<float> buf(4800 * 2);
        for (int i = 0; i < 400 && peak == 0.0f; ++i) {
            audio.render(buf.data(), 4800);
            for (float v : buf)
                peak = std::max(peak, std::abs(v));
            if (peak == 0.0f)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        for (int i = 0; i < 6; ++i) { // the attack and body of the shot
            audio.render(buf.data(), 4800);
            for (float v : buf)
                peak = std::max(peak, std::abs(v));
        }
        audio.stop(handle);
        std::printf("audio peak at %.0f m: %.3f\n", -at.z, peak);
        return peak;
    };
    const float near = loudness({0, 0, -2}), far = loudness({0, 0, -150});
    check(near > 0.2f, "a shot 2 m away is heard");
    check(far > 0.0f && far < near * 0.2f, "a shot 150 m away is much quieter");
    // Occlusion: the same shot behind a wall is quieter and duller.
    audio.setOcclusionTest([](glm::vec3, glm::vec3) { return 1.0f; });
    const float walled = loudness({0, 0, -2});
    audio.setOcclusionTest({});
    check(walled > 0.0f && walled < near * 0.7f, "a shot behind a wall is muffled");
    // Ducking: down while held, back up after the release.
    audio.duck(engine::AudioGroup::Ambient, 0.4f, 0.3f, 0.5f);
    audio.update(0.1f);
    check(std::abs(audio.duckGain(engine::AudioGroup::Ambient) - 0.4f) < 1e-3f && audio.duckGain(engine::AudioGroup::Sfx) == 1.0f,
          "ducking lowers only its group");
    for (int i = 0; i < 10; ++i)
        audio.update(0.1f);
    check(audio.duckGain(engine::AudioGroup::Ambient) == 1.0f, "ducked group comes back after hold + release");
    // Reverb: two presets blended (zones).
    audio.setReverbMix("hallway", 0.5f, "cave", 0.25f);
    check(audio.reverb(0) == "hallway" && audio.reverb(1) == "cave" && audio.reverbAmount(1) == 0.25f, "two reverbs blend");
    audio.setReverb("none");
    const auto h = audio.play2D(*shot);
    check(audio.playing(h) && audio.activeVoices() == 1, "a playing sound holds a voice");
    for (int i = 0; i < 40; ++i)
        audio.update(0.1f); // 4 s mixed: the 0.8 s shot ends
    check(!audio.playing(h) && audio.activeVoices() == 0, "a finished sound frees its voice");
    for (int i = 0; i < 20; ++i)
        audio.play2D(*shot);
    check(audio.activeVoices() == shot->maxInstances, "max_instances caps the voices of one sound");
    audio.stopAll();
    audio.update(0.1f);

    // A stereo file played in 3D is mixed to mono and positioned (OpenAL leaves stereo buffers un-positioned).
    {
        const fs::path wav = fs::temp_directory_path() / "fps_engine_stereo.wav";
        const std::uint32_t rate = 48000, frames = rate / 2;
        std::vector<std::int16_t> pcm(frames * 2);
        for (std::uint32_t i = 0; i < frames; ++i)
            pcm[2 * i] = pcm[2 * i + 1] = static_cast<std::int16_t>(12000 * std::sin(i * 2.0 * 3.14159265 * 440.0 / rate));
        std::FILE* f = std::fopen(wav.string().c_str(), "wb");
        auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
        auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };
        const std::uint32_t bytes = frames * 4;
        std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
        u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
        std::fwrite("data", 1, 4, f); u32(bytes); std::fwrite(pcm.data(), 2, pcm.size(), f);
        std::fclose(f);
        engine::SoundDef stereo;
        stereo.files = {wav};
        audio.preload(stereo);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto handle = audio.play(stereo, {4, 0, 0}); // to the right of the listener
        std::vector<float> buf(4800 * 2);
        float left = 0.0f, right = 0.0f;
        for (int i = 0; i < 400 && right == 0.0f; ++i) {
            audio.render(buf.data(), 4800);
            for (std::size_t k = 0; k < buf.size(); k += 2) {
                left = std::max(left, std::abs(buf[k]));
                right = std::max(right, std::abs(buf[k + 1]));
            }
            if (right == 0.0f)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        audio.stop(handle);
        std::printf("stereo file at +X: left %.3f right %.3f\n", left, right);
        check(right > 0.05f && right > left * 1.5f, "a stereo file played in 3D is positioned (louder on its side)");
        fs::remove(wav);
    }
    audio.shutdown();
}

// ------------------------------------------------------------------ anomalies

engine::Scene anomalyScene(engine::AnomalyKind kind, std::map<std::string, std::string> props = {})
{
    engine::Scene s;
    engine::Entity e;
    engine::initAnomalyEntity(e, kind);
    e.name = "A";
    e.position = {0, 0, 0};
    for (auto& [k, v] : props)
        e.properties[k] = v;
    s.entities.push_back(e);
    return s;
}

// Runs the field for `seconds` with one body that follows the influences like a simple point.
struct AnomalyRun {
    float damage = 0.0f, radiation = 0.0f;
    bool teleported = false, burst = false, charged = false;
    glm::vec3 lastDrift{0.0f}, impulse{0.0f}, feet{0.0f}, teleportTo{0.0f};
};
AnomalyRun runAnomaly(engine::Scene& s, glm::vec3 feet, float seconds, bool small = false, bool move = true)
{
    engine::AnomalyField field;
    field.build(s);
    AnomalyRun r;
    std::vector<engine::AnomalyBody> bodies(1);
    std::vector<engine::AnomalyInfluence> out;
    std::vector<engine::AnomalyEvent> events;
    const float dt = 1.0f / 60.0f;
    for (float t = 0.0f; t < seconds; t += dt) {
        bodies[0] = {feet, small ? 0.05f : 1.8f, {}, small};
        events.clear();
        field.step(dt, bodies, out, events);
        const engine::AnomalyInfluence& in = out[0];
        r.damage += in.damage;
        r.radiation = std::max(r.radiation, in.radiation);
        if (glm::dot(in.drift, in.drift) > 0.0f)
            r.lastDrift = in.drift;
        r.impulse += in.impulse;
        if (in.teleport) {
            r.teleported = true;
            r.teleportTo = in.teleportTo;
            feet = in.teleportTo;
        }
        if (move)
            feet += glm::vec3(in.drift.x, 0.0f, in.drift.z) * dt;
        for (const auto& ev : events) {
            r.burst = r.burst || ev.type == engine::AnomalyEvent::Burst;
            r.charged = r.charged || ev.type == engine::AnomalyEvent::Charge;
        }
    }
    r.feet = feet;
    return r;
}

void testAnomalies()
{
    using K = engine::AnomalyKind;
    // Kinds and their parameters round-trip through the level file.
    {
        auto s = anomalyScene(K::Vent, {{"period", "3.5"}});
        const auto file = std::filesystem::temp_directory_path() / "anomaly_roundtrip.level";
        check(engine::saveScene(s, file), "anomaly level saves");
        engine::Scene loaded;
        check(engine::loadScene(loaded, file), "anomaly level loads");
        check(loaded.entities.size() == 1 && loaded.entities[0].type == "anomaly" && loaded.entities[0].helper(),
              "anomaly entity is a helper (not hit by bullets, not drawn)");
        const auto cfg = engine::anomalySettings(loaded.entities[0]);
        check(cfg.kind == K::Vent && std::abs(cfg.get("period") - 3.5f) < 1e-4f && std::abs(cfg.get("burst") - 1.8f) < 1e-4f,
              "vent kind, edited period and default burst time come back");
        check(engine::anomalyKindFromId("rift") == K::Rift && engine::anomalyParams(K::Crusher).size() > 3, "kind table");
    }
    // Crusher: pulls toward the core, squeezes, crushes and flings outward.
    {
        auto s = anomalyScene(K::Crusher); // radius 2.5, core 0.7
        auto r = runAnomaly(s, {2.0f, 0.0f, 0.0f}, 0.1f, false, false);
        check(r.lastDrift.x < -1.0f, "crusher pulls toward its middle");
        r = runAnomaly(s, {2.0f, 0.0f, 0.0f}, 3.0f);
        check(r.charged && r.burst, "crusher charges and crushes once something reaches the core");
        check(r.damage >= 60.0f, "crusher crush hurts");
        check(glm::length(r.impulse) > 5.0f && r.impulse.y > 0.0f, "crusher flings up and away");
        r = runAnomaly(s, {6.0f, 0.0f, 0.0f}, 2.0f);
        check(r.damage == 0.0f && !r.burst, "crusher leaves things outside alone");
        r = runAnomaly(s, {0.3f, 0.0f, 0.0f}, 2.0f, true);
        check(r.burst && r.damage == 0.0f, "a bolt sets the crusher off without being 'hurt'");
    }
    // Discharge: strikes after the delay, then recharges.
    {
        auto s = anomalyScene(K::Discharge, {{"idle", "0"}});
        auto r = runAnomaly(s, {0.5f, 0.0f, 0.0f}, 1.0f);
        check(r.burst && std::abs(r.damage - 35.0f) < 0.01f, "one lightning strike in the first second (35 HP)");
        r = runAnomaly(s, {0.5f, 0.0f, 0.0f}, 3.5f);
        check(r.damage > 69.0f && r.damage < 106.0f, "it strikes again after recharging");
    }
    // Vent: erupts on its cycle and when stepped on; burns while active.
    {
        auto s = anomalyScene(K::Vent, {{"on_step", "1"}, {"period", "0"}});
        auto r = runAnomaly(s, {0.2f, 0.0f, 0.0f}, 2.5f);
        check(r.burst && r.damage > 30.0f && r.damage < 80.0f, "stepping on the vent sets the fire column off (about 1.8 s x 40 HP/s)");
        auto quiet = anomalyScene(K::Vent, {{"on_step", "0"}, {"period", "0"}});
        r = runAnomaly(quiet, {0.2f, 0.0f, 0.0f}, 3.0f);
        check(!r.burst && r.damage == 0.0f, "a vent without cycle or step trigger stays quiet");
    }
    // Acid: burns and slows while wading; bolts dissolve.
    {
        auto s = anomalyScene(K::Acid);
        engine::AnomalyField field;
        field.build(s);
        std::vector<engine::AnomalyInfluence> out;
        std::vector<engine::AnomalyEvent> events;
        field.step(1.0f, {{{1.0f, 0.0f, 0.0f}, 1.8f, {}, false}, {{1.0f, 1.5f, 0.0f}, 1.8f, {}, false}}, out, events);
        check(std::abs(out[0].damage - 8.0f) < 0.01f && out[0].slow > 0.4f, "acid burns 8 HP/s and slows");
        check(out[1].damage == 0.0f, "feet above the sludge are safe");
    }
    // Rift: throws to its linked entity, not back into itself.
    {
        auto s = anomalyScene(K::Rift, {{"link", "Exit"}});
        engine::Entity exit;
        exit.name = "Exit";
        exit.position = {30.0f, 2.0f, -5.0f};
        s.entities.push_back(exit);
        auto r = runAnomaly(s, {0.2f, 0.0f, 0.0f}, 0.2f);
        check(r.teleported && glm::distance(glm::vec2(r.teleportTo.x, r.teleportTo.z), glm::vec2(30.0f, -5.0f)) < 2.0f,
              "rift throws you to its exit");
        auto lone = anomalyScene(K::Rift, {{"jump", "12"}});
        r = runAnomaly(lone, {0.2f, 0.0f, 0.0f}, 0.2f);
        check(r.teleported && glm::length(glm::vec2(r.teleportTo.x, r.teleportTo.z)) > 1.25f + 1.0f, "unlinked rift throws you out of it");
    }
    // Radiation: hotspot fades out to the radius, areas are even inside the box.
    {
        auto s = anomalyScene(K::Radiation, {{"intensity", "40"}}); // 8 m wide: radius 4
        engine::AnomalyField field;
        field.build(s);
        check(field.radiationAt({0, 1, 0}) > 35.0f, "hotspot is strongest in the middle");
        check(field.radiationAt({3, 1, 0}) < field.radiationAt({1, 1, 0}), "and weaker toward the edge");
        check(field.radiationAt({5, 1, 0}) == 0.0f, "nothing beyond its radius");
        auto area = anomalyScene(K::Radiation, {{"area", "1"}, {"intensity", "5"}, {"fade", "2"}});
        engine::AnomalyField af;
        af.build(area);
        check(std::abs(af.radiationAt({3.5f, 1, 3.5f}) - 5.0f) < 0.01f && af.radiationAt({5.0f, 1, 0}) > 0.0f &&
                  af.radiationAt({7.0f, 1, 0}) == 0.0f,
              "radiation area: even inside, fading over 2 m outside");
        check(field.detectorSignal({0, 1, 0}) == 0.0f, "the anomaly detector ignores radiation");
    }
    // Detector: stronger closer to the field edge.
    {
        auto s = anomalyScene(K::Discharge); // radius 1.75, detect 5
        engine::AnomalyField field;
        field.build(s);
        const float near = field.detectorSignal({3.0f, 1.0f, 0.0f}), far = field.detectorSignal({6.0f, 1.0f, 0.0f});
        check(near > far && far > 0.0f && field.detectorSignal({8.0f, 1.0f, 0.0f}) == 0.0f, "detector signal grows toward the field");
    }
    // Player: a push flings, drift pulls, slow slows.
    {
        auto s = flatFloor();
        engine::PhysicsWorld world;
        world.setLevel(s);
        engine::PlayerController p;
        p.spawn(world, {0, 0, 0});
        simulate(p, {}, 0.3f);
        p.push({6.0f, 5.0f, 0.0f});
        float peak = 0.0f;
        engine::FpsCamera cam;
        for (int i = 0; i < 60; ++i) {
            p.fixedUpdate({}, cam, 1.0f / 60.0f);
            peak = std::max(peak, p.feet().y);
        }
        check(peak > 0.4f && p.feet().x > 1.5f, "a push throws the player up and sideways");
        const float x = p.feet().x;
        for (int i = 0; i < 60; ++i) {
            p.drift({-3.0f, 0.0f, 0.0f});
            p.fixedUpdate({}, cam, 1.0f / 60.0f);
        }
        check(p.feet().x < x - 2.0f, "a drift pulls the player along");
        const glm::vec3 start = p.feet();
        engine::InputState in;
        in.set(engine::Action::MoveForward, true);
        for (int i = 0; i < 60; ++i) {
            p.slow(0.5f);
            p.fixedUpdate(in, cam, 1.0f / 60.0f);
        }
        const float walked = glm::length(glm::vec2(p.feet().x - start.x, p.feet().z - start.z));
        check(walked > 2.0f && walked < 3.0f, "half the walking speed while slowed (5 m/s -> about 2.5 m in 1 s)");
    }
}

void testPhysicsProps()
{
    engine::Scene s;
    std::string err;
    check(engine::loadScene(s, ENGINE_TEST_ASSETS "/levels/physics_test.level", &err), "load physics_test.level: " + err);
    const int dynamicModels = static_cast<int>(std::count_if(s.models.begin(), s.models.end(), [](const engine::Model& m) { return m.dynamic; }));
    check(dynamicModels >= 15, "physics test level has dynamic props");
    check(engine::saveScene(s, "physics_props.level", &err), "save props: " + err);
    engine::Scene t;
    check(engine::loadScene(t, "physics_props.level", &err), "reload props: " + err);
    const int tyre = engine::findObject(t, "Tyre");
    check(tyre >= 0 && t.model(tyre).dynamic && t.model(tyre).massKg == 9.0f && t.model(tyre).collider == "cylinder",
          "physics prop settings survive save + load");

    engine::PhysicsWorld world;
    world.setLevel(t);
    check(world.dynamicCount() == dynamicModels, "every dynamic model gets a body");
    const int top = engine::findObject(t, "Crate 10");
    const float topY = t.model(top).position.y;
    for (int i = 0; i < 180; ++i)
        world.step(1.0f / 60.0f);
    world.syncDynamic(t);
    check(t.model(tyre).position.y < 0.8f, "a prop placed in the air falls");
    check(std::abs(t.model(top).position.y - topY) < 0.05f, "a stacked crate pyramid stays standing");

    const glm::vec3 above = t.model(top).position + glm::vec3(0.0f, 2.0f, 0.0f);
    const auto hit = world.raycast(above, {0, -1, 0}, 5.0f);
    check(hit && hit->model == top - t.firstModel(), "a ray reports which prop it hit");
    const glm::vec3 before = t.model(top).position;
    world.applyImpulse(top - t.firstModel(), before + glm::vec3(0, 0.2f, 0.2f), {0.0f, 0.0f, -60.0f});
    for (int i = 0; i < 60; ++i)
        world.step(1.0f / 60.0f);
    world.syncDynamic(t);
    check(glm::length(t.model(top).position - before) > 0.3f, "an impulse knocks the crate off the stack");
}

void testEntityIO()
{
    const char* path = "io_listener.lua";
    std::ofstream(path) << "heard = 0\n"
                           "function on_input(self, input, param) heard = heard + 1; self.health = tonumber(param) or -1\n"
                           "  if input == 'Ping' then self:fire_output('OnPong') end end\n"
                           "function on_save(self) return { heard = heard } end\n";
    auto s = flatFloor();
    auto entity = [&](const char* name, const char* type, glm::vec3 pos, glm::vec3 size) -> engine::Entity& {
        engine::Entity e;
        e.name = name;
        e.type = type;
        e.position = pos;
        e.size = size;
        e.id = 0x100 + s.entities.size();
        s.entities.push_back(e);
        return s.entities.back();
    };
    {
        engine::Entity& t = entity("Door trigger", "trigger", {0, 0, -5}, {2, 2, 2});
        t.properties["once"] = "1";
        t.outputs.push_back({"OnTrigger", "Relay", "Trigger", "", 0.0f, false});
        t.outputs.push_back({"OnStartTouch", "!player", "ShowMessage", "entered", 0.0f, false});
    }
    {
        engine::Entity& r = entity("Relay", "relay", {0, 3, 0}, {0.4f, 0.4f, 0.4f});
        r.outputs.push_back({"OnTrigger", "Lamp", "TurnOn", "", 1.0f, false});
        r.outputs.push_back({"OnTrigger", "Listener", "Ping", "7", 0.0f, true});
    }
    entity("Lamp", "light", {0, 3, -5}, {0.3f, 0.3f, 0.3f}).properties["enabled"] = "0";
    {
        engine::Entity& l = entity("Listener", "empty", {4, 0, 0}, {0.5f, 0.5f, 0.5f});
        l.scriptPath = std::filesystem::absolute(path);
        l.outputs.push_back({"OnPong", "Lamp", "SetProperty", "range=3", 0.0f, false});
        l.outputs.push_back({"OnDeath", "Lamp", "TurnOff", "", 0.0f, false});
    }
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController player;
    player.spawn(world, {0, 0, 0});
    std::vector<std::string> messages;
    auto run = [&](engine::ScriptSystem& sys, float seconds) {
        for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
            sys.fixedUpdate(1.0f / 60.0f);
    };
    auto copy = s;
    std::vector<std::string> log;
    engine::ScriptSystem scripts(copy, world, player, [&](const std::string& m) { log.push_back(m); });
    scripts.setMessageHandler([&](const std::string& m) { messages.push_back(m); });
    check(scripts.start() == 0, "I/O test scripts start");
    run(scripts, 0.5f);
    const engine::Entity& lamp = copy.entities[2];
    const engine::Entity& listener = copy.entities[3];
    check(engine::entityEnabled(copy.entities[0]) && !engine::entityEnabled(lamp), "nothing fires before the player enters");
    player.teleport({0, 0, -5});
    run(scripts, 0.1f);
    check(messages.size() == 1 && messages[0] == "entered", "OnStartTouch -> !player ShowMessage");
    check(!engine::entityEnabled(copy.entities[0]), "a once trigger switches itself off");
    check(std::abs(listener.health - 7.0f) < 1e-4f, "relay -> script on_input with the parameter");
    check(lamp.properties.count("range") && lamp.properties.at("range") == "3", "script fire_output -> SetProperty");
    check(!engine::entityEnabled(lamp) && scripts.pendingInputs() == 1, "a delayed input waits");

    // Saved while the lamp's input is still on its way.
    const auto saved = scripts.saveEntities();
    const auto pending = scripts.savePendingInputs();
    run(scripts, 1.0f);
    check(engine::entityEnabled(lamp), "the delayed TurnOn arrives");
    player.teleport({0, 0, 0});
    run(scripts, 0.2f);
    player.teleport({0, 0, -5});
    run(scripts, 0.2f);
    check(messages.size() == 1, "a used-up once trigger stays quiet");
    scripts.sendInput("Relay", "Trigger");
    run(scripts, 0.1f);
    check(std::abs(listener.health - 7.0f) < 1e-4f && log.end() == std::find_if(log.begin(), log.end(), [](const std::string& m) {
              return m.find("no input") != std::string::npos;
          }),
          "a once connection fires only once");
    scripts.damageEntity(3, 1000.0f);
    run(scripts, 0.05f);
    check(!engine::entityEnabled(lamp), "OnDeath -> TurnOff");

    // Load: properties changed by inputs, used triggers and the waiting input come back.
    auto fresh = s;
    engine::ScriptSystem again(fresh, world, player, [](const std::string&) {});
    again.start();
    again.restoreEntities(saved);
    again.restorePendingInputs(pending);
    check(!engine::entityEnabled(fresh.entities[0]) && fresh.entities[2].properties.count("range"), "saved I/O state restored");
    check(again.pendingInputs() == 1 && !engine::entityEnabled(fresh.entities[2]), "the waiting input is restored, OnStart ones dropped");
    run(again, 1.1f);
    check(engine::entityEnabled(fresh.entities[2]), "the restored input arrives after the rest of its delay");

    // Level file round trip.
    std::string err;
    s.environment.reverb = "hallway";
    s.environment.reverbAmount = 0.4f;
    check(engine::saveScene(s, "io_round_trip.level", &err), "save I/O level: " + err);
    engine::Scene loaded;
    check(engine::loadScene(loaded, "io_round_trip.level", &err), "load I/O level: " + err);
    check(loaded.entities.size() == s.entities.size() && loaded.entities[0].outputs == s.entities[0].outputs &&
              loaded.entities[1].outputs == s.entities[1].outputs && loaded.entities[1].outputs[0].delay == 1.0f &&
              loaded.entities[1].outputs[1].once,
          "outputs survive save + load");
    check(loaded.environment.reverb == "hallway" && loaded.environment.reverbAmount == 0.4f, "level reverb survives save + load");

    // Renaming an entity keeps the connections pointing at it.
    check(engine::renameNode(s, "Lamp", "Hall lamp") && s.entities[1].outputs[0].target == "Hall lamp", "rename updates I/O targets");
}

void testTimeOfDay()
{
    engine::TimeOfDay t;
    t.latitude = 50.0f;
    t.dayOfYear = 172;
    // Summer solstice at 50 N: the sun culminates at 90 - 50 + 23.44 = 63.4 degrees, in the south (+Z).
    const glm::vec3 noon = engine::sunPosition(t, 12.0f);
    check(std::abs(std::asin(noon.y) * 57.2958f - 63.44f) < 0.5f, "noon altitude " + std::to_string(std::asin(noon.y) * 57.2958f));
    check(noon.z > 0.1f && std::abs(noon.x) < 1e-3f, "noon sun in the south");
    check(engine::sunPosition(t, 0.0f).y < 0.0f, "sun below the horizon at midnight");
    check(engine::sunPosition(t, 6.0f).x > 0.5f, "morning sun in the east");
    check(engine::sunPosition(t, 18.0f).x < -0.5f, "evening sun in the west");
    t.dayOfYear = 355; // winter: lower noon sun, sunrise after 07:00
    check(engine::sunPosition(t, 12.0f).y < noon.y - 0.4f, "winter noon lower");
    check(engine::sunPosition(t, 7.0f).y < 0.0f, "winter: dark at 07:00");
    t.moonPhase = 0.5f; // full moon is up at midnight, opposite the sun
    check(engine::moonPosition(t, 0.0f).y > 0.3f, "full moon high at midnight in winter");
    check(glm::dot(engine::moonPosition(t, 0.0f), engine::sunPosition(t, 0.0f)) < -0.9f, "full moon opposite the sun");

    // Weather keys interpolate and wrap around midnight.
    std::vector<engine::WeatherKey> keys(2);
    keys[0].hour = 22.0f;
    keys[0].rain = 1.0f;
    keys[1].hour = 6.0f;
    keys[1].rain = 0.0f;
    check(std::abs(engine::weatherAt(keys, 2.0f).rain - 0.5f) < 1e-3f, "weather wraps midnight");
    check(engine::weatherAt(keys, 6.0f).rain < 1e-4f && engine::weatherAt(keys, 22.0f).rain > 0.999f, "weather at keys");
    check(engine::weatherAt(keys, 14.0f).rain > 0.4f && engine::weatherAt(keys, 14.0f).rain < 0.6f, "weather between keys");

    // Lighting: authored at noon, red sunset, dark night lit by the moon; off = untouched.
    engine::Environment env;
    env.time.enabled = true;
    env.time.dayOfYear = 172;
    env.time.hour = 13.0f;
    engine::SkyState sky;
    const engine::Environment day = engine::environmentAt(env, {0, -1, 0}, &sky);
    check(sky.night < 0.01f && sky.lightDirection.y < -0.7f, "day light from above");
    check(day.sunIntensity > env.sunIntensity * 0.8f, "noon sun nearly full");
    env.time.hour = 19.6f;
    const engine::Environment dusk = engine::environmentAt(env, {0, -1, 0}, &sky);
    check(dusk.sunColor.b < dusk.sunColor.r * 0.8f && sky.twilight > 0.3f, "sunset is red");
    env.time.hour = 0.5f;
    const engine::Environment night = engine::environmentAt(env, {0, -1, 0}, &sky);
    check(sky.night > 0.99f && sky.stars > 0.9f, "night with stars");
    check(night.skyZenith.b < day.skyZenith.b * 0.5f && night.sunIntensity < 0.2f, "night is dark");
    env.time.keys = keys;
    check(std::abs(engine::environmentAt(env, {0, -1, 0}).rain - 0.768f) < 0.01f, "weather keys drive rain");
    env.time.enabled = false;
    check(engine::environmentAt(env, {1, -1, 0}, &sky).sunIntensity == env.sunIntensity &&
              std::abs(sky.lightDirection.x - 0.7071f) < 1e-3f, "cycle off: authored sun");

    // Saved with the level.
    engine::Scene s = flatFloor();
    s.environment.time.enabled = true;
    s.environment.time.hour = 21.25f;
    s.environment.time.dayMinutes = 30.0f;
    s.environment.time.latitude = 56.0f;
    s.environment.time.dayOfYear = 280;
    s.environment.time.moonPhase = 0.25f;
    std::string err;
    keys[0].storm = 0.7f;
    s.environment.time.keys = keys;
    s.environment.storm = 0.4f;
    s.environment.puddles = 0.3f;
    s.environment.windDirection = 135.0f;
    s.environment.windGusts = 0.8f;
    {
        engine::Model m;
        check(engine::loadModelFile(m, ENGINE_TEST_ASSETS "/models/ramp.gltf", &err), "load ramp for sway: " + err);
        m.name = "Bush";
        m.sway = 0.7f;
        s.models.push_back(m);
    }
    s.environment.thunderSound = std::filesystem::absolute("thunder_custom.sound");
    check(engine::saveScene(s, "time_of_day.level", &err), "save time: " + err);
    engine::Scene l;
    check(engine::loadScene(l, "time_of_day.level", &err), "load time: " + err);
    const engine::TimeOfDay& r = l.environment.time;
    check(r.enabled && r.hour == 21.25f && r.dayMinutes == 30.0f && r.latitude == 56.0f && r.dayOfYear == 280 && r.moonPhase == 0.25f,
          "time of day round trip");
    check(r.keys.size() == 2 && r.keys[0].hour == 22.0f && r.keys[0].rain == 1.0f && r.keys[0].storm == 0.7f, "weather keys round trip");
    check(l.environment.storm == 0.4f && l.environment.puddles == 0.3f && l.environment.thunderSound.filename() == "thunder_custom.sound",
          "storm settings round trip");
    check(l.environment.windDirection == 135.0f && l.environment.windGusts == 0.8f, "wind round trip");
    check(!l.models.empty() && l.models.back().sway == 0.7f, "model sway round trip");
    {
        engine::ParticleEffect fx;
        fx.emitters.emplace_back();
        fx.emitters[0].sheltered = true;
        fx.emitters[0].levelWind = 0.6f;
        check(engine::saveParticleFile(fx, "sheltered.particle", &err), "save particle: " + err);
        engine::ParticleEffect loaded;
        check(engine::loadParticleFile(loaded, "sheltered.particle", &err) && !loaded.emitters.empty() && loaded.emitters[0].sheltered && loaded.emitters[0].levelWind == 0.6f,
              "particle sheltered round trip");
    }
    char text[8];
    check(std::string(engine::formatHour(21.25f, text)) == "21:15" && std::string(engine::formatHour(24.5f, text)) == "00:30", "format hour");
}

void testAssetPalette()
{
    auto image = [](std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        std::vector<std::uint8_t> px(16 * 16 * 4);
        for (std::size_t i = 0; i < px.size(); i += 4) {
            px[i] = r;
            px[i + 1] = g;
            px[i + 2] = b;
            px[i + 3] = 255;
        }
        return px;
    };
    const auto gray = image(128, 125, 120);
    const engine::AlbedoStats ok = engine::albedoStats(gray.data(), 16, 16);
    check(ok.pixels > 0 && engine::paletteProblems(ok).empty(), "mid gray albedo is fine");
    const auto black = image(10, 10, 10);
    const engine::AlbedoStats dark = engine::albedoStats(black.data(), 16, 16);
    check(dark.tooDark > 0.99f && !engine::paletteProblems(dark).empty(), "black albedo too dark");
    const auto white = image(250, 250, 252);
    check(engine::albedoStats(white.data(), 16, 16).tooBright > 0.99f, "white albedo too bright");
    const auto red = image(220, 20, 20);
    const engine::AlbedoStats sat = engine::albedoStats(red.data(), 16, 16);
    check(sat.meanSaturation > 0.7f && !engine::paletteProblems(sat).empty(), "pure red too saturated");
    const auto rust = image(110, 62, 34);
    check(engine::paletteProblems(engine::albedoStats(rust.data(), 16, 16)).empty(), "rust brown is fine");
    // The material color tints the map (gray x dark tint = too dark); no map = the color alone.
    check(engine::albedoStats(gray.data(), 16, 16, glm::vec3(0.1f)).tooDark > 0.99f, "tint darkens");
    check(engine::albedoStats(nullptr, 0, 0, glm::vec3(0.5f)).pixels == 1, "color only");
    // Cut-out pixels (alpha < 50 %) are ignored.
    auto leaves = image(10, 10, 10);
    for (std::size_t i = 3; i < leaves.size(); i += 4)
        leaves[i] = 0;
    check(engine::albedoStats(leaves.data(), 16, 16).pixels == 0, "transparent pixels skipped");
    // Real assets: a material and a glTF model of the project.
    const auto entries = engine::checkAssetPalette(ENGINE_TEST_ASSETS "/models/ramp.gltf");
    check(!entries.empty() && entries[0].stats.pixels > 0, "glTF model palette");
}

void testWater()
{
    // A 10 x 4 m canal, 2 m deep, turned 90 degrees (runs along Z), flowing at 1 m/s.
    engine::Scene s;
    engine::Entity e;
    e.type = "water";
    e.name = "Canal";
    e.position = {0.0f, -2.0f, 0.0f};
    e.size = {10.0f, 2.0f, 4.0f};
    e.yawDegrees = 90.0f;
    e.properties["flow"] = "1";
    e.properties["clarity"] = "0.5";
    s.entities.push_back(e);
    engine::WaterBody w;
    check(engine::waterAt(s, {0.0f, -1.0f, 4.5f}, &w) && !engine::waterAt(s, {4.5f, -1.0f, 0.0f}) &&
              !engine::waterAt(s, {0.0f, 0.5f, 0.0f}) && std::abs(w.level) < 1e-5f && std::abs(w.flow.y - 1.0f) < 1e-4f &&
              w.clarity == 0.5f,
          "water: box, yaw, flow along the facing");
    // Shots from above stop at the surface; from below they do not.
    engine::PhysicsWorld physics;
    physics.setLevel(s);
    const engine::ShotHit down = engine::traceShot(s, physics, {0.0f, 3.0f, 2.0f}, glm::normalize(glm::vec3(0.0f, -1.0f, 0.2f)), 50.0f);
    check(down.hit && down.surface == "water" && std::abs(down.point.y) < 1e-4f, "water: shot splashes on the surface");
    const engine::ShotHit up = engine::traceShot(s, physics, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 50.0f);
    check(!up.hit || up.surface != "water", "water: shot from below goes through");
    // Swimming: deep water holds the player near the surface instead of sinking.
    engine::Box floor;
    floor.position = {0.0f, -2.5f, 0.0f};
    floor.size = {40.0f, 1.0f, 40.0f};
    engine::Scene pool = s;
    pool.boxes.push_back(floor);
    pool.entities[0].size = {20.0f, 2.0f, 20.0f};
    engine::PhysicsWorld world;
    world.setLevel(pool);
    engine::PlayerController player;
    player.spawn(world, {0.0f, -1.9f, 0.0f});
    engine::InputState input;
    engine::FpsCamera camera;
    for (int i = 0; i < 240; ++i) {
        if (engine::WaterBody b; engine::waterAt(pool, player.feet(), &b))
            player.setWater(b.level, b.flow);
        player.fixedUpdate(input, camera, 1.0f / 60.0f);
        world.step(1.0f / 60.0f);
    }
    check(player.swimming() && std::abs(player.eye().y - 0.2f) < 0.35f,
          "water: player floats with the head out (eye " + std::to_string(player.eye().y) + ")");
    check(std::abs(player.feet().z) > 0.5f, "water: the current carries the player");
}

int main()
{
    // Hermetic cook cache for the tests.
    engine::textureCookConfig().cacheDir = std::filesystem::current_path() / "texture_cache";
    std::filesystem::remove_all(engine::textureCookConfig().cacheDir);
    testTextureCooking();
    testSharedMeshes();
    testTerrain();
    testFixedStep();
    testFallsAndLands();
    testWallBlocks();
    testClimbsSteps();
    testJump();
    testSceneRoundTrip();
    testGltfRamp();
    testLevelWithModel();
    testRaycast();
    testModelCollisionFlags();
    testScriptApi();
    testScriptErrorIsReported();
    testEnemyChasesAndAttacks();
    testEnemyBlockedBySight();
    testWeaponFireRateAndReload();
    testShotHitsEnemyNotWallBehind();
    testEnemyDiesAndReactsToDamage();
    testTexturedWeaponModel();
    testWeaponFilesAndLoadout();
    testSemiAutomatic();
    testKnifeAndGuns();
    testHierarchyAndDependencies();
    testJobSystem();
    testRotateGizmo();
    testRotatedBox();
    testScriptPropertiesFromEditor();
    testPick();
    testAnimatedCharacter();
    testEnemyAnimations();
    testScriptSyntaxCheck();
    testMaterialsAndTextures();
    testAudio();
    testNavMesh();
    testObjectIds();
    testCVars();
    testLights();
    testScriptSaveRestore();
    testLog();
    testEnsure();
    testSurfaces();
    testAnomalies();
    testPhysicsProps();
    testEntityIO();
    testTimeOfDay();
    testAssetPalette();
    testWater();
    std::printf(failures ? "%d test(s) failed\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
