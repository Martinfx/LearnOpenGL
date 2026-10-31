// Headless tests for the engine core (no OpenGL needed).
#include <engine/Camera.h>
#include <engine/FixedStepClock.h>
#include <engine/Physics.h>
#include <engine/PlayerController.h>
#include <engine/Scene.h>
#include <engine/Scripting.h>
#include <engine/JobSystem.h>
#include <engine/Material.h>
#include <engine/SceneGraph.h>
#include <engine/Weapon.h>

#include <atomic>
#include <fstream>
#include <glm/geometric.hpp>

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
    engine::WeaponSettings blaster; // the pistol
    check(engine::loadWeaponFile(blaster, level.loadout.at(1), &err), "load pistol.weapon: " + err);
    check(blaster.magazineSize == 12 && !blaster.automatic, "pistol is semi-automatic");
    check(std::filesystem::exists(blaster.modelPath), "weapon model path resolves next to the .weapon file");

    blaster.damage = 11.5f;
    check(engine::saveWeaponFile(blaster, "copy.weapon", &err), "save weapon: " + err);
    engine::WeaponSettings copy;
    check(engine::loadWeaponFile(copy, "copy.weapon", &err) && copy.damage == 11.5f, "weapon file round trip");
    check(std::filesystem::equivalent(copy.modelPath, blaster.modelPath), "model path rewritten relative to the new file");
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
    check(s.groups.size() == 4 && engine::nodeParent(s, "Crate B") == "Crate A", "test level has a hierarchy");
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
    check(props.size() == 23 && props[0].name == "health" && props[0].defaultValue == "100", "enemy.lua declares 23 tunables, health first");

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
    check(engine::saveMaterial(copy, "copy.material", &err), "save material: " + err);
    auto loaded = engine::loadMaterial("copy.material", &err);
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
    testWeaponFireRateAndReload();
    testShotHitsEnemyNotWallBehind();
    testEnemyDiesAndReactsToDamage();
    testTexturedWeaponModel();
    testWeaponFilesAndLoadout();
    testSemiAutomatic();
    testKnifeAndGuns();
    testHierarchyAndDependencies();
    testJobSystem();
    testScriptPropertiesFromEditor();
    testPick();
    testAnimatedCharacter();
    testEnemyAnimations();
    testScriptSyntaxCheck();
    testMaterialsAndTextures();
    std::printf(failures ? "%d test(s) failed\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
