// Headless tests for the engine core (no OpenGL needed).
#include <engine/Log.h>
#include <engine/Demo.h>
#include <engine/MeshCache.h>
#include <engine/Vfs.h>
#include <engine/Localization.h>
#include <cstring>
#include <set>
#include <engine/Assert.h>
#include <engine/ALife.h>
#include <engine/Anomaly.h>
#include <engine/Fauna.h>
#include <engine/Dialog.h>
#include <engine/Quests.h>
#include <engine/Lights.h>
#include <engine/EntityIO.h>
#include <engine/Ambience.h>
#include <engine/AssetPalette.h>
#include <engine/Particles.h>
#include <engine/AssetCredits.h>
#include <engine/Audio.h>
#include <engine/NavMesh.h>
#include <engine/Surfaces.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <engine/Gizmo.h>
#include <engine/Camera.h>
#include <engine/FixedStepClock.h>
#include <engine/Physics.h>
#include <engine/Ragdoll.h>
#include <engine/PlayerController.h>
#include <engine/Probes.h>
#include <engine/Renderer.h>
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
#include <sstream>
#include <glm/geometric.hpp>

#include <cmath>
#include <cstdio>
#include <engine/CVars.h>
#include <engine/Water.h>
#include <engine/WorldStreaming.h>
#include <engine/FrameArena.h>
#include <engine/Memory.h>
#include <engine/AssetRegistry.h>
#include <engine/FileWatcher.h>
#include <engine/HotReload.h>
#include <engine/Items.h>
#include <engine/ScriptDebug.h>
#include <engine/Prefab.h>
#include <engine/RangeAllocator.h>
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
    check(engine::loadScene(s, ENGINE_TEST_DATA "/scene.level", &err), "load scene.level: " + err);
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

void testDemo()
{
    // Round trip of a recording.
    engine::Demo demo;
    demo.level = "levels/zone.level";
    demo.seed = 1234567u;
    demo.cvars["m_sensitivity"] = "0.12";
    for (int i = 0; i < 300; ++i) {
        engine::DemoFrame f;
        f.dt = 1.0f / 60.0f;
        f.mouseDelta = glm::vec2(float(i % 7) - 3.0f, 0.5f);
        f.cursor = glm::vec2(640.0f, 360.0f);
        f.scroll = i == 10 ? 1.0f : 0.0f;
        if (i % 3)
            f.keys = {87, static_cast<std::uint16_t>(engine::kDemoMouseBase + 0)};
        f.player = glm::vec3(0.0f, 0.0f, -0.08f * float(i));
        demo.frames.push_back(f);
    }
    const auto file = std::filesystem::temp_directory_path() / "engine_test.demo";
    std::string error;
    check(engine::saveDemo(demo, file, &error), "demo saves: " + error);
    engine::Demo back;
    check(engine::loadDemo(back, file, &error), "demo loads: " + error);
    check(back.level == demo.level && back.seed == demo.seed && back.cvars == demo.cvars, "demo header round trip");
    bool same = back.frames.size() == demo.frames.size();
    for (std::size_t i = 0; same && i < back.frames.size(); ++i) {
        const auto &a = demo.frames[i], &b = back.frames[i];
        same = a.dt == b.dt && a.mouseDelta == b.mouseDelta && a.cursor == b.cursor && a.scroll == b.scroll && a.keys == b.keys &&
               a.player == b.player;
    }
    check(same, "demo frames round trip bit for bit");
    check(std::abs(back.duration() - 5.0f) < 1e-3f, "300 frames at 60 Hz last 5 s");
    {
        std::ofstream cut(file, std::ios::binary | std::ios::trunc);
        cut << "FPSDEMO1";
    }
    check(!engine::loadDemo(back, file, &error) && !error.empty(), "a damaged demo is an error");
    {
        engine::Demo empty;
        empty.level = "levels/test.level";
        check(engine::saveDemo(empty, file), "an empty demo saves");
        error.clear();
        check(!engine::loadDemo(back, file, &error) && error.find("no frames") != std::string::npos,
              "a demo without frames is refused (nothing to replay)");
    }
    std::filesystem::remove(file);

    // Seeds: the same game seed gives the same streams, streams differ from each other.
    const std::uint32_t old = engine::gameSeed();
    engine::setGameSeed(42u);
    const std::uint32_t a = engine::seedFor("weather"), b = engine::seedFor("jam");
    engine::setGameSeed(43u);
    check(engine::seedFor("weather") != a, "another game seed changes the streams");
    engine::setGameSeed(42u);
    check(engine::seedFor("weather") == a && a != b, "streams are reproducible and independent");
    engine::setGameSeed(old);

    // Benchmark statistics: 99 frames of 10 ms and one of 50 ms.
    std::vector<float> times(99, 0.010f);
    times.push_back(0.050f);
    const engine::FrameStats st = engine::frameStats(times);
    check(st.frames == 100 && std::abs(st.seconds - 1.04) < 1e-4, "frame count and time");
    check(std::abs(st.averageFps - 100.0 / 1.04) < 0.01, "average FPS");
    check(std::abs(st.low1Fps - 20.0) < 0.01 && std::abs(st.worstMs - 50.0) < 0.01, "1 % low is the slowest frame");
    check(std::abs(st.medianMs - 10.0) < 0.01, "median frame time");
    check(engine::formatFrameStats(st).find("1% low 20.0 FPS") != std::string::npos, "formatted statistics");
}

void testMeshCache()
{
    // A textured triangle: glTF + external .bin + external BMP image, all written here.
    const auto dir = std::filesystem::temp_directory_path() / "mesh_cache_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        std::ofstream bin(dir / "tri.bin", std::ios::binary);
        const float pos[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        const float uv[6] = {0, 0, 1, 0, 0, 1};
        const std::uint16_t idx[4] = {0, 1, 2, 0}; // padded to 4 bytes
        bin.write(reinterpret_cast<const char*>(pos), sizeof pos);
        bin.write(reinterpret_cast<const char*>(uv), sizeof uv);
        bin.write(reinterpret_cast<const char*>(idx), sizeof idx);
    }
    {
        // 2x2 RGB PNG.
        const std::uint8_t png[77] = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 0, 2, 0, 0, 0, 2, 8, 2, 0,
                                      0, 0, 253, 212, 154, 115, 0, 0, 0, 20, 73, 68, 65, 84, 120, 156, 99, 248, 207, 192, 192, 0, 194,
                                      12, 255, 255, 255, 103, 0, 0, 30, 239, 4, 252, 163, 200, 180, 247, 0, 0, 0, 0, 73, 69, 78, 68,
                                      174, 66, 96, 130};
        std::ofstream(dir / "tex.png", std::ios::binary).write(reinterpret_cast<const char*>(png), sizeof png);
    }
    {
        std::ofstream gltf(dir / "tri.gltf");
        gltf << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"mesh":0,"translation":[0,0.5,0]}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0}]}],
"materials":[{"name":"rust","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"roughnessFactor":0.6}}],
"textures":[{"source":0}],"images":[{"uri":"tex.png"}],
"buffers":[{"uri":"tri.bin","byteLength":68}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":6}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    }
    const auto file = dir / "tri.gltf";
    std::string err;
    engine::Model first;
    check(engine::loadModelFile(first, file, &err), "load the test glTF: " + err);
    check(std::filesystem::exists(engine::meshCachePath(file)), "loading cooks the mesh into the cache");
    engine::Model cached;
    check(engine::readMeshCache(cached, file), "the cooked mesh reads back");
    const bool same = cached.parts.size() == 1 && first.parts.size() == 1 && cached.parts[0].positions == first.parts[0].positions &&
                      cached.parts[0].indices == first.parts[0].indices && cached.parts[0].texcoords == first.parts[0].texcoords &&
                      cached.parts[0].materialName == "rust" && std::abs(cached.parts[0].roughness - 0.6f) < 1e-6f &&
                      cached.parts[0].texture && first.parts[0].texture &&
                      cached.parts[0].texture->cookedFile == first.parts[0].texture->cookedFile &&
                      cached.parts[0].texture->width == 2 && cached.localBounds.max.y == first.localBounds.max.y;
    check(same, "cooked mesh equals the glTF (vertices, material, texture, bounds)");
    check(std::abs(cached.parts[0].positions[2].y - 1.5f) < 1e-6f, "node transform stays baked in");
    // A changed input (the image) makes the cache stale.
    std::filesystem::last_write_time(dir / "tex.png", std::filesystem::last_write_time(dir / "tex.png") + std::chrono::seconds(5));
    engine::Model stale;
    check(!engine::readMeshCache(stale, file), "a changed image invalidates the cooked mesh");
    check(engine::loadModelFile(stale, file, &err) && engine::readMeshCache(cached, file), "the next load cooks it again");
    std::filesystem::remove_all(dir);
}

void testVfs()
{
    namespace fs = std::filesystem;
    const auto base = fs::temp_directory_path() / "vfs_test";
    fs::remove_all(base);
    auto write = [](const fs::path& f, const std::string& text) {
        fs::create_directories(f.parent_path());
        std::ofstream(f, std::ios::binary) << text;
    };
    const auto assets = base / "assets";
    write(assets / "a.txt", "loose a");
    write(assets / "sub" / "b.txt", "loose b");
    write(assets / "weapons" / "base.weapon", "# FPS engine weapon\nversion 2\nname \"Base\"\ndamage 10\n");
    // The archive replaces b and adds c and a weapon that inherits from the loose one.
    const auto packSrc = base / "pack";
    write(packSrc / "sub" / "b.txt", "packed b");
    write(packSrc / "sub" / "c.txt", "packed c");
    write(packSrc / "weapons" / "child.weapon", "# FPS engine weapon\nversion 2\nbase \"base.weapon\"\nname \"Child\"\n");
    std::string error;
    check(engine::writeArchive(base / "game.pak", packSrc, {"sub/b.txt", "sub/c.txt", "weapons/child.weapon"}, &error),
          "archive written: " + error);
    std::vector<std::pair<std::string, std::uint64_t>> toc;
    check(engine::listArchive(base / "game.pak", toc) && toc.size() == 3 && toc[0].first == "sub/b.txt" && toc[0].second == 8,
          "archive table of contents");
    // A mod folder replaces a and the archive's c.
    write(base / "mods" / "fix" / "a.txt", "mod a");
    write(base / "mods" / "fix" / "sub" / "c.txt", "mod c");

    engine::Vfs& v = engine::vfs();
    v.clear();
    v.setRoot(assets);
    check(v.mountArchive(base / "game.pak", &error), "archive mounts: " + error);
    v.mountFolder(base / "mods" / "fix");
    std::string text;
    check(v.read(assets / "a.txt", text) && text == "mod a", "a mod replaces a loose file");
    check(v.read(assets / "sub" / "b.txt", text) && text == "packed b", "an archive replaces a loose file");
    check(v.read(assets / "sub" / "c.txt", text) && text == "mod c", "a mod replaces an archived file");
    check(v.exists(assets / "weapons" / "child.weapon") && !v.exists(assets / "nothing.txt"), "exists sees every layer");
    const auto listed = v.list(assets / "sub");
    check(listed.size() == 2 && listed[0] == assets / "sub" / "b.txt" && listed[1] == assets / "sub" / "c.txt",
          "a folder lists the files of all layers once");
    check(v.list(assets, true).size() == 5, "recursive listing from the root");
    engine::WeaponSettings w;
    check(engine::loadWeaponFile(w, assets / "weapons" / "child.weapon", &error) && w.name == "Child" && w.damage == 10.0f,
          "a loader reads an archived file that inherits from a loose one: " + error);
    const auto extracted = v.diskPath(assets / "weapons" / "child.weapon");
    check(!extracted.empty() && engine::VfsStream(extracted).is_open(), "an archived file can be extracted for libraries");
    check(v.diskPath(assets / "a.txt") == (base / "mods" / "fix" / "a.txt").lexically_normal(), "disk path of a mod file is the mod's");
    const std::string before = v.identity(assets / "a.txt");
    fs::remove(base / "mods" / "fix" / "a.txt");
    check(v.identity(assets / "a.txt") != before && v.read(assets / "a.txt", text) && text == "loose a",
          "without the mod file the loose one is back (and cache keys change)");
    check(!engine::VfsStream(assets / "missing.txt"), "a missing file fails like an ifstream");
    v.clear();
    v.setRoot({});
    fs::remove_all(extracted.parent_path().parent_path());
    fs::remove_all(base);
}

void testLocalization()
{
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "text_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        std::ofstream(dir / "cs.strings") << "# test\nmenu.back = Zpět\nhud.ammo.one = {0} náboj\nhud.ammo.few = {0} náboje\n"
                                             "hud.ammo.other = {0} nábojů\nhud.only_cs = Jen česky\nhud.two = {0} a {1}\nhud.lines = a\\nb\n";
        std::ofstream(dir / "en.strings") << "menu.back = Back\nhud.ammo.one = {0} round\nhud.ammo.other = {0} rounds\nhud.lines = x\n";
    }
    engine::Localization l;
    std::string error;
    check(l.load(dir, "cs", &error), "Czech texts load: " + error);
    check(l.text("menu.back") == "Zpět" && l.text("hud.lines") == "a\nb", "Czech text and \\n");
    check(l.plural("hud.ammo", 1) == "1 náboj" && l.plural("hud.ammo", 3) == "3 náboje" && l.plural("hud.ammo", 5) == "5 nábojů",
          "Czech plurals 1 / 2-4 / 5+");
    check(l.text("no.such.key") == "no.such.key", "a missing key shows itself");
    check(l.load(dir, "en", &error), "English texts load: " + error);
    check(l.text("menu.back") == "Back" && l.text("hud.only_cs") == "Jen česky", "English, Czech as the fallback");
    check(l.plural("hud.ammo", 1) == "1 round" && l.plural("hud.ammo", 3) == "3 rounds", "English plurals");
    check(engine::formatText(l.text("hud.two"), {"A", "B"}) == "A a B" && l.localize("@menu.back") == "Back" && l.localize("plain") == "plain",
          "arguments and @key data strings");
    check(l.textOr("menu.back", "x") == "Back" && l.textOr("hud.only_cs", "written") == "written",
          "textOr: only the current language, else the written text");
    engine::StringTable cs, en;
    check(cs.load(dir / "cs.strings") && en.load(dir / "en.strings"), "tables load");
    const auto missing = engine::untranslatedKeys(cs, en, "en");
    check(missing == std::vector<std::string>{"hud.only_cs", "hud.two"}, "untranslated: English needs .one/.other only, not .few");
    check(engine::availableLanguages(dir) == std::vector<std::string>{"cs", "en"}, "languages, Czech first");
    // Saving keeps every text (and escapes line breaks).
    check(en.save(dir / "en2.strings") && cs.load(dir / "en2.strings") && cs.texts == en.texts, "a table saves and loads back");
    fs::remove_all(dir);

    // Every key the game asks for exists in the shipped Czech table.
    engine::StringTable shipped;
    check(shipped.load(ENGINE_TEST_ASSETS "/text/cs.strings", &error), "assets/text/cs.strings: " + error);
    std::vector<std::string> unknown;
    for (const auto& entry : fs::directory_iterator(fs::path(ENGINE_TEST_ASSETS).parent_path() / "game")) {
        std::ifstream in(entry.path());
        const std::string code((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (const char* call : {"tr(\"", "trf(\"", "trn(\""})
            for (std::size_t at = code.find(call); at != std::string::npos; at = code.find(call, at + 1)) {
                if (at > 0 && (std::isalnum(static_cast<unsigned char>(code[at - 1])) || code[at - 1] == '_'))
                    continue; // part of another name
                if (at >= 2 && code.compare(at - 2, 2, "::") == 0 && (at < 8 || code.compare(at - 8, 8, "engine::") != 0))
                    continue; // QObject::tr and the like
                const std::size_t start = at + std::strlen(call), end = code.find('"', start);
                const std::string key = code.substr(start, end - start);
                if (key.empty() || key.back() == '.')
                    continue; // a prefix completed at run time ("language." + code)
                const bool plural = call[2] == 'n';
                if (!shipped.texts.count(key) && !(plural && shipped.texts.count(key + ".other")))
                    unknown.push_back(entry.path().filename().string() + ": " + key);
            }
    }
    check(unknown.empty(), "keys used by the game exist in cs.strings" + (unknown.empty() ? std::string() : ": " + unknown.front()));

    // Sound captions (subtitles) name keys that exist.
    std::vector<std::string> noCaption;
    int captioned = 0;
    for (const auto& entry : fs::recursive_directory_iterator(ENGINE_TEST_ASSETS "/sounds")) {
        engine::SoundDef sound;
        if (entry.path().extension() != ".sound" || !engine::loadSoundFile(sound, entry.path()) || sound.caption.empty())
            continue;
        ++captioned;
        if (sound.caption[0] == '@' && !shipped.texts.count(sound.caption.substr(1)))
            noCaption.push_back(entry.path().filename().string() + ": " + sound.caption);
    }
    // "@key" texts in the game data (dialogs, quests, items, weapons, levels) exist in cs.strings.
    std::vector<std::string> noDataKey;
    for (const char* folder : {"dialogs", "quests", "weapons", "levels"})
        for (const auto& entry : fs::directory_iterator(fs::path(ENGINE_TEST_ASSETS) / folder)) {
            const std::string ext = entry.path().extension().string();
            if (!entry.is_regular_file() || (ext != ".dialog" && ext != ".quest" && ext != ".weapon" && ext != ".level"))
                continue;
            std::ifstream in(entry.path());
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            for (std::size_t at = text.find("\"@"); at != std::string::npos; at = text.find("\"@", at + 1)) {
                const std::size_t end = text.find('"', at + 1);
                const std::string key = text.substr(at + 2, end - at - 2);
                if (!key.empty() && !shipped.texts.count(key))
                    noDataKey.push_back(entry.path().filename().string() + ": @" + key);
            }
        }
    {
        engine::ItemCatalog items;
        engine::loadItems(items, ENGINE_TEST_ASSETS "/items.cfg");
        for (const engine::ItemDef& d : items.items)
            if (!d.name.empty() && d.name[0] == '@' && !shipped.texts.count(d.name.substr(1)))
                noDataKey.push_back("items.cfg: " + d.name);
    }
    {
        // Every shipped level has an English title (level.<stem>.title; the level file's own title is Czech).
        engine::StringTable english;
        english.load(fs::path(ENGINE_TEST_ASSETS) / "text" / "en.strings");
        for (const auto& entry : fs::directory_iterator(fs::path(ENGINE_TEST_ASSETS) / "levels"))
            if (entry.path().extension() == ".level" && !english.texts.count("level." + entry.path().stem().string() + ".title"))
                noDataKey.push_back("en.strings: level." + entry.path().stem().string() + ".title");
    }
    check(noDataKey.empty(), "\"@key\" texts of the data exist in cs.strings" + (noDataKey.empty() ? std::string() : ": " + noDataKey.front()));
    check(captioned > 10 && noCaption.empty(),
          "sound captions exist in cs.strings" + (noCaption.empty() ? std::string() : ": " + noCaption.front()));
}

void testThirdPartyLicenses()
{
    namespace fs = std::filesystem;
    // Every library in 3rdparty/ is listed in its README table and ships its licence text.
    const fs::path root = fs::path(ENGINE_TEST_ASSETS).parent_path();
    auto readAll = [](const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    const std::string table = readAll(root / "3rdparty" / "README.md");
    const std::string cmake = readAll(root / "cmake" / "Licenses.cmake");
    check(!table.empty() && !cmake.empty(), "3rdparty/README.md and cmake/Licenses.cmake exist");
    int libraries = 0;
    for (const auto& entry : fs::directory_iterator(root / "3rdparty")) {
        if (!entry.is_directory())
            continue;
        ++libraries;
        const std::string name = entry.path().filename().string();
        check(table.find("| `" + name + "/` |") != std::string::npos, "3rdparty/README.md has a row for " + name);
        check(cmake.find("${T}/" + name + "/") != std::string::npos, "cmake/Licenses.cmake ships the licence of " + name);
    }
    check(libraries >= 10, "3rdparty libraries found");
}

void testAssetCredits()
{
    namespace fs = std::filesystem;
    using L = std::vector<std::string>;
    check(engine::licencesIn("| a.glb | x | y | CC0 1.0 |") == L{"CC0"}, "CC0 found");
    check(engine::licencesIn("licensed under CC-BY-4.0") == L{"CC-BY"}, "CC-BY is not share-alike");
    check(engine::licencesIn("CC BY-SA 3.0") == L{"CC-BY-SA"}, "CC-BY-SA found");
    check(engine::licencesIn("CC-BY-NC 4.0") == L{"NC"}, "NonCommercial found");
    check(engine::licencesIn("under the LGPL") == L{}, "LGPL is not GPL");
    check(engine::needsCredits("a/B.PNG") && engine::needsCredits("x.ogg") && !engine::needsCredits("x.sound") &&
              !engine::needsCredits("CREDITS.md"),
          "media files need credits");

    const fs::path root = fs::temp_directory_path() / "fps_engine_credits_test";
    fs::remove_all(root);
    fs::create_directories(root / "models" / "props" / "source");
    fs::create_directories(root / "sounds");
    auto write = [](const fs::path& p, const std::string& text) { std::ofstream(p, std::ios::binary) << text; };
    write(root / "models" / "CREDITS.md", "# Models\n| `crate.glb` | made here | us | CC0 |\n");
    write(root / "models" / "props" / "crate.glb", "x");
    write(root / "models" / "props" / "source" / "raw.png", "x"); // not shipped: no credits needed
    write(root / "sounds" / "bang.wav", "x");
    write(root / "sounds" / "CREDITS.md", "# Sounds\nThanks to a friend.\n");
    check(engine::creditsFileFor(root / "models" / "props" / "crate.glb", root) == root / "models" / "CREDITS.md",
          "credits come from a parent folder");
    auto issues = engine::checkAssetCredits(root);
    check(issues.size() == 1 && issues[0].file.filename() == "CREDITS.md" && issues[0].severity == engine::AssetIssue::Severity::Error,
          "a CREDITS.md without a licence is an error, covered and source files are fine");
    fs::remove(root / "sounds" / "CREDITS.md");
    issues = engine::checkAssetCredits(root);
    check(issues.size() == 1 && issues[0].file.filename() == "bang.wav", "a sound without credits is an error");
    write(root / "sounds" / "CREDITS.md", "# Sounds\n| `bang.wav` | x | y | CC BY-NC 3.0 |\n");
    issues = engine::checkAssetCredits(root);
    check(issues.size() == 1 && issues[0].message.find("NonCommercial") != std::string::npos, "NonCommercial is refused");
    fs::remove_all(root);

    // The shipped assets: everything credited, no unusable licence.
    const auto shipped = engine::checkAssetCredits(ENGINE_TEST_ASSETS);
    check(shipped.empty(), "assets have credits and usable licences" +
                               (shipped.empty() ? std::string() : ": " + shipped.front().file.string() + " " + shipped.front().message));
}

void testScriptDebugger()
{
    check(engine::ScriptDebugger::fileKey("@/home/x/assets/scripts/enemy.lua") == "scripts/enemy.lua" &&
              engine::ScriptDebugger::fileKey("C:\\game\\bin\\assets\\scripts\\dog.lua") == "scripts/dog.lua" &&
              engine::ScriptDebugger::fileKey("../scripts/crow.lua") == "scripts/crow.lua",
          "debugger: files are named below assets/");
    const auto dir = std::filesystem::temp_directory_path() / "dbg_test" / "assets" / "scripts";
    std::filesystem::create_directories(dir);
    {
        std::ofstream f(dir / "dbg.lua");
        f << "properties = { { \"speed\", 3, \"x\" } }\n"   // 1
             "local count = 0\n"                            // 2
             "local function bump(n)\n"                     // 3
             "    count = count + n\n"                      // 4
             "    return count\n"                           // 5
             "end\n"                                        // 6
             "function on_update(self, dt)\n"               // 7
             "    local before = count\n"                   // 8
             "    local after = bump(2)\n"                  // 9
             "    local done = after * props.speed\n"       // 10
             "end\n";                                       // 11
    }
    auto s = flatFloor();
    engine::Entity e;
    e.name = "Debugged";
    e.scriptPath = dir / "dbg.lua";
    s.entities.push_back(e);
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController player;
    player.spawn(world, {0, 0, 0});
    engine::ScriptSystem scripts(s, world, player, [](const std::string&) {});
    scripts.start();
    std::vector<engine::ScriptDebugPause> pauses;
    const engine::ScriptDebugAction plan[] = {engine::ScriptDebugAction::StepInto, engine::ScriptDebugAction::StepOut,
                                              engine::ScriptDebugAction::Continue};
    scripts.debugger().setPauseHandler([&](const engine::ScriptDebugPause& p) {
        pauses.push_back(p);
        return pauses.size() <= 3 ? plan[pauses.size() - 1] : engine::ScriptDebugAction::Continue;
    });
    scripts.debugger().setBreakpoint((dir / "dbg.lua").string(), 9, true);
    scripts.fixedUpdate(1.0f / 60.0f);
    auto var = [](const engine::ScriptDebugFrame& f, const std::string& name) {
        for (const engine::ScriptDebugVar& v : f.vars)
            if (v.name == name)
                return v.scope + ":" + v.value;
        return std::string("-");
    };
    check(pauses.size() == 3, "debugger: breakpoint, step into, step out (" + std::to_string(pauses.size()) + " pauses)");
    if (pauses.size() == 3) {
        const auto& a = pauses[0];
        check(a.reason == "breakpoint" && a.file == "scripts/dbg.lua" && a.line == 9 && a.entity == "Debugged" && a.frames[0].function == "on_update",
              "debugger: stops at the breakpoint in on_update");
        check(var(a.frames[0], "before") == "local:0" && var(a.frames[0], "dt").rfind("local:0.01", 0) == 0 &&
                  var(a.frames[0], "self").find("entity \"Debugged\"") != std::string::npos && var(a.frames[0], "count") == "upvalue:0" &&
                  var(a.frames[0], "speed") == "props:3",
              "debugger: locals, the entity, upvalues and props (self = " + var(a.frames[0], "self") + ")");
        const auto& b = pauses[1];
        check(b.reason == "step" && b.line == 4 && b.frames.size() > a.frames.size() && var(b.frames[0], "n") == "local:2" &&
                  b.frames[1].line == 9,
              "debugger: step into enters bump() with n = 2");
        const auto& c = pauses[2];
        check(c.line == 10 && var(c.frames[0], "after") == "local:2", "debugger: step out returns to the caller's next line");
        // The protocol to the editor carries all of it.
        engine::ScriptDebugPause back;
        bool done = false;
        for (const std::string& line : engine::ScriptDebugger::pauseLines(a))
            done = engine::ScriptDebugger::parsePauseLine(line, back);
        check(done && back.file == a.file && back.line == 9 && back.frames.size() == a.frames.size() &&
                  back.frames[0].vars.size() == a.frames[0].vars.size() && var(back.frames[0], "self") == var(a.frames[0], "self"),
              "debugger: pause lines round trip");
    }
    scripts.debugger().setBreakpoint("scripts/dbg.lua", 9, false);
    scripts.fixedUpdate(1.0f / 60.0f);
    check(pauses.size() == 3, "debugger: no stop without breakpoints");
    scripts.debugger().requestPause();
    scripts.fixedUpdate(1.0f / 60.0f);
    check(pauses.size() == 4 && pauses.back().reason == "pause" && pauses.back().line == 8, "debugger: pause stops at the next line");
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "dbg_test");
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
    scripts.damageEntity(0, 20.0f, -1); // the player shoots it
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
    check(engine::loadScene(level, ENGINE_TEST_DATA "/scene.level", &err), "load level: " + err);
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
    check(engine::loadScene(s, ENGINE_TEST_DATA "/scene.level", &err), "load level: " + err);
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
    check(props.size() == 36 && props[0].name == "health" && props[0].defaultValue == "100", "enemy.lua declares 36 tunables, health first");

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

void testEnemyAnimGraph()
{
    // The enemy script drives human.animgraph: run speed while chasing, aim/shoot on the upper
    // body, death on the base layer; the graph is saved with the level.
    auto s = flatFloor();
    s.playerStart = {0, 0, 0};
    engine::Entity e = enemyAt({26, 0, 0}, 180.0f);
    e.modelPath = ENGINE_TEST_ASSETS "/models/characters/humans/soldier.glb";
    e.model = engine::loadCharacterModel(e.modelPath);
    e.animation = "Idle_Loop";
    e.animGraphPath = std::filesystem::path(ENGINE_TEST_ASSETS "/animgraphs/human.animgraph").lexically_normal();
    e.animator.setGraph(engine::loadAnimGraph(e.animGraphPath));
    check(e.animator.graph != nullptr, "human.animgraph loads");
    s.entities.push_back(e);
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController player;
    player.spawn(world, s.playerStart);
    engine::ScriptSystem scripts(s, world, player, [](const std::string&) {});
    scripts.start();
    const engine::Skeleton& sk = *s.entities[0].model->skeleton;
    bool ran = false, aimed = false, shot = false, stepped = false;
    for (int i = 0; i < 300; ++i) {
        scripts.fixedUpdate(1.0f / 60.0f);
        engine::updateAnimations(s, 1.0f / 60.0f);
        const engine::Animator& a = s.entities[0].animator;
        ran = ran || (a.stateName() == "Locomotion" && a.param("speed") > 3.0f);
        aimed = aimed || a.stateName(1) == "Aim";
        shot = shot || a.stateName(1) == "Shoot";
        stepped = stepped || std::find(a.graphState.events.begin(), a.graphState.events.end(), "footstep") != a.graphState.events.end();
    }
    check(ran, "chasing enemy runs in the locomotion blend space");
    check(stepped, "running fires footstep events");
    check(aimed && shot, "attacking enemy aims and shoots on the upper body layer");
    for (int i = 0; i < 10; ++i)
        scripts.damageEntity(0, 30.0f);
    for (int i = 0; i < 200; ++i)
        engine::updateAnimations(s, 1.0f / 60.0f);
    check(s.entities[0].animator.stateName() == "Death" && s.entities[0].animator.finished(sk), "killed enemy plays the Death state to its end");

    std::string err;
    check(engine::saveScene(s, "graph.level", &err), "save level with an animation graph: " + err);
    engine::Scene loaded;
    check(engine::loadScene(loaded, "graph.level", &err) && loaded.entities.size() == 1 &&
              loaded.entities[0].animGraphPath == s.entities[0].animGraphPath && loaded.entities[0].animator.graph,
          "animation graph path is saved with the level");
    std::filesystem::remove("graph.level");
}

void testFactions()
{
    // The shipped table: loners neutral to the player, bandits hostile; goodwill moves it.
    engine::FactionTable t;
    std::string err;
    check(engine::loadFactions(t, ENGINE_TEST_ASSETS "/factions.cfg", &err), "load factions.cfg: " + err);
    check(t.factions.size() == 4 && t.find("military") && t.playerFaction == "stalker", "four factions, the player is a stalker");
    check(t.relation("bandit", "stalker") == -80.0f && t.relation("stalker", "bandit") == -80.0f, "relations are symmetric");
    check(t.attitude("player", "bandit") == engine::Attitude::Hostile && t.attitude("player", "stalker") == engine::Attitude::Neutral &&
              t.attitude("bandit", "bandit") == engine::Attitude::Friendly,
          "attitudes from the relations");
    t.changeGoodwill("stalker", -60.0f);
    check(t.attitude("player", "stalker") == engine::Attitude::Hostile && t.attitude("stalker", "stalker") == engine::Attitude::Neutral,
          "losing goodwill turns a faction against the player only");
    t.changeGoodwill("bandit", -500.0f);
    check(t.relation("player", "bandit") == -100.0f && t.goodwill["bandit"] == -20.0f, "goodwill is clamped");
    check(engine::saveFactions(t, "factions_test.cfg", &err), "save factions: " + err);
    engine::FactionTable u;
    check(engine::loadFactions(u, "factions_test.cfg", &err) && u.relations == t.relations && u.factions.size() == 4 &&
              u.factions[1].name == t.factions[1].name && u.hostileBelow == t.hostileBelow,
          "factions round trip: " + err);
    std::filesystem::remove("factions_test.cfg");

    // A bandit and a soldier see each other: they fight without the player.
    {
        auto s = flatFloor();
        s.playerStart = {0, 0, -60};
        s.entities.push_back(enemyAt({0, 0, 0}, 0.0f)); // bandit (type enemy), facing +x
        engine::Entity soldier = enemyAt({14, 0, 0}, 180.0f);
        soldier.name = "Soldier";
        soldier.properties["faction"] = "military";
        s.entities.push_back(soldier);
        const float playerHealth = runAi(s, 6.0f);
        check(playerHealth == 100.0f, "the player far away is left alone");
        check(s.entities[0].health < 100.0f || s.entities[1].health < 100.0f, "bandit and soldier shoot at each other");
    }

    // Neutral loners in camp ignore the player until one is shot; then its group fights.
    {
        auto s = flatFloor();
        s.playerStart = {0, 0, 0};
        for (int i = 0; i < 2; ++i) {
            engine::Entity e = enemyAt({8.0f + i * 1.5f, 0, 0}, i == 0 ? 180.0f : 0.0f); // the second one faces away
            e.name = i == 0 ? "Loner" : "Loner 2";
            e.type = "npc";
            e.properties["group"] = "camp";
            e.properties["patrol_radius"] = "0";
            s.entities.push_back(e);
        }
        engine::PhysicsWorld world;
        world.setLevel(s);
        engine::PlayerController player;
        player.spawn(world, s.playerStart);
        std::vector<std::string> log;
        engine::ScriptSystem scripts(s, world, player, [&log](const std::string& m) { log.push_back(m); });
        check(scripts.start() == 0, "npc scripts start");
        auto run = [&](float seconds) {
            engine::FpsCamera cam;
            engine::InputState idle;
            for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) {
                player.fixedUpdate(idle, cam, 1.0f / 60.0f);
                scripts.fixedUpdate(1.0f / 60.0f);
            }
        };
        run(3.0f);
        check(scripts.playerHealth() == 100.0f, "neutral loners do not attack the player");
        scripts.damageEntity(1, 10.0f, -1); // the player shoots the one looking away
        check(s.entities[1].properties["grudge"] == "1" && scripts.factions().goodwill["stalker"] < 0.0f,
              "the victim holds a grudge, the faction loses goodwill");
        run(4.0f);
        check(s.entities[0].properties["grudge"] == "1", "its group mate was called and turns on the player");
        check(scripts.playerHealth() < 100.0f, "the camp fights back");
        for (const std::string& m : log)
            check(m.find("[script]") != 0, "no script errors: " + m);
    }
}

void testALife()
{
    // Two points 300 m apart; a roaming bandit squad at the first, soldiers holding the second;
    // the player far away. The bandits go offline, travel, meet the soldiers and fight it out.
    auto s = flatFloor();
    s.playerStart = {-2000, 0, 0};
    for (float x : {0.0f, 300.0f}) {
        engine::Entity p;
        p.type = "alife_point";
        p.name = x == 0.0f ? "Barn" : "Checkpoint";
        p.position = {x, 0, 0};
        p.properties["capacity"] = "2"; // room for the soldiers holding it and a visiting squad
        s.entities.push_back(p);
    }
    for (int i = 0; i < 3; ++i) {
        engine::Entity b = enemyAt({1.0f + i, 0, 2}, 0.0f);
        b.name = "Bandit " + std::to_string(i);
        b.properties["group"] = "raiders";
        b.properties["alife"] = "roam";
        s.entities.push_back(b);
    }
    for (int i = 0; i < 2; ++i) {
        engine::Entity m = enemyAt({300.0f + i, 0, 2}, 180.0f);
        m.name = "Soldier " + std::to_string(i);
        m.properties["faction"] = "military";
        m.properties["group"] = "post";
        s.entities.push_back(m);
    }
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::PlayerController player;
    player.spawn(world, s.playerStart);
    std::vector<std::string> log;
    engine::ScriptSystem scripts(s, world, player, [&log](const std::string& m) { log.push_back(m); });
    check(scripts.start() == 0, "squad scripts start");
    engine::ALife alife;
    alife.settings.minWait = alife.settings.maxWait = 5.0f;
    alife.start(s);
    alife.settings.minWait = alife.settings.maxWait = 1000.0f; // once there, they stay
    check(alife.points().size() == 2 && alife.squads().size() == 2, "two points, two squads (groups)");
    check(alife.squads()[0].roams && !alife.squads()[1].roams && alife.squads()[0].target == 0 && alife.squads()[1].target == 1,
          "the raiders roam, both squads start at their nearest point");
    int gunfire = 0;
    auto run = [&](float seconds, glm::vec3 at) {
        for (int i = 0; i < static_cast<int>(seconds * 10.0f); ++i) {
            scripts.fixedUpdate(0.1f);
            alife.update(s, scripts, scripts.factions(), at, 0.1f);
            for (const auto& ev : alife.takeEvents())
                gunfire += ev.kind == engine::ALife::Event::Gunfire;
        }
    };
    run(2.0f, s.playerStart);
    check(s.entities[2].offline && s.entities[5].offline && !alife.squads()[0].online, "far squads go offline (hidden)");
    run(30.0f, s.playerStart);
    const auto& raiders = alife.squads()[0];
    check(!raiders.arrived && raiders.target == 1 && raiders.position.x > 20.0f, "the raiders travel to the other point offline");
    run(260.0f, s.playerStart);
    int dead = 0;
    for (int i = 2; i < 7; ++i)
        dead += !s.entities[static_cast<std::size_t>(i)].alive;
    check(gunfire > 0 && dead > 0, "hostile squads that meet offline fight: " + std::to_string(dead) + " dead");
    // The player comes: the squads there go online where they got to.
    run(2.0f, {300, 0, 0});
    check(!s.entities[5].offline && alife.squads()[1].online, "squads near the player come back online");
    bool placed = true;
    for (int i = 2; i < 5; ++i)
        if (s.entities[static_cast<std::size_t>(i)].alive)
            placed = placed && s.entities[static_cast<std::size_t>(i)].position.x > 250.0f;
    check(placed, "the raiders stand where their record travelled");
    check(alife.squads()[0].online || alife.squads()[0].living == 0, "the raiders came online too");
    const auto saved = alife.save();
    check(saved.size() == 1 && saved[0].name == "raiders", "roaming squads are saved");
    for (const std::string& m : log)
        check(m.find("[script]") != 0, "no script errors: " + m);
}

void testTradeAndWear()
{
    engine::ItemCatalog items;
    std::string error;
    check(engine::loadItems(items, ENGINE_TEST_ASSETS "/items.cfg", &error), "items.cfg loads: " + error);
    check(items.find(engine::ItemDef::Kind::Medkit) && items.find(engine::ItemDef::Kind::Ammo, "5.45x39") &&
              items.find(engine::ItemDef::Kind::Armor, "jacket") && !items.find(engine::ItemDef::Kind::Armor, "nope"),
          "catalog: medkit, ammo by type, armor by id");
    {
        const auto file = std::filesystem::temp_directory_path() / "rt_items.cfg";
        engine::ItemCatalog back;
        check(engine::saveItems(items, file) && engine::loadItems(back, file) && back == items, "items.cfg round trip");
        std::filesystem::remove(file);
    }
    check(engine::buyPrice(1000, 1.3f) == 1300 && engine::buyPrice(1000, 1.3f, 0.5f) < 1300, "buy price: markup, worn is cheaper");
    check(engine::sellPrice(1000, 0.45f) == 450 && engine::sellPrice(1000, 0.45f, 0.5f) < 450, "sell price: rate, worn is cheaper");
    check(engine::repairPrice(1000, 1.0f) == 0 && engine::repairPrice(1000, 0.6f) > 0 && engine::repairPrice(1000, 0.2f) > engine::repairPrice(1000, 0.6f),
          "repair costs more the more worn");
    const engine::ItemDef& jacket = *items.find(engine::ItemDef::Kind::Armor, "jacket");
    float cond = 1.0f;
    const float taken = engine::armorAbsorb(jacket, cond, 50.0f);
    check(taken < 50.0f && taken > 30.0f && cond < 1.0f, "armor stops part of a hit and wears");
    float wrecked = 0.0f;
    check(std::abs(engine::armorAbsorb(jacket, wrecked, 50.0f) - 50.0f) < 1e-3f, "wrecked armor stops nothing");
    check(engine::loadSpeedFactor(items, 10.0f) == 1.0f && engine::loadSpeedFactor(items, items.carryMax + 5.0f) < 0.5f &&
              engine::loadAllowsSprint(items, 10.0f) && !engine::loadAllowsSprint(items, items.carryFree + 1.0f),
          "load: full speed when light, slow and no sprint when overloaded");
    engine::WeaponSettings ak;
    check(engine::loadWeaponFile(ak, ENGINE_TEST_ASSETS "/weapons/ak74.weapon") && ak.price > 0 && ak.weight > 1.0f && ak.wearPerShot > 0.0f,
          "ak74: price, weight and wear");
    check(engine::jamProbability(ak, 1.0f) == 0.0f && engine::jamProbability(ak, 0.1f) > engine::jamProbability(ak, 0.4f) &&
              engine::jamProbability(ak, 0.4f) > 0.0f,
          "a new weapon never jams, worn ones more the worse");
    check(engine::wornSpreadScale(1.0f) == 1.0f && engine::wornSpreadScale(0.0f) > 2.0f, "worn weapons spread more");
    // Inheritance: the old AK-74 is the AK-74 with a few values of its own.
    {
        engine::WeaponSettings old;
        std::string werr;
        check(engine::loadWeaponFile(old, ENGINE_TEST_ASSETS "/weapons/ak74_old.weapon", &werr), "ak74_old loads: " + werr);
        check(old.base == "ak74.weapon" && old.damage == ak.damage && old.magazineSize == ak.magazineSize && old.modelPath == ak.modelPath &&
                  old.spreadDegrees > ak.spreadDegrees && old.price < ak.price && old.name != ak.name,
              "a weapon inherits from its base and overrides some values");
        const auto over = engine::weaponOverrides(old, ENGINE_TEST_ASSETS "/weapons/ak74_old.weapon");
        check(over == std::set<std::string>{"name", "spread", "price", "wear"}, "the overridden keys are known");
        const auto dir = std::filesystem::temp_directory_path() / "weapon_base_test";
        std::filesystem::create_directories(dir);
        engine::WeaponSettings base = ak;
        base.base.clear();
        check(engine::saveWeaponFile(base, dir / "base.weapon"), "base saved");
        engine::WeaponSettings child = base;
        child.base = "base.weapon";
        child.damage = base.damage + 5.0f;
        check(engine::saveWeaponFile(child, dir / "child.weapon"), "variant saved");
        int lines = 0;
        {
            std::ifstream in(dir / "child.weapon");
            for (std::string l; std::getline(in, l);)
                lines += !l.empty() && l[0] != '#';
        }
        check(lines == 3, "the variant's file has version, base and its one difference (" + std::to_string(lines) + " lines)");
        base.magazineSize = 45;
        engine::saveWeaponFile(base, dir / "base.weapon");
        engine::WeaponSettings back;
        check(engine::loadWeaponFile(back, dir / "child.weapon") && back.magazineSize == 45 && back.damage == child.damage,
              "a change to the base shows in the variant, its own values stay");
        {
            std::ofstream(dir / "loop.weapon") << "version 2\nbase \"loop.weapon\"\n";
        }
        engine::WeaponSettings loop;
        check(!engine::loadWeaponFile(loop, dir / "loop.weapon"), "a base loop is an error, not a hang");
        std::filesystem::remove_all(dir);
    }
    // Artifacts: every anomaly kind that grows one has a kind, effects add up on the belt only.
    for (const char* kind : {"crusher", "discharge", "vent", "acid", "rift"}) {
        const auto kinds = engine::artifactsFor(items, kind);
        check(!kinds.empty(), std::string("an artifact grows in ") + kind);
        for (const engine::ItemDef* d : kinds)
            check(std::filesystem::exists(std::filesystem::path(ENGINE_TEST_ASSETS) / d->model), "artifact model exists: " + d->model);
    }
    check(engine::artifactsFor(items, "radiation").empty(), "nothing grows in radiation");
    const engine::ItemDef* spring = items.find(engine::ItemDef::Kind::Artifact, "spring");
    const engine::ItemDef* knot = items.find(engine::ItemDef::Kind::Artifact, "knot");
    check(spring && knot && spring->heal > 0.0f && knot->protection > 0.0f, "spring heals, knot protects");
    {
        engine::ItemCatalog belt = items;
        belt.beltSlots = 2;
        const auto e = engine::beltEffects(belt, {"knot", "knot", "spring"}); // the spring is in the backpack
        check(std::abs(e.armor - (1.0f - (1.0f - knot->protection) * (1.0f - knot->protection))) < 1e-4f && e.heal == 0.0f &&
                  std::abs(e.carry - 2.0f * knot->carry) < 1e-4f,
              "belt: two knots stack their protection, the third artifact does nothing");
        check(engine::beltEffects(belt, {"nope"}).armor == 0.0f, "unknown artifacts do nothing");
    }
    {
        engine::Entity e;
        engine::initAnomalyEntity(e, engine::AnomalyKind::Crusher);
        const engine::AnomalySettings st = engine::anomalySettings(e);
        check(st.get("artifacts") >= 1.0f && st.get("artifact_chance") > 0.0f, "anomalies grow one artifact by default");
    }
}

void testQuestsAndDialogs()
{
    engine::Quests quests;
    std::vector<std::string> errors;
    check(quests.loadFolder(ENGINE_TEST_ASSETS "/quests", &errors) >= 1 && errors.empty(), "the shipped quests load");
    const engine::QuestDef* stash = quests.find("camp_stash");
    check(stash && stash->stages.size() == 2 && stash->stage("report") && stash->rewards.size() == 4, "camp_stash: two stages, four rewards");
    for (const char* name : {"grisha", "tolik"}) {
        engine::DialogDef d;
        std::string error;
        check(engine::loadDialog(d, std::string(ENGINE_TEST_ASSETS "/dialogs/") + name + ".dialog", &error), "dialog loads: " + error);
        const auto problems = engine::checkDialog(d, &quests);
        check(problems.empty(), std::string(name) + ".dialog has no broken links" + (problems.empty() ? "" : ": " + problems[0]));
    }
    engine::DialogDef grisha;
    engine::loadDialog(grisha, ENGINE_TEST_ASSETS "/dialogs/grisha.dialog");
    // Round trips of both formats.
    {
        const auto dir = std::filesystem::temp_directory_path();
        engine::DialogDef d2;
        engine::QuestDef q2;
        check(engine::saveDialog(grisha, dir / "rt.dialog") && engine::loadDialog(d2, dir / "rt.dialog") && d2 == grisha, "dialog save/load round trip");
        check(engine::saveQuest(*stash, dir / "rt.quest") && engine::loadQuest(q2, dir / "rt.quest") && q2 == *stash, "quest save/load round trip");
        std::filesystem::remove(dir / "rt.dialog");
        std::filesystem::remove(dir / "rt.quest");
    }
    // The talk: greeting, job offered, quest started; then the waiting line; then the reward.
    engine::DialogSession talk;
    auto choose = [&](const std::string& text) {
        for (std::size_t i = 0; i < talk.choices().size(); ++i)
            if (talk.node()->choices[static_cast<std::size_t>(talk.choices()[i])].text.starts_with(text))
                return talk.choose(static_cast<int>(i), quests);
        check(false, "answer \"" + text + "\" offered in node " + (talk.node() ? talk.node()->id : std::string("-")));
        return std::vector<engine::DialogAction>{};
    };
    check(talk.start(grisha, quests) && talk.node()->id == "first", "first meeting: the greeting");
    choose("@dialog.grisha.first.1"); // "Co se tu dá dělat?" (texts are keys of assets/text)
    check(talk.node() && talk.node()->id == "work" && quests.flag("met_grisha"), "asks for work; the meeting is remembered");
    choose("@dialog.grisha.work.1");
    check(!talk.active() && quests.status("camp_stash") == engine::Quests::Status::Active && quests.stage("camp_stash") == "find",
          "the quest starts at its first stage");
    auto events = quests.takeEvents();
    check(events.size() == 1 && events[0].kind == engine::Quests::Event::Started, "one event: started");
    check(talk.start(grisha, quests) && talk.node()->id == "waiting", "while searching he repeats where to go");
    talk.end();
    check(quests.advance("camp_stash") && quests.stage("camp_stash") == "report", "the stash found: report back");
    check(talk.start(grisha, quests) && talk.node()->id == "found", "back at the camp: the reward line");
    choose("@dialog.grisha.found.1");
    check(quests.status("camp_stash") == engine::Quests::Status::Completed && quests.is("camp_stash", "done"), "completed");
    check(talk.start(grisha, quests) && talk.node()->id == "start", "later: the ordinary greeting");
    bool offersWork = false;
    for (int i : talk.choices())
        offersWork = offersWork || talk.node()->choices[static_cast<std::size_t>(i)].text == "@dialog.grisha.start.1"; // "Máš pro mě nějakou práci?"
    check(!offersWork, "no job offered again once it is done");
    // Tolik's radio: offered from the radio talk, the waiting line, the reward, not offered again.
    {
        engine::DialogDef tolik;
        engine::loadDialog(tolik, ENGINE_TEST_ASSETS "/dialogs/tolik.dialog");
        const engine::QuestDef* radio = quests.find("tolik_radio");
        check(radio && radio->stages.size() == 2 && radio->stage("find") && radio->stage("find")->target == "Locomotive radio",
              "tolik_radio: find the locomotive radio, then report");
        check(talk.start(tolik, quests) && talk.node()->id == "start", "Tolik: the greeting");
        choose("@dialog.tolik.start.1");
        choose("@dialog.tolik.radio.3");
        check(talk.node() && talk.node()->id == "job", "asking about the weak radio offers the job");
        choose("@dialog.tolik.job.1");
        check(quests.status("tolik_radio") == engine::Quests::Status::Active && quests.stage("tolik_radio") == "find", "the radio quest starts");
        check(talk.start(tolik, quests) && talk.node()->id == "waiting", "Tolik says where the locomotive is");
        talk.end();
        quests.advance("tolik_radio");
        check(talk.start(tolik, quests) && talk.node()->id == "fixed", "the amplifier brought back: the reward line");
        choose("@dialog.tolik.fixed.1");
        check(quests.is("tolik_radio", "done"), "the radio quest is done");
        check(talk.start(tolik, quests) && talk.node()->id == "start", "later: the ordinary greeting");
        choose("@dialog.tolik.start.1");
        bool offered = false;
        for (int i : talk.choices())
            offered = offered || talk.node()->choices[static_cast<std::size_t>(i)].text == "@dialog.tolik.radio.3";
        check(!offered, "the radio job is not offered again");
        talk.end();
        quests.takeEvents();
    }
    // Saved games.
    const std::vector<std::string> saved = quests.save();
    engine::Quests back;
    back.loadFolder(ENGINE_TEST_ASSETS "/quests");
    back.restore(saved);
    check(back.is("camp_stash", "done") && back.flag("met_grisha") && back.save() == saved, "quest log and flags survive a save");
    // Game actions come back to the caller; quest ones are done.
    {
        engine::Quests q;
        q.loadFolder(ENGINE_TEST_ASSETS "/quests");
        const auto game = engine::runDialogActions({{{"give", "medkits", "2"}}, {{"quest_stage", "camp_stash", "report"}}, {{"fire", "Gate", "Open"}}}, q);
        check(game.size() == 2 && game[0].words[0] == "give" && game[1].words[0] == "fire" && q.stage("camp_stash") == "report",
              "give/fire go to the game, quest_stage jumps (and starts the quest)");
    }
    // A quest objective in a level: counts only at its stage, then moves the quest on.
    {
        auto s = flatFloor();
        s.playerStart = {0, 0, 0};
        engine::Entity stashEntity;
        stashEntity.type = "objective";
        stashEntity.name = "Hospital stash";
        stashEntity.position = {0.5f, 0, 0};
        stashEntity.scriptPath = ENGINE_TEST_ASSETS "/scripts/objective.lua";
        stashEntity.properties["quest"] = "camp_stash";
        stashEntity.properties["stage"] = "find";
        s.entities.push_back(stashEntity);
        engine::PhysicsWorld world;
        world.setLevel(s);
        engine::PlayerController player;
        player.spawn(world, s.playerStart);
        engine::Quests q;
        q.loadFolder(ENGINE_TEST_ASSETS "/quests");
        std::vector<std::string> log;
        engine::ScriptSystem scripts(s, world, player, [&log](const std::string& m) { log.push_back(m); });
        scripts.setQuests(&q);
        check(scripts.start() == 0, "objective script starts");
        for (int i = 0; i < 30; ++i)
            scripts.fixedUpdate(1.0f / 60.0f);
        check(q.status("camp_stash") == engine::Quests::Status::None, "the stash means nothing before the quest is taken");
        q.start("camp_stash");
        for (int i = 0; i < 30; ++i)
            scripts.fixedUpdate(1.0f / 60.0f);
        check(q.stage("camp_stash") == "report", "standing at the stash moves the quest to its report stage");
        for (const std::string& m : log)
            check(m.find("[script]") != 0, "no script errors: " + m);
    }
    // The fixed radio (storm_radio.lua): warns once per storm, only after its quest is done.
    {
        auto s = flatFloor();
        engine::Entity radio;
        radio.type = "radio";
        radio.name = "Radio";
        radio.position = {3, 0, 0};
        radio.scriptPath = ENGINE_TEST_ASSETS "/scripts/storm_radio.lua";
        radio.properties["quest"] = "tolik_radio";
        radio.properties["lead"] = "120";
        radio.properties["message"] = "storm soon";
        radio.properties["sound"] = "";
        s.entities.push_back(radio);
        engine::PhysicsWorld world;
        world.setLevel(s);
        engine::PlayerController player;
        player.spawn(world, s.playerStart);
        engine::Quests q;
        q.loadFolder(ENGINE_TEST_ASSETS "/quests");
        std::vector<std::string> shown;
        engine::ScriptSystem scripts(s, world, player, [&shown](const std::string& m) { shown.push_back(m); });
        scripts.setMessageHandler([&shown](const std::string& m) { shown.push_back(m); }); // game.message()
        scripts.setQuests(&q);
        check(scripts.start() == 0, "storm radio script starts");
        engine::AnomalyStormNow& now = s.environment.anomalyStormNow;
        auto run = [&](const char* phase, float left) {
            now.phase = phase;
            now.secondsLeft = left;
            for (int i = 0; i < 5; ++i)
                scripts.fixedUpdate(1.0f / 60.0f);
        };
        auto said = [&] { return std::count(shown.begin(), shown.end(), std::string("storm soon")); };
        run("calm", 60.0f);
        check(said() == 0, "the broken radio says nothing");
        q.start("tolik_radio");
        q.complete("tolik_radio");
        run("calm", 300.0f);
        check(said() == 0, "fixed radio: quiet while the storm is far");
        run("calm", 100.0f);
        run("calm", 90.0f);
        for (const std::string& m : shown)
            check(m.find("[script]") != 0, "storm radio: no script errors: " + m);
        check(said() == 1, "fixed radio: one warning within the lead time");
        run("warning", 50.0f);
        run("storm", 80.0f);
        run("calm", 110.0f);
        check(said() == 2, "and again before the next storm");
    }
}

void testAnomalyStorm()
{
    // Level setting round trip.
    {
        engine::Scene scene = flatFloor();
        engine::AnomalyStormSettings& a = scene.environment.anomalyStorm;
        a.enabled = true;
        a.minMinutes = 5.0f;
        a.maxMinutes = 9.0f;
        a.warningSeconds = 45.0f;
        a.durationSeconds = 70.0f;
        a.damage = 4.0f;
        a.npcDamage = 12.0f;
        a.tint = {0.9f, 0.5f, 0.3f};
        const auto file = std::filesystem::temp_directory_path() / "storm_roundtrip.level";
        engine::Scene back;
        check(engine::saveScene(scene, file) && engine::loadScene(back, file) && back.environment.anomalyStorm == a,
              "anomaly storm settings survive save and load");
        std::filesystem::remove(file);
    }
    // Phases: calm -> warning -> storm -> fading -> calm, with the look and danger ramps.
    {
        engine::AnomalyStormSettings s;
        s.enabled = true;
        s.minMinutes = s.maxMinutes = 0.05f; // 3 s
        s.warningSeconds = 2.0f;
        s.durationSeconds = 3.0f;
        engine::AnomalyStorm storm(7);
        storm.start(s);
        using P = engine::AnomalyStorm::Phase;
        auto run = [&](float seconds) {
            int changes = 0;
            for (int i = 0; i < static_cast<int>(seconds * 60.0f + 0.5f); ++i)
                changes += storm.update(s, 1.0f / 60.0f);
            return changes;
        };
        run(2.8f);
        check(storm.phase() == P::Calm && storm.look() == 0.0f, "calm until the time comes");
        check(run(0.4f) == 1 && storm.phase() == P::Warning && storm.look() > 0.0f && storm.danger() == 0.0f,
              "the warning comes: the sky turns, nothing hurts yet");
        run(2.0f);
        check(storm.phase() == P::Storm, "then the storm itself");
        run(1.0f);
        check(storm.danger() == 1.0f && storm.look() == 1.0f, "full danger in the storm");
        run(2.2f);
        check(storm.phase() == P::Fading && storm.danger() == 0.0f, "it fades and stops hurting");
        run(26.0f);
        check(storm.phase() == P::Calm && storm.look() == 0.0f && storm.secondsLeft() > 0.0f, "calm again, the next one scheduled");
        s.enabled = false;
        run(10.0f);
        check(storm.phase() == P::Calm, "storms switched off: none comes");
        storm.trigger(s, 1.0f);
        run(1.1f);
        check(storm.phase() == P::Storm, "a forced storm (console, script) runs even when switched off");
        storm.restore(1, 30.0f);
        check(storm.phase() == P::Warning && storm.secondsLeft() == 30.0f, "saved games restore the phase");
        // The sky: rust coloured, lightning, wind.
        engine::Environment env;
        env.anomalyStorm.tint = {1.0f, 0.5f, 0.4f};
        const engine::Environment calm = engine::environmentAt(env, {0, -1, 0});
        env.anomalyStormNow.look = 1.0f;
        const engine::Environment red = engine::environmentAt(env, {0, -1, 0});
        check(red.tint.b < calm.tint.b * 0.6f && red.storm >= 0.99f && red.wind >= 0.99f && red.cloudCover > 0.99f &&
                  red.fogColor.r > red.fogColor.b,
              "the storm colours the picture and sky, brings wind and lightning");
    }
    // NPCs run under a roof when warned and go home when it is over.
    {
        engine::Scene s = flatFloor();
        s.boxes.push_back({"Roof", {16.0f, 3.2f, 0.0f}, {8.0f, 0.4f, 8.0f}, {1, 1, 1}}); // a canopy on its own, open sides
        s.playerStart = {-30, 0, -30};
        engine::Entity npc;
        npc.type = "npc";
        npc.name = "Stalker";
        npc.position = {0, 0, 0};
        npc.size = {0.6f, 1.8f, 0.6f};
        npc.scriptPath = ENGINE_TEST_ASSETS "/scripts/enemy.lua";
        npc.properties["faction"] = "stalker";
        npc.properties["patrol_radius"] = "0";
        npc.properties["rest_chance"] = "0";
        s.entities.push_back(npc);
        engine::NavMesh nav;
        check(nav.build(s, s.nav) && nav.ready(), "navmesh for the storm test");
        engine::PhysicsWorld world;
        world.setLevel(s);
        engine::PlayerController player;
        player.spawn(world, s.playerStart);
        std::vector<std::string> log;
        engine::ScriptSystem scripts(s, world, player, [&log](const std::string& m) { log.push_back(m); });
        scripts.setNavMesh(&nav);
        check(scripts.start() == 0, "storm test script starts");
        engine::FpsCamera cam;
        engine::InputState idle;
        auto run = [&](float seconds) {
            for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) {
                player.fixedUpdate(idle, cam, 1.0f / 60.0f);
                scripts.fixedUpdate(1.0f / 60.0f);
            }
        };
        const glm::vec3 head(0.0f, 1.9f, 0.0f);
        check(!engine::sheltered(world, {}, s.entities[0].position + head) && engine::sheltered(world, {}, glm::vec3(16, 0, 0) + head),
              "out in the open vs. under the canopy");
        run(2.0f);
        s.environment.anomalyStormNow.phase = "warning";
        run(15.0f);
        check(engine::sheltered(world, {}, s.entities[0].position + head),
              "warned, the stalker runs under the roof: " + std::to_string(s.entities[0].position.x) + " " +
                  std::to_string(s.entities[0].position.z));
        s.environment.anomalyStormNow.phase = "calm";
        run(20.0f);
        check(glm::length(glm::vec2(s.entities[0].position.x, s.entities[0].position.z)) < 1.5f, "after the storm it goes home");
        for (const std::string& m : log)
            check(m.find("[script]") != 0, "no script errors: " + m);
    }
}

void testFauna()
{
    // A crow flock entity becomes its crows; they take off when the player comes close.
    {
        auto s = flatFloor();
        s.playerStart = {-40, 0, 0};
        engine::Entity flock;
        flock.type = "crow_flock";
        flock.name = "Crows";
        flock.id = 0x1234;
        flock.size = {0.35f, 0.3f, 0.35f};
        flock.scriptPath = ENGINE_TEST_ASSETS "/scripts/crow.lua";
        flock.properties["count"] = "5";
        s.entities.push_back(flock);
        check(engine::expandFlocks(s) == 5 && s.entities.size() == 6 && s.entities[0].scriptPath.empty() &&
                  s.entities[3].type == "crow" && s.entities[3].properties["flock"] == "Crows" && s.entities[3].id != 0 &&
                  s.entities[3].id != s.entities[4].id,
              "a flock of 5 becomes 5 crows with their own ids");
        engine::PhysicsWorld world;
        world.setLevel(s);
        engine::PlayerController player;
        player.spawn(world, s.playerStart);
        std::vector<std::string> log;
        engine::ScriptSystem scripts(s, world, player, [&log](const std::string& m) { log.push_back(m); });
        check(scripts.start() == 0, "crow scripts start");
        engine::FpsCamera cam;
        engine::InputState idle;
        auto run = [&](float seconds) {
            for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) {
                player.fixedUpdate(idle, cam, 1.0f / 60.0f);
                scripts.fixedUpdate(1.0f / 60.0f);
            }
        };
        auto airborne = [&] {
            int n = 0;
            for (std::size_t i = 1; i < s.entities.size(); ++i)
                n += s.entities[i].position.y > 2.0f;
            return n;
        };
        run(3.0f);
        check(airborne() == 0, "crows stay on the ground while nobody is near");
        scripts.makeNoise({-30, 1.7f, 0}, 80.0f); // a shot 30 m away
        run(3.0f);
        check(airborne() == 5, "a shot sends the whole flock up: " + std::to_string(airborne()));
        float spread = 0.0f;
        for (std::size_t i = 1; i < s.entities.size(); ++i)
            spread = std::max(spread, glm::distance(s.entities[i].position, s.entities[1].position));
        check(spread < 40.0f, "the flock keeps together in the air");
        run(60.0f);
        check(airborne() == 0, "the crows land again when it is quiet");
        for (const std::string& m : log)
            check(m.find("[script]") != 0, "no script errors: " + m);
    }

    // A pack of two dogs smells the player, hunts and bites; a badly hurt dog runs home.
    {
        auto s = flatFloor();
        s.playerStart = {0, 0, 0};
        for (int i = 0; i < 2; ++i) {
            engine::Entity dog;
            dog.type = "mutant";
            dog.name = "Dog " + std::to_string(i);
            dog.position = {14.0f, 0, i * 2.0f};
            dog.yawDegrees = 0.0f; // facing away: it has to smell or hear first
            dog.size = {0.5f, 0.75f, 0.5f};
            dog.scriptPath = ENGINE_TEST_ASSETS "/scripts/dog.lua";
            dog.properties["group"] = "pack";
            s.entities.push_back(dog);
        }
        s.entities[1].properties["smell_range"] = "15"; // only this one notices; it calls the other
        engine::PhysicsWorld world;
        world.setLevel(s);
        engine::PlayerController player;
        player.spawn(world, s.playerStart);
        std::vector<std::string> log;
        engine::ScriptSystem scripts(s, world, player, [&log](const std::string& m) { log.push_back(m); });
        check(scripts.start() == 0, "dog scripts start");
        engine::FpsCamera cam;
        engine::InputState idle;
        float closest = 1e9f;
        for (int i = 0; i < 6 * 60; ++i) {
            player.fixedUpdate(idle, cam, 1.0f / 60.0f);
            scripts.fixedUpdate(1.0f / 60.0f);
            closest = std::min(closest, glm::length(glm::vec2(s.entities[0].position.x, s.entities[0].position.z)));
        }
        check(closest < 2.0f, "the pack mate was called and came for the player");
        check(scripts.playerHealth() < 100.0f, "the dogs bite");
        scripts.damageEntity(0, 45.0f, -1);
        const glm::vec3 hurt = s.entities[0].position;
        for (int i = 0; i < 3 * 60; ++i) {
            player.fixedUpdate(idle, cam, 1.0f / 60.0f);
            scripts.fixedUpdate(1.0f / 60.0f);
        }
        check(glm::length(glm::vec2(s.entities[0].position.x, s.entities[0].position.z)) > glm::length(glm::vec2(hurt.x, hurt.z)) + 3.0f,
              "a badly hurt dog runs back to its lair");
        for (const std::string& m : log)
            check(m.find("[script]") != 0, "no script errors: " + m);
    }
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
    check(engine::loadScene(s, ENGINE_TEST_DATA "/scene.level", &err), "load scene.level: " + err);
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
        tree.sway = 0.0f; // rigid (a ramp): collides with its mesh
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
    check(engine::loadScene(s, ENGINE_TEST_DATA "/scene.level", &err), "load scene.level: " + err);
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
    // Tile cache: the same level again loads every tile; a moved wall rebuilds only its tiles.
    {
        check(nav.cachedTileCount() == 0, "a first build has no cached tiles");
        engine::NavMesh again;
        check(again.build(scene, scene.nav, &error) && again.cachedTileCount() == again.tileCount() &&
                  again.polygonCount() == nav.polygonCount() && again.findPath({-10.0f, 0.0f, -10.0f}, {-10.0f, 0.0f, 10.0f}).size() == path.size(),
              "an unchanged level loads its navmesh tiles from the cache");
        scene.nav.tileSize = 8.0f;
        engine::NavMesh small;
        check(small.build(scene, scene.nav, &error) && small.cachedTileCount() == 0, "other settings do not use cached tiles");
        engine::Scene moved = scene;
        moved.boxes[1].position.z = 0.6f;
        moved.boxes[1].size.x = 10.0f; // the wall now ends at x = 2
        moved.boxes[1].position.x = -3.0f;
        engine::NavMesh changed;
        check(changed.build(moved, moved.nav, &error) && changed.cachedTileCount() > 0 && changed.cachedTileCount() < changed.tileCount(),
              "a changed wall rebuilds only the tiles it touches");
        scene.nav.tileSize = engine::NavSettings{}.tileSize;
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
    copy.caption = "@caption.test \"x\"";
    engine::SoundDef back;
    check(engine::saveSoundFile(copy, tmp) && engine::loadSoundFile(back, tmp) && back.files == copy.files &&
              back.pitchMin == 0.8f && back.maxDistance == 123.0f && back.group == engine::AudioGroup::Music &&
              back.caption == copy.caption,
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
    // The 0.8 s shot ends once mixed; it decodes on a worker first, which can take a while
    // on a busy machine, so mix until it is done (at most ~10 s of waiting).
    for (int i = 0; i < 1000 && audio.playing(h); ++i) {
        audio.update(0.1f);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
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
    if (!engine::loadScene(s, ENGINE_TEST_DATA "/physics_scene.level", &err)) {
        check(false, "load physics_scene.level: " + err); // the rest would index missing objects
        return;
    }
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

void testHotReload()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::absolute("hot_reload_assets");
    fs::remove_all(dir);
    fs::create_directories(dir);
    // The watcher sees a written file.
    {
        std::ofstream(dir / "a.material") << "color 1 0 0\n";
        engine::FileWatcher watcher(dir, 0.05);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        check(watcher.changes().empty(), "nothing changed yet");
        std::this_thread::sleep_for(std::chrono::milliseconds(1100)); // coarse file times on some file systems
        std::ofstream(dir / "a.material") << "color 0 1 0\n";
        std::ofstream(dir / "notes.txt") << "not an asset";
        std::vector<fs::path> changed;
        for (int i = 0; i < 40 && changed.empty(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            changed = watcher.changes();
        }
        check(changed.size() == 1 && changed[0].filename() == "a.material", "watcher reports the changed asset only");
    }
    // Material in use: reloaded in place.
    std::ofstream(dir / "b.material") << "color 1 0 0\n";
    auto mat = engine::loadMaterial(dir / "b.material");
    std::ofstream(dir / "b.material") << "color 0 0 1\n";
    auto lines = engine::hotReload({dir / "b.material", dir / "unused.material"}, {});
    check(mat && mat->color == glm::vec3(0, 0, 1) && lines.size() == 1, "material reloaded in place, unused files skipped");
    // Particle effect in use.
    {
        engine::ParticleEffect fx;
        fx.emitters.emplace_back();
        fx.emitters[0].burst = 3;
        std::string err;
        check(engine::saveParticleFile(fx, dir / "fx.particle", &err), "save fx: " + err);
        auto used = engine::loadParticleEffect(dir / "fx.particle");
        fx.emitters[0].burst = 9;
        fx.emitters.emplace_back();
        check(engine::saveParticleFile(fx, dir / "fx.particle", &err), "save fx again: " + err);
        engine::hotReload({dir / "fx.particle"}, {});
        check(used && used->emitters.size() == 2 && used->emitters[0].burst == 9, "particle effect reloaded in place");
    }
    // Lua: new functions, state kept, on_start not repeated.
    {
        const fs::path script = dir / "counter.lua";
        std::ofstream(script) << "starts = (starts or 0) + 1\nfunction on_start(self) self.health = 0 end\n"
                                 "function on_update(self, dt) self.health = self.health + 1 end\n";
        auto s = flatFloor();
        engine::Entity e;
        e.name = "Counter";
        e.scriptPath = script;
        s.entities.push_back(e);
        engine::PhysicsWorld world;
        world.setLevel(s);
        engine::PlayerController player;
        player.spawn(world, {0, 0, 0});
        engine::ScriptSystem scripts(s, world, player, [](const std::string&) {});
        scripts.start();
        scripts.fixedUpdate(1.0f / 60.0f);
        std::ofstream(script) << "starts = (starts or 0) + 1\nfunction on_start(self) self.health = 0 end\n"
                                 "function on_update(self, dt) self.health = self.health + 100 end\n";
        lines = engine::hotReload({script}, {&s, &world, nullptr, &scripts});
        scripts.fixedUpdate(1.0f / 60.0f);
        check(lines.size() == 1 && s.entities[0].health == 101.0f, "script reloaded: new on_update, health kept");
    }
    fs::remove_all(dir);
}

void testAssetRegistry()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::absolute("registry_assets");
    fs::remove_all(root);
    fs::create_directories(root / "levels");
    fs::create_directories(root / "materials");
    fs::create_directories(root / "models/crate");
    fs::create_directories(root / "textures");
    auto write = [](const fs::path& f, const std::string& text) { std::ofstream(f, std::ios::binary) << text; };
    auto read = [](const fs::path& f) {
        std::ifstream in(f, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    write(root / "CREDITS.md", "| all | made here | us | CC0 |\n"); // credits checks (checkAssetCredits) stay quiet
    write(root / "textures/rust.png", "png");
    write(root / "materials/rust.material", "albedo \"../textures/rust.png\"\nroughness 0.8\n");
    write(root / "models/crate/crate.gltf", "{\"images\":[{\"name\":\"look.png\",\"uri\": \"crate_diff.png\"}]}");
    write(root / "models/crate/crate_diff.png", "png");
    write(root / "levels/a.level", "model \"Crate\" \"../models/crate/crate.gltf\" 0 0 0\nmaterial \"../materials/rust.material\"\n"
                                   "missing \"../textures/gone.png\"\n");
    engine::AssetRegistry reg;
    std::string err;
    check(reg.scan(root, &err), "registry scan: " + err);
    check(reg.records().size() == 5, "five assets");
    const auto* tex = reg.find(fs::path("textures/rust.png"));
    check(tex && reg.referencedBy(root / "textures/rust.png").size() == 1, "the material uses the texture");
    const auto* level = reg.find(root / "levels/a.level");
    check(level && level->dependencies.size() == 2 && level->missing.size() == 1, "level dependencies and a missing file");
    const auto refs = engine::findReferences(root / "models/crate/crate.gltf", root);
    check(refs.size() == 1 && refs[0].text == "crate_diff.png", "glTF: uri values only, not names");
    const auto issues = reg.validate();
    check(std::count_if(issues.begin(), issues.end(), [](const engine::AssetIssue& i) { return i.severity == engine::AssetIssue::Severity::Error; }) == 1,
          "validation reports the missing file");
    check(reg.save(&err), "registry save: " + err);
    const std::uint64_t guid = tex->guid;

    // Rename into another folder: the material's path follows, the GUID stays.
    std::vector<fs::path> changed;
    check(reg.rename(root / "textures/rust.png", root / "textures/metal/rust_old.png", &err, &changed), "rename: " + err);
    check(read(root / "materials/rust.material").find("\"../textures/metal/rust_old.png\"") != std::string::npos && changed.size() == 1,
          "reference rewritten");
    // Move the glTF up a folder: its own relative uri is rewritten, the level follows.
    check(reg.rename(root / "models/crate/crate.gltf", root / "models/crate.gltf", &err), "move gltf: " + err);
    check(read(root / "models/crate.gltf").find("\"crate/crate_diff.png\"") != std::string::npos &&
              read(root / "levels/a.level").find("\"../models/crate.gltf\"") != std::string::npos,
          "moved glTF keeps its texture, the level its model");
    check(!reg.rename(root / "materials/rust.material", root / "levels/a.level", &err), "rename onto an existing file fails");
    engine::AssetRegistry again;
    check(again.scan(root, &err) && again.find(fs::path("textures/metal/rust_old.png")) &&
              again.find(fs::path("textures/metal/rust_old.png"))->guid == guid,
          "GUID kept across the rename and a rescan");
    fs::remove_all(root);
}

void testMemoryBudgets()
{
    // Frame arena: aligned bump allocations, one block again after a big frame.
    engine::FrameArena arena(4096);
    {
        std::pmr::vector<double> a(100, 1.0, &arena);
        std::pmr::vector<char> b(5000, 'x', &arena); // more than a block
        a.push_back(2.0);
        check(reinterpret_cast<std::uintptr_t>(a.data()) % alignof(double) == 0 && a.back() == 2.0 && b[4999] == 'x',
              "frame arena allocations");
        check(arena.overflowBlocks() >= 1, "frame arena grows past its block");
    }
    const std::size_t used = arena.used();
    arena.reset();
    check(arena.overflowBlocks() == 0 && arena.capacity() >= used && arena.used() == 0 && arena.peak() == used,
          "frame arena reset keeps one block big enough");

    // Budgets: over / under, unmeasured categories ignored.
    engine::MemoryBudget budget;
    budget.mb[static_cast<std::size_t>(engine::MemoryCategory::Lua)] = 1.0f;
    engine::MemoryUsage usage;
    usage.set(engine::MemoryCategory::Lua, 3u * 1024u * 1024u);
    usage.set(engine::MemoryCategory::Audio, 1024u);
    const auto over = engine::overBudget(usage, budget);
    check(over.size() == 1 && over[0].category == engine::MemoryCategory::Lua && over[0].usedMB > 2.9, "Lua over its budget");
    engine::Scene s;
    s.memoryBudget = budget;
    std::string err;
    check(engine::saveScene(s, "budget.level", &err), "save budget: " + err);
    engine::Scene t;
    check(engine::loadScene(t, "budget.level", &err) && t.memoryBudget == budget, "memory budget round trip");

    // Particle pool and budget: a finished effect leaves its arrays for the next one.
    auto fx = std::make_shared<engine::ParticleEffect>();
    fx->emitters.emplace_back();
    fx->emitters[0].burst = 50;
    fx->emitters[0].rate = 0.0f;
    fx->emitters[0].maxParticles = 100;
    fx->emitters[0].lifeMin = fx->emitters[0].lifeMax = 0.1f;
    engine::ParticleSystem ps;
    ps.setParticleBudget(60);
    ps.spawn(fx, glm::vec3(0.0f));
    ps.spawn(fx, glm::vec3(0.0f));
    ps.update(0.01f);
    check(ps.particleCount() == 100, "two bursts of 50");
    ps.spawn(fx, glm::vec3(0.0f));
    ps.update(0.01f);
    check(ps.particleCount() == 100, "budget full: the third burst waits");
    for (int i = 0; i < 20; ++i)
        ps.update(0.05f);
    check(ps.instanceCount() == 0, "the waiting burst ran once there was room, then everything finished");
    const std::size_t pooled = ps.memoryBytes();
    check(pooled > 0, "finished effects stay pooled");
    ps.spawn(fx, glm::vec3(0.0f));
    check(ps.memoryBytes() <= pooled + 4096, "a new effect reuses pooled arrays");
}

void testWeatherCycle()
{
    engine::Environment env;
    env.fogDensity = 0.01f;
    env.weather.enabled = true;
    env.weather.start = "Fog";
    env.weather.minMinutes = env.weather.maxMinutes = 0.1f; // 6 s per kind
    env.weather.transitionMinutes = 0.05f;                  // 3 s
    engine::WeatherDirector d(7);
    d.start(env);
    check(env.weatherNow.current == "Fog" && env.weatherNow.amount == 1.0f, "weather cycle starts with the chosen kind");
    const auto list = engine::defaultWeatherPresets();
    const float fog = list[3].fogDensity;
    check(std::abs(engine::environmentAt(env, {0, -1, 0}).fogDensity - fog) < 1e-6f, "the kind replaces the authored fog");
    // A script asks for a storm over 10 s: halfway is between fog and storm.
    env.weatherNow.request = "Storm";
    env.weatherNow.requestSeconds = 10.0f;
    d.update(env, 0.0f);
    d.update(env, 5.0f);
    const engine::Environment half = engine::environmentAt(env, {0, -1, 0});
    check(env.weatherNow.current == "Storm" && half.rain > 0.2f && half.rain < 0.8f, "storm blends in");
    d.update(env, 5.0f);
    check(std::abs(engine::environmentAt(env, {0, -1, 0}).storm - list[5].storm) < 1e-5f, "storm reached");
    // The cycle moves on to another kind after the hold time.
    for (int i = 0; i < 100; ++i)
        d.update(env, 0.1f);
    check(env.weatherNow.current != "Storm", "the cycle picks the next kind");
    check(!d.change(env, "Blizzard", 1.0f), "unknown kind is refused");
    // Off: the authored weather again.
    env.weather.enabled = false;
    d.update(env, 0.1f);
    check(engine::environmentAt(env, {0, -1, 0}).fogDensity == 0.01f, "cycle off restores the authored weather");

    // Level file round trip with a custom kind.
    engine::Scene s;
    s.environment.weather.enabled = true;
    s.environment.weather.start = "Ash fall";
    s.environment.weather.presets = engine::defaultWeatherPresets();
    s.environment.weather.presets.push_back({"Ash fall", 0.8f, 0.03f, 0.0f, 0.2f, 0.0f, 0.6f, 0.5f});
    std::string err;
    check(engine::saveScene(s, "weather.level", &err), "save weather: " + err);
    engine::Scene t;
    check(engine::loadScene(t, "weather.level", &err), "load weather: " + err);
    check(t.environment.weather == s.environment.weather, "weather cycle round trip");
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

void testWorldStreaming()
{
    // Ramps every 100 m along X; streaming with 64 m cells, load within 120 m.
    std::string err;
    engine::Model ramp;
    check(engine::loadModelFile(ramp, ENGINE_TEST_ASSETS "/models/ramp.gltf", &err), "streaming: ramp " + err);
    engine::Scene s;
    s.streaming.enabled = true;
    s.streaming.cellSize = 64.0f;
    s.streaming.loadRadius = 120.0f;
    s.streaming.unloadRadius = 180.0f;
    for (int i = 0; i < 6; ++i) {
        engine::Model m = ramp;
        m.name = "Ramp " + std::to_string(i);
        m.position = {i * 100.0f + 10.0f, 0.0f, 10.0f};
        s.models.push_back(m);
    }
    s.playerStart = {0.0f, 0.0f, 0.0f};
    check(engine::saveScene(s, "streaming_test.level", &err), "streaming: save " + err);
    engine::Scene editor, game;
    check(engine::loadScene(editor, "streaming_test.level", &err) && editor.streaming.enabled && editor.streaming.cellSize == 64.0f &&
              std::none_of(editor.models.begin(), editor.models.end(), [](const engine::Model& m) { return m.deferred; }),
          "streaming: the editor loads everything " + err);
    const glm::vec3 nan(std::numeric_limits<float>::quiet_NaN());
    check(engine::loadScene(game, "streaming_test.level", &err, nullptr, &nan), "streaming: game load " + err);
    int deferred = 0;
    for (const engine::Model& m : game.models)
        deferred += m.deferred;
    check(deferred == 4 && !game.models[0].deferred && !game.models[1].deferred && game.models[5].parts.empty() &&
              game.models[5].localBounds.max == ramp.localBounds.max,
          "streaming: far models deferred with their saved bounds (" + std::to_string(deferred) + ")");
    engine::PhysicsWorld physics;
    physics.setLevel(game);
    engine::WorldStreamer streamer;
    streamer.reset(game);
    const glm::vec3 top = game.models[5].position + glm::vec3((ramp.localBounds.min.x + ramp.localBounds.max.x) * 0.5f, 30.0f,
                                                              ramp.localBounds.max.z - 0.2f);
    check(!physics.raycast(top, {0, -1, 0}, 60.0f), "streaming: no collision while deferred");
    streamer.loadAround(game, &physics, {510.0f, 0.0f, 10.0f});
    check(!game.models[5].deferred && !game.models[5].parts.empty() && game.models[0].deferred &&
              game.models[5].parts.sharedWith(game.models[4].parts),
          "streaming: walking to the far end loads it (shared mesh) and releases the start");
    check(static_cast<bool>(physics.raycast(top, {0, -1, 0}, 60.0f)), "streaming: collision added with the mesh");
    const engine::WorldStreamer::Stats st = streamer.stats();
    check(st.cells == 6 && st.loadedCells == 2 && st.released >= 2 && st.meshBytes > 0, "streaming: stats");
    // Locked cells stay; a tiny budget keeps only the nearest.
    engine::WorldStreamer::Cell* far = streamer.cell(streamer.cellOf(game.models[5].bounds().min));
    check(far != nullptr, "streaming: cell lookup");
    if (far)
        far->locked = true;
    streamer.loadAround(game, &physics, {10.0f, 0.0f, 10.0f});
    check(!game.models[5].deferred && !game.models[0].deferred, "streaming: a locked cell is kept");
}

void testPrefabs()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::absolute("prefab_assets");
    fs::remove_all(dir);
    fs::create_directories(dir / "prefabs");
    auto writePrefab = [&](const char* wallColor) {
        std::ofstream(dir / "prefabs" / "post.prefab")
            << "version 6\n"
            << "group \"Walls\" \"\"\n"
            << "box \"Wall\" 1 0.5 0 2 1 0.2 " << wallColor << "\n  parent \"Walls\"\n  id 00000000000000a1\n"
            << "entity \"Guard\" \"enemy\" 0 0 2 0 0.6 1.8 0.6 1 0 0 \"\"\n  id 00000000000000a2\n  prop \"hp\" \"50\"\n"
            << "  output \"OnDeath\" \"Door\" \"Open\" \"\" 0 0\n"
            << "entity \"Door\" \"relay\" 0 0 3 0 1 1 1 1 1 1 \"\"\n  id 00000000000000a3\n";
        engine::clearPrefabCache();
    };
    writePrefab("0.5 0.5 0.5");
    const fs::path level = dir / "test.level";
    std::ofstream(level) << "version 6\n"
                         << "prefab \"A\" \"prefabs/post.prefab\" 10 0 0 0\n  id 0000000000000001\n"
                         << "prefab \"B\" \"prefabs/post.prefab\" 0 0 10 90\n  id 0000000000000002\n";
    engine::Scene scene;
    std::string error;
    check(engine::loadScene(scene, level, &error), ("level with prefabs loads: " + error).c_str());
    const int aWall = engine::findObject(scene, "A/Wall"), bWall = engine::findObject(scene, "B/Wall");
    check(scene.boxes.size() == 2 && scene.entities.size() == 4 && aWall >= 0 && bWall >= 0, "two instances expanded");
    check(glm::length(engine::objectPosition(scene, aWall) - glm::vec3(11, 0.5f, 0)) < 1e-3f, "instance offset applied");
    check(glm::length(engine::objectPosition(scene, bWall) - glm::vec3(0, 0.5f, 9)) < 1e-3f && std::abs(scene.boxes[1].rotation.y - 90.0f) < 1e-3f,
          "instance yaw turns positions and rotations");
    check(engine::objectParent(scene, aWall) == "A/Walls" && engine::findGroup(scene, "A") >= 0, "hierarchy under the instance group");
    const int aGuard = engine::findObject(scene, "A/Guard");
    check(aGuard >= 0 && scene.entity(aGuard).outputs[0].target == "A/Door", "links inside the prefab stay inside the instance");
    check(scene.boxes[0].id != scene.boxes[1].id, "instances get their own object IDs");

    // Overrides: move a wall, change a property, delete the door.
    scene.boxes[static_cast<std::size_t>(aWall)].position.x += 1.0f;
    scene.entity(aGuard).properties["hp"] = "80";
    scene.entities.erase(scene.entities.begin() + (engine::findObject(scene, "A/Door") - scene.firstEntity()));
    check(engine::saveScene(scene, level, &error), "level with prefabs saves");
    std::string text;
    {
        std::ifstream in(level);
        text.assign(std::istreambuf_iterator<char>(in), {});
    }
    check(text.find("box ") == std::string::npos && text.find("override \"Wall\" transform 2 0.5 0") != std::string::npos &&
              text.find("override \"Guard\" prop \"hp\" \"80\"") != std::string::npos &&
              text.find("override \"Door\" removed") != std::string::npos,
          "only the differences are saved");
    // A change to the prefab shows in both instances, the overrides stay.
    writePrefab("0.2 0.3 0.4");
    check(engine::loadScene(scene, level, &error), "level reloads");
    const int a2 = engine::findObject(scene, "A/Wall"), b2 = engine::findObject(scene, "B/Wall");
    check(a2 >= 0 && b2 >= 0 && std::abs(scene.boxes[static_cast<std::size_t>(a2)].color.r - 0.2f) < 1e-4f &&
              std::abs(scene.boxes[static_cast<std::size_t>(b2)].color.r - 0.2f) < 1e-4f,
          "prefab change reaches every instance");
    check(std::abs(scene.boxes[static_cast<std::size_t>(a2)].position.x - 12.0f) < 1e-3f &&
              scene.entity(engine::findObject(scene, "A/Guard")).properties["hp"] == "80" && engine::findObject(scene, "A/Door") < 0 &&
              engine::findObject(scene, "B/Door") >= 0,
          "overrides survive a prefab change");
    // Live refresh in the editor (the prefab changes while the level is open).
    writePrefab("0.2 0.3 0.4");
    engine::loadPrefab(dir / "prefabs" / "post.prefab"); // cached as the instances were built
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::ofstream(dir / "prefabs" / "post.prefab", std::ios::app) << "box \"Extra\" 0 3 0 1 1 1 1 1 1\n  id 00000000000000a4\n";
    check(engine::refreshPrefabInstances(scene, dir) == 2 && engine::findObject(scene, "A/Extra") >= 0 &&
              std::abs(scene.boxes[static_cast<std::size_t>(engine::findObject(scene, "A/Wall"))].position.x - 12.0f) < 1e-3f,
          "refresh adds new prefab objects and keeps overrides");

    // Moving every object of an instance moves the instance; another change detaches.
    for (int o : engine::descendantObjects(scene, "B"))
        engine::translateObject(scene, o, glm::vec3(5, 0, 0));
    scene.boxes[static_cast<std::size_t>(engine::findObject(scene, "A/Extra"))].color = glm::vec3(0, 1, 0);
    check(engine::saveScene(scene, level, &error), "saves again");
    check(engine::loadScene(scene, level, &error), "reloads again");
    const int b = engine::findPrefabInstance(scene, "B");
    check(b >= 0 && glm::length(scene.prefabs[static_cast<std::size_t>(b)].position - glm::vec3(5, 0, 10)) < 1e-3f,
          "moving all objects moves the instance");
    const int extra = engine::findObject(scene, "A/Extra");
    check(extra >= 0 && scene.boxes[static_cast<std::size_t>(extra)].prefabInstance.empty() &&
              scene.boxes[static_cast<std::size_t>(extra)].color.g > 0.9f && engine::objectParent(scene, extra).empty(),
          "an object changed beyond overrides is saved as a normal object");

    // Revert, unpack, create from a group, apply.
    check(engine::revertPrefabInstance(scene, engine::findPrefabInstance(scene, "A")) && engine::findObject(scene, "A/Door") >= 0,
          "revert brings the prefab back");
    engine::unpackPrefab(scene, engine::findPrefabInstance(scene, "A"));
    check(engine::findPrefabInstance(scene, "A") < 0 && engine::findObject(scene, "A/Wall") >= 0 &&
              scene.boxes[static_cast<std::size_t>(engine::findObject(scene, "A/Wall"))].prefabInstance.empty(),
          "unpack keeps the objects");
    engine::Scene s2;
    s2.groups.push_back({"Hut", "", {}});
    s2.boxes.push_back(engine::Box("Floor", glm::vec3(20, 0.1f, 20), glm::vec3(4, 0.2f, 4), glm::vec3(0.5f)));
    s2.boxes.back().parent = "Hut";
    s2.boxes.push_back(engine::Box("Roof", glm::vec3(20, 3, 20), glm::vec3(4, 0.2f, 4), glm::vec3(0.5f)));
    s2.boxes.back().parent = "Hut";
    engine::assignObjectIds(s2);
    const fs::path hut = dir / "prefabs" / "hut.prefab";
    check(engine::createPrefabFromGroup(s2, "Hut", hut, &error), ("group saved as a prefab: " + error).c_str());
    check(s2.prefabs.size() == 1 && glm::length(s2.prefabs[0].position - glm::vec3(20, 0, 20)) < 1e-3f &&
              engine::findObject(s2, "Hut/Roof") >= 0 &&
              std::abs(engine::objectPosition(s2, engine::findObject(s2, "Hut/Roof")).y - 3.0f) < 1e-3f,
          "the group is replaced by an instance in the same place");
    engine::addPrefabInstance(s2, hut, glm::vec3(0), 0.0f);
    s2.boxes[static_cast<std::size_t>(engine::findObject(s2, "Hut/Roof"))].color = glm::vec3(1, 0, 0);
    check(engine::applyPrefabInstance(s2, 0, dir, &error), "apply to prefab");
    const int other = engine::findObject(s2, s2.prefabs[1].name + "/Roof");
    check(other >= 0 && s2.boxes[static_cast<std::size_t>(other)].color.r > 0.9f, "apply updates the other instances");
    engine::setPrefabTransform(s2, 1, glm::vec3(0, 0, 0), 180.0f);
    check(glm::length(engine::objectPosition(s2, other) - glm::vec3(0, 3, 0)) < 1e-3f, "turning an instance keeps its objects in place");
    fs::remove_all(dir);
    engine::clearPrefabCache();
}

void testRangeAllocator()
{
    engine::RangeAllocator a(100);
    const auto x = a.allocate(40), y = a.allocate(40), z = a.allocate(40);
    check(x && *x == 0 && y && *y == 40 && !z, "first fit, full pool refuses");
    a.release(*x, 40);
    check(a.largestFree() == 40 && a.used() == 40, "released range is free again");
    a.release(*y, 40);
    check(a.largestFree() == 100 && a.used() == 0, "neighbouring free ranges merge");
    a.allocate(90);
    a.grow(200);
    const auto w = a.allocate(100);
    check(w && *w == 90 && a.capacity() == 200, "growing adds room at the end");
}

void testImpostorSettings()
{
    namespace fs = std::filesystem;
    const fs::path file = fs::absolute("impostor_settings.level");
    std::ofstream(file) << "version 6\nbox \"B\" 0 0 0 1 1 1 1 1 1\n";
    engine::Scene scene;
    std::string error;
    check(engine::loadScene(scene, file, &error), "impostor level loads");
    engine::Model m;
    m.name = "Tree";
    m.sourcePath = fs::absolute("tree.glb");
    m.impostorDistance = 90.0f;
    std::ostringstream out;
    engine::writeModel(out, m, fs::current_path(), false);
    check(out.str().find("  impostor 90") != std::string::npos, "impostor distance is saved with the model");
    fs::remove(file);
}

void testVolumetricFogSettings()
{
    namespace fs = std::filesystem;
    const fs::path file = fs::absolute("volumetric_fog.level");
    engine::Scene scene;
    check(scene.environment.fogVolumetricRange > 0.0f, "volumetric fog is on by default");
    scene.environment.fogVolumetricRange = 48.0f;
    scene.environment.fogLightShafts = 2.5f;
    scene.environment.fogAnisotropy = -0.2f;
    scene.environment.fogNoise = 0.0f;
    scene.environment.fogLocalLights = 3.0f;
    std::string error;
    check(engine::saveScene(scene, file, &error), "volumetric fog level saves");
    engine::Scene loaded;
    check(engine::loadScene(loaded, file, &error), "volumetric fog level loads");
    const engine::Environment& e = loaded.environment;
    check(e.fogVolumetricRange == 48.0f && e.fogLightShafts == 2.5f && e.fogAnisotropy == -0.2f && e.fogNoise == 0.0f &&
              e.fogLocalLights == 3.0f,
          "volumetric fog settings round-trip");
    // Defaults write nothing (older levels stay unchanged on save).
    engine::Scene plain;
    check(engine::saveScene(plain, file, &error), "plain level saves");
    std::string text;
    {
        std::ifstream in(file); // closed before the remove below (Windows cannot delete an open file)
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    check(text.find("fog_light_shafts") == std::string::npos, "default volumetric settings are not written");
    check(engine::RenderQuality::preset(0).volumetricFog == 0 && engine::RenderQuality::preset(2).volumetricFog > 0,
          "volumetric fog follows the quality preset");
    fs::remove(file);
}

void testProbes()
{
    // Constant radiance 1 from every direction: irradiance / pi = 1 for every normal.
    const int size = 8;
    std::vector<float> faces(6 * size * size * 3, 1.0f);
    glm::vec4 sh[3];
    engine::projectIrradiance(faces, size, sh);
    const glm::vec3 up = engine::evalIrradiance(sh, {0.0f, 1.0f, 0.0f});
    const glm::vec3 side = engine::evalIrradiance(sh, {1.0f, 0.0f, 0.0f});
    check(std::abs(up.x - 1.0f) < 0.02f && std::abs(side.z - 1.0f) < 0.02f, "SH of constant radiance is flat");
    // Light only from above (+Y face, GL face 2): brighter up than down.
    std::vector<float> top(6 * size * size * 3, 0.0f);
    for (int i = 2 * size * size * 3; i < 3 * size * size * 3; ++i)
        top[i] = 1.0f;
    engine::projectIrradiance(top, size, sh);
    check(engine::evalIrradiance(sh, {0.0f, 1.0f, 0.0f}).x > engine::evalIrradiance(sh, {0.0f, -1.0f, 0.0f}).x + 0.2f,
          "SH keeps the direction of the light");
    check(engine::cubeTexelDirection(0, 0.5f, 0.5f).x > 0.99f, "cube face 0 looks along +X");

    engine::Scene scene;
    engine::Entity e;
    e.type = "irradiance_volume";
    e.name = "Room";
    e.position = {0.0f, 0.0f, 0.0f};
    e.size = {8.0f, 3.0f, 4.0f};
    e.properties["spacing"] = "2";
    scene.entities.push_back(e);
    engine::Entity r;
    r.type = "reflection_probe";
    r.name = "Mirror";
    r.size = {4.0f, 3.0f, 4.0f};
    r.properties["resolution"] = "64";
    r.properties["parallax"] = "0";
    scene.entities.push_back(r);
    auto volumes = engine::irradianceVolumesToBake(scene);
    auto refl = engine::reflectionProbesToBake(scene);
    check(volumes.size() == 1 && refl.size() == 1, "probes come from their entities");
    check(volumes[0].dims.x == 5 && volumes[0].dims.z == 3, "irradiance grid follows the spacing");
    check(refl[0].size == 64 && !refl[0].parallax && std::abs(refl[0].shape.center.y - 1.5f) < 1e-4f,
          "reflection probe settings and box middle");
    const glm::vec3 corner = engine::irradiancePoint(volumes[0], {0, 0, 0});
    check(corner.x > -4.0f && corner.x < -3.5f && corner.y > 0.0f, "irradiance points stay inside the box");

    engine::ProbeSet set;
    volumes[0].sh.assign(static_cast<size_t>(volumes[0].dims.x * volumes[0].dims.y * volumes[0].dims.z) * 3, glm::vec4(0.5f));
    volumes[0].intensity = 2.0f;
    refl[0].faces.assign(6 * 64 * 64, 0x12345678u);
    set.volumes = volumes;
    set.reflections = refl;
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "engine_tests.probes";
    engine::ProbeSet back;
    check(engine::saveProbes(set, file) && engine::loadProbes(back, file), "probes save and load");
    check(back.volumes.size() == 1 && back.volumes[0].sh == set.volumes[0].sh && back.volumes[0].intensity == 2.0f &&
              back.reflections.size() == 1 && back.reflections[0].faces == set.reflections[0].faces &&
              back.reflections[0].shape.name == "Mirror",
          "probe file round trip");
    std::filesystem::remove(file);
    check(engine::probeFile("a/b.level") == std::filesystem::path("a/b.probes"), "probe file sits next to the level");
}

void testThickets()
{
    engine::Scene s;
    auto plant = [&](const char* name, glm::vec3 pos, glm::vec3 size, float sway) {
        engine::Model m;
        m.name = name;
        m.position = pos;
        m.localBounds = {glm::vec3(-size.x * 0.5f, 0.0f, -size.z * 0.5f), glm::vec3(size.x * 0.5f, size.y, size.z * 0.5f)};
        m.sway = sway;
        s.models.push_back(m);
    };
    plant("Bush", {10, 0, 0}, {2.0f, 1.4f, 2.0f}, 0.7f);
    plant("Grass", {20, 0, 0}, {1.0f, 0.3f, 1.0f}, 1.0f);   // too low: no rustle, no slowdown
    plant("Birch", {30, 0, 0}, {4.0f, 12.0f, 4.0f}, 0.3f);  // a tree: its trunk collides
    plant("Crate", {40, 0, 0}, {1.0f, 1.0f, 1.0f}, 0.0f);
    engine::Thickets t;
    t.build(s);
    check(t.size() == 1, "only the bush is a thicket");
    check(t.densityAt({10, 0, 0}) > 0.95f, "the middle of a bush is dense");
    const float edge = t.densityAt({10.9f, 0, 0});
    check(edge > 0.0f && edge < 0.5f, "its edge thins out");
    check(t.densityAt({12.0f, 0, 0}) == 0.0f, "outside the bush nothing");
    check(t.densityAt({10, 1.5f, 0}) == 0.0f, "standing above the bush is not in it");
    check(t.densityAt({20, 0, 0}) == 0.0f && t.densityAt({30, 0, 0}) == 0.0f && t.densityAt({40, 0, 0}) == 0.0f,
          "grass, trees and props are not thickets");

    // The slowdown is a level setting (editor: Player > Bush slowdown).
    s.player.bushSlow = 0.55f;
    std::string err;
    engine::Scene back;
    s.models.clear(); // test models have no files
    check(engine::saveScene(s, "thickets.level", &err) && engine::loadScene(back, "thickets.level", &err), "reload: " + err);
    check(std::abs(back.player.bushSlow - 0.55f) < 1e-4f, "bush slowdown survives save + load");
    std::filesystem::remove("thickets.level");
}

void testPlantCollision()
{
    // A "tree": a 16-sided stem (r = 0.1 m) 5 m high at the origin and a branch at 1 m reaching out to x = 3.
    auto addBox = [](engine::MeshPart& part, glm::vec3 lo, glm::vec3 hi) {
        const auto base = static_cast<std::uint32_t>(part.positions.size());
        for (int i = 0; i < 8; ++i)
            part.positions.push_back({i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z});
        const std::uint32_t f[36] = {0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1, 2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3};
        for (std::uint32_t i : f)
            part.indices.push_back(base + i);
    };
    engine::Model tree;
    engine::MeshPart part;
    part.id = 0x7ee5eed;
    for (int i = 0; i < 16; ++i) { // stem: bottom and top ring
        const float a = static_cast<float>(i) / 16.0f * 6.2831853f;
        part.positions.push_back({0.1f * std::cos(a), 0.0f, 0.1f * std::sin(a)});
        part.positions.push_back({0.1f * std::cos(a), 5.0f, 0.1f * std::sin(a)});
    }
    for (std::uint32_t i = 0; i < 16; ++i) {
        const std::uint32_t a = i * 2, b = ((i + 1) % 16) * 2;
        part.indices.insert(part.indices.end(), {a, a + 1, b, b, a + 1, b + 1});
    }
    addBox(part, {0.1f, 0.9f, -0.05f}, {3.0f, 1.1f, 0.05f});
    tree.parts.push_back(part);
    tree.name = "Tree";
    const engine::Trunk t = engine::trunkOf(tree);
    check(glm::length(t.center) < 0.05f && t.radius > 0.05f && t.radius < 0.3f && std::abs(t.height - 5.0f) < 0.01f,
          "the trunk is found under the branch: r=" + std::to_string(t.radius));
    check(engine::plantCollider(0.0f) == engine::PlantCollider::Mesh && engine::plantCollider(0.3f) == engine::PlantCollider::Trunk &&
              engine::plantCollider(0.7f) == engine::PlantCollider::None,
          "rigid: mesh, trees: trunk, bushes: nothing");
    auto hits = [&](float sway, glm::vec3 from) {
        engine::Scene scene = flatFloor();
        engine::Model m = tree;
        m.sway = sway;
        scene.models.push_back(m);
        engine::PhysicsWorld world;
        world.setLevel(scene);
        return world.raycast(from, {0.0f, 0.0f, 1.0f}, 10.0f).has_value();
    };
    check(hits(0.0f, {2.0f, 1.0f, -5.0f}), "a rigid model blocks with its whole mesh (the branch)");
    check(!hits(0.3f, {2.0f, 1.0f, -5.0f}) && hits(0.3f, {0.0f, 1.0f, -5.0f}), "a tree blocks with its trunk only");
    check(!hits(0.7f, {0.0f, 1.0f, -5.0f}), "a bush does not block at all");
}

void testVertexPaint()
{
    // A 2 x 2 m quad of 3 x 3 vertices facing +Y, placed at (10, 0, 0).
    engine::Model model;
    engine::MeshPart part;
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x)
            part.positions.push_back({x - 1.0f, 0.0f, z - 1.0f});
    for (std::uint32_t z = 0; z < 2; ++z)
        for (std::uint32_t x = 0; x < 2; ++x) {
            const std::uint32_t a = z * 3 + x;
            part.indices.insert(part.indices.end(), {a, a + 3, a + 1, a + 1, a + 3, a + 4});
        }
    model.parts.push_back(part);
    model.position = {10.0f, 0.0f, 0.0f};
    float t = 0.0f;
    check(engine::raycastModel(model, {10.5f, 5.0f, 0.5f}, {0.0f, -1.0f, 0.0f}, 100.0f, t) && std::abs(t - 5.0f) < 1e-4f,
          "a ray hits the model's triangles");
    check(!engine::raycastModel(model, {13.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f, t), "a ray beside the model misses");
    engine::VertexPaint paint;
    const std::uint64_t v0 = paint.version;
    check(engine::paintVertices(paint, model, {10.0f, 0.0f, 0.0f}, 0.9f, 0, 1.0f, false), "a dab paints vertices");
    check(paint.parts.size() == 1 && paint.parts[0].size() == 9 && paint.parts[0][4].r == 255 && paint.parts[0][0].r == 0 &&
              paint.parts[0][4].g == 0 && paint.version != v0,
          "paint reaches the vertices within the radius, in its channel");
    engine::paintVertices(paint, model, {10.0f, 0.0f, 0.0f}, 0.9f, 0, 0.5f, true);
    check(paint.parts[0][4].r > 0 && paint.parts[0][4].r < 255, "erasing takes paint away gradually");
    paint.parts[0][8] = {1, 2, 3, 4};
    std::vector<glm::u8vec4> back;
    check(engine::decodeVertexPaint(engine::encodeVertexPaint(paint.parts[0]), back) && back == paint.parts[0],
          "vertex paint survives the level file encoding");
    check(!engine::decodeVertexPaint("@@@", back), "bad vertex paint text is refused");
}

void testMaterialInstances()
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "engine_tests_materials";
    std::filesystem::create_directories(dir);
    {
        std::ofstream base(dir / "base.material");
        base << "version 2\nname \"Base\"\ncolor 0.5 0.4 0.3\nroughness 0.8\nmetallic 0\ntiling 2 2\n";
        std::ofstream inst(dir / "rusty.material");
        inst << "version 2\nname \"Rusty\"\nparent \"base.material\"\nroughness 0.3\n";
    }
    std::string error;
    auto instance = engine::loadMaterial(dir / "rusty.material", &error);
    check(instance != nullptr, "material instance loads");
    if (!instance)
        return;
    auto base = engine::loadMaterial(dir / "base.material");
    check(instance->parent == base && instance->color == glm::vec3(0.5f, 0.4f, 0.3f) && instance->tiling == glm::vec2(2.0f),
          "an instance takes the parent's values");
    check(instance->roughness == 0.3f && instance->overrides.count("roughness") == 1 && instance->overrides.size() == 1,
          "an instance keeps its own values");
    base->color = glm::vec3(1.0f, 0.0f, 0.0f);
    base->roughness = 0.9f;
    engine::refreshMaterialInstances();
    check(instance->color == glm::vec3(1.0f, 0.0f, 0.0f) && instance->roughness == 0.3f, "an instance follows the edited parent");
    engine::Material before = *instance;
    instance->metallic = 1.0f;
    const auto diff = engine::materialDifferences(before, *instance);
    check(diff.size() == 1 && diff[0] == "metallic", "material differences name the changed value");
    instance->overrides.insert("metallic");
    check(engine::saveMaterial(*instance, dir / "rusty.material", &error), "an instance saves");
    std::ifstream in(dir / "rusty.material");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close(); // Windows cannot delete the folder below while the file is open
    check(text.find("parent") != std::string::npos && text.find("metallic 1") != std::string::npos &&
              text.find("roughness 0.3") != std::string::npos && text.find("color") == std::string::npos,
          "an instance file holds only its overrides");
    check(!engine::setMaterialParent(*base, dir / "rusty.material"), "a material cannot be its own ancestor");
    instance.reset();
    base.reset();

    // Detail map settings: parsed, inherited like any value, left out of files without detail maps.
    {
        std::ofstream detail(dir / "detail.material");
        detail << "version 2\nname \"Detail\"\ndetail_tiling 16\ndetail_strength 0.5\ndetail_distance 20\n";
    }
    auto detail = engine::loadMaterial(dir / "detail.material", &error);
    check(detail && detail->detailTiling == 16.0f && detail->detailStrength == 0.5f && detail->detailDistance == 20.0f,
          "detail map settings load");
    if (detail) {
        check(engine::saveMaterial(*detail, dir / "detail.material", &error), "a material with detail settings saves");
        std::ifstream din(dir / "detail.material");
        const std::string dtext((std::istreambuf_iterator<char>(din)), std::istreambuf_iterator<char>());
        check(dtext.find("detail_") == std::string::npos, "detail settings are not written without detail maps");
        engine::Material changed = *detail;
        changed.detailStrength = 1.0f;
        const auto ddiff = engine::materialDifferences(*detail, changed);
        check(ddiff.size() == 1 && ddiff[0] == "detail_strength", "detail strength is a material value");
    }
    detail.reset();

    // Height map (parallax): depth parsed, written only with a height map, a material value.
    {
        std::ofstream height(dir / "height.material");
        height << "version 2\nname \"Height\"\nheight_depth 0.035\n";
    }
    auto height = engine::loadMaterial(dir / "height.material", &error);
    check(height && height->heightDepth == 0.035f && std::string(engine::mapKey(engine::MapKind::Height)) == "height_map",
          "height depth loads");
    if (height) {
        check(engine::saveMaterial(*height, dir / "height.material", &error), "a material with a height depth saves");
        std::ifstream hin(dir / "height.material");
        const std::string htext((std::istreambuf_iterator<char>(hin)), std::istreambuf_iterator<char>());
        check(htext.find("height_depth") == std::string::npos, "the height depth is not written without a height map");
        engine::Material changed = *height;
        changed.heightDepth = 0.01f;
        const auto hdiff = engine::materialDifferences(*height, changed);
        check(hdiff.size() == 1 && hdiff[0] == "height_depth", "height depth is a material value");
    }
    height.reset();

    // Blend layers: another material by path, saved relative; a material cannot be its own layer.
    {
        std::ofstream moss(dir / "moss.material");
        moss << "version 2\nname \"Moss\"\ncolor 0.2 0.5 0.1\n";
        std::ofstream wall(dir / "wall.material");
        wall << "version 2\nname \"Wall\"\nlayer1_material \"moss.material\"\nblend_sharpness 6\n";
    }
    auto wall = engine::loadMaterial(dir / "wall.material", &error);
    check(wall && wall->layers[0] && wall->layers[0]->name == "Moss" && !wall->layers[1] && wall->blendSharpness == 6.0f,
          "a material loads its blend layer");
    if (wall) {
        check(!engine::setMaterialLayer(*wall, 1, dir / "wall.material"), "a material cannot be its own layer");
        check(engine::saveMaterial(*wall, dir / "wall.material", &error), "a layered material saves");
        std::ifstream win(dir / "wall.material");
        const std::string wtext((std::istreambuf_iterator<char>(win)), std::istreambuf_iterator<char>());
        check(wtext.find("layer1_material \"moss.material\"") != std::string::npos && wtext.find("layer2_material") == std::string::npos,
              "blend layers are saved relative, unset ones left out");
    }
    wall.reset();
    std::filesystem::remove_all(dir);
}

void testInteriorVolumes()
{
    engine::Scene scene;
    engine::Entity e;
    e.type = "interior";
    e.position = {0.0f, 0.0f, 0.0f};
    e.yawDegrees = 90.0f; // long side (x) turned onto world z
    e.size = {10.0f, 3.0f, 2.0f};
    e.properties["sky"] = "0.1";
    e.properties["fade"] = "1";
    scene.entities.push_back(e);
    engine::Entity other;
    other.type = "reverb";
    scene.entities.push_back(other);
    const auto volumes = engine::interiorVolumes(scene);
    check(volumes.size() == 1 && volumes[0].sky == 0.1f, "interior volumes come from interior entities");
    check(engine::interiorWeight(volumes, {0.0f, 1.0f, 4.0f}) == 1.0f, "inside a turned interior");
    check(engine::interiorWeight(volumes, {4.0f, 1.0f, 0.0f}) == 0.0f, "outside a turned interior");
    const float edge = engine::interiorWeight(volumes, {1.5f, 1.0f, 0.0f});
    check(edge > 0.4f && edge < 0.6f, "interior blends out over its fade distance");
    scene.entities[0].properties["enabled"] = "0";
    check(engine::interiorVolumes(scene).empty(), "a disabled interior does nothing");
}

void testParticleEvents()
{
    // Sparks: a burst falling onto the ground 1 m below the effect, bouncing; each dying spark
    // spawns two puffs; the sparks emit a light.
    auto fx = std::make_shared<engine::ParticleEffect>();
    engine::ParticleEmitter sparks;
    sparks.name = "Sparks";
    sparks.rate = 0.0f;
    sparks.burst = 10;
    sparks.direction = {0.0f, -1.0f, 0.0f};
    sparks.spread = 0.0f;
    sparks.speedMin = sparks.speedMax = 5.0f;
    sparks.lifeMin = sparks.lifeMax = 0.6f;
    sparks.collision = engine::ParticleCollision::Bounce;
    sparks.bounce = 0.5f;
    sparks.onDeath = 1;
    sparks.onDeathCount = 2;
    sparks.light = true;
    engine::ParticleEmitter puff;
    puff.name = "Puff";
    puff.rate = 0.0f;
    puff.lifeMin = puff.lifeMax = 5.0f;
    puff.speedMin = puff.speedMax = 0.0f;
    fx->emitters = {sparks, puff};
    engine::ParticleSystem ps;
    ps.setGroundQuery([](glm::vec3 p) { return p.y - 1.0f; });
    const auto h = ps.spawn(fx, {0.0f, 2.0f, 0.0f});
    for (int i = 0; i < 20; ++i)
        ps.update(0.02f); // 0.4 s: the sparks have reached the ground and bounced
    std::vector<engine::SpriteBatch> batches;
    ps.appendSprites({0.0f, 2.0f, 5.0f}, glm::mat3(1.0f), batches);
    bool above = !batches.empty();
    for (const engine::SpriteBatch& b : batches)
        for (const engine::SpriteInstance& s : b.instances)
            above = above && s.position.y >= 1.0f - 1e-4f;
    check(above, "colliding particles stay above the ground");
    std::vector<engine::LocalLight> lights;
    ps.appendLights({0.0f, 2.0f, 5.0f}, lights);
    check(lights.size() == 1 && glm::length(lights[0].color) > 0.0f, "an emitter with light adds a point light");
    for (int i = 0; i < 15; ++i)
        ps.update(0.02f); // past the sparks' life
    check(ps.alive(h) && ps.particleCount() == 20, "every dying spark spawns its sub-emitter's particles");
    lights.clear();
    ps.appendLights({0.0f, 2.0f, 5.0f}, lights);
    check(lights.empty(), "the light goes out with its particles");

    const std::filesystem::path file = std::filesystem::absolute("events_test.particle");
    std::string error;
    check(engine::saveParticleFile(*fx, file, &error), "save particle events: " + error);
    engine::ParticleEffect back;
    check(engine::loadParticleFile(back, file, &error) && back.emitters.size() == 2 &&
              back.emitters[0].collision == engine::ParticleCollision::Bounce && back.emitters[0].bounce == 0.5f &&
              back.emitters[0].onDeath == 1 && back.emitters[0].onDeathCount == 2 && back.emitters[0].light &&
              back.emitters[1].collision == engine::ParticleCollision::None && back.emitters[1].onDeath == -1,
          "collision, sub-emitter and light survive save + load");
    std::filesystem::remove(file);
}

void testDistortParticles()
{
    namespace fs = std::filesystem;
    const fs::path file = fs::absolute("distort_test.particle");
    engine::ParticleEffect fx;
    engine::ParticleEmitter e;
    e.name = "Heat haze";
    e.blend = engine::SpriteBlend::Distort;
    fx.emitters.push_back(e);
    std::string error;
    check(engine::saveParticleFile(fx, file, &error), "distort particle saves");
    engine::ParticleEffect loaded;
    check(engine::loadParticleFile(loaded, file, &error) && loaded.emitters.size() == 1 &&
              loaded.emitters[0].blend == engine::SpriteBlend::Distort,
          "distort blend round-trips");
    fs::remove(file);
}

void testAnimGraph()
{
    // Synthetic skeleton: root -> Spine -> Hand. Clips move the root along x (A: 0, B: 10) and
    // the spine (C: 5; C also moves the root, which the upper layer's mask must ignore).
    engine::Skeleton sk;
    sk.nodes = {{"Root", -1, {}, {1, 0, 0, 0}, {1, 1, 1}}, {"Spine", 0, {}, {1, 0, 0, 0}, {1, 1, 1}}, {"Hand", 1, {}, {1, 0, 0, 0}, {1, 1, 1}}};
    auto clip = [](const char* name, float duration, std::vector<std::pair<int, float>> moves) {
        engine::Skeleton::Clip c{name, duration, {}};
        for (auto [node, x] : moves)
            c.channels.push_back({node, engine::Skeleton::Path::Translation, false, {0.0f, duration}, {glm::vec4(x, 0, 0, 0), glm::vec4(x, 0, 0, 0)}});
        return c;
    };
    sk.clips = {clip("A", 1.0f, {{0, 0.0f}}), clip("B", 2.0f, {{0, 10.0f}}), clip("C", 1.0f, {{0, 99.0f}, {1, 5.0f}})};

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "animgraph_test";
    std::filesystem::create_directories(dir);
    {
        std::ofstream f(dir / "t.animgraph");
        f << "param \"speed\" float 0\nparam \"hit\" trigger\nparam \"aim\" bool 0\nparam \"x\" float 0\nparam \"y\" float 0\n"
             "event \"A\" 0.5 \"step\"\n"
             "layer \"Base\" \"\" 1 \"\"\nentry \"Loco\"\n"
             "state \"Loco\" blend1d \"speed\" \"\" loop 1 speed 1\nsample \"A\" 0\nsample \"B\" 2\n"
             "state \"Hit\" clip \"\" \"\" loop 0\nsample \"A\"\n"
             "state \"Grid\" blend2d \"x\" \"y\"\nsample \"A\" 0 0\nsample \"B\" 1 0\nsample \"A\" 0 1\nsample \"B\" 1 1\n"
             "transition \"Loco\" \"Hit\" 0 -1 hit\ntransition \"Hit\" \"Loco\" 0 1\ntransition \"*\" \"Grid\" 0 -1 speed > 50\n"
             "layer \"Upper\" \"Spine\" 1 \"\"\nentry \"Off\"\nstate \"Off\" empty\nstate \"Aim\" clip\nsample \"C\"\n"
             "transition \"Off\" \"Aim\" 0 -1 aim\ntransition \"Aim\" \"Off\" 0.5 -1 !aim\n";
    }
    std::string error;
    engine::AnimGraph g;
    check(engine::loadAnimGraphFile(g, dir / "t.animgraph", &error), "anim graph loads: " + error);
    check(g.layers.size() == 2 && g.layers[0].states.size() == 3 && g.layers[0].transitions.size() == 3 && g.params.size() == 5,
          "anim graph layers, states, transitions and parameters");
    check(g.layers[0].transitions[2].from == -1 && g.layers[0].transitions[2].conditions[0].op == engine::AnimGraph::Op::Greater,
          "from-any transition with a comparison");
    check(engine::saveAnimGraphFile(g, dir / "copy.animgraph", &error), "anim graph saves");
    engine::AnimGraph copy;
    check(engine::loadAnimGraphFile(copy, dir / "copy.animgraph", &error) && copy.layers[1].maskBone == "Spine" &&
              copy.layers[0].states[2].samples.size() == 4 && copy.layers[0].transitions[1].exitTime == 1.0f && copy.events.size() == 1,
          "anim graph round trip");
    engine::AnimGraph bad;
    {
        std::ofstream f(dir / "bad.animgraph");
        f << "layer \"Base\"\nstate \"S\" clip\ntransition \"S\" \"Nope\" 0.2 -1\n";
    }
    check(!engine::loadAnimGraphFile(bad, dir / "bad.animgraph", &error) && error.find("Nope") != std::string::npos,
          "transition to an unknown state is an error");

    engine::Animator a;
    a.setGraph(std::make_shared<engine::AnimGraph>(g));
    std::vector<glm::mat4> pose;
    auto rootX = [&] { engine::evaluatePose(sk, &a, pose); return pose[0][3].x; };
    check(a.setParam("speed", 1.0f) && !a.setParam("missing", 1.0f), "parameters by name");
    a.update(sk, 0.0f);
    check(std::abs(rootX() - 5.0f) < 1e-3f, "blend 1D halfway between A (0) and B (10)");
    a.update(sk, 0.75f); // synced phase: one pass takes 0.5 * 1 + 0.5 * 2 = 1.5 s
    check(std::abs(a.graphState.layers[0].phase - 0.5f) < 1e-3f, "blend space samples share one phase");
    check(a.stateName() == "Loco" && a.clipName(sk) == "Loco", "state name");

    a.setParam("speed", 0.0f);
    a.update(sk, 0.0f);
    a.graphState.layers[0].phase = 0.4f; // only A plays: its event at 0.5 s fires on the way past
    a.update(sk, 0.2f);
    check(std::find(a.graphState.events.begin(), a.graphState.events.end(), "step") != a.graphState.events.end(), "clip event fires");
    a.update(sk, 0.2f);
    check(a.graphState.events.empty(), "events last one update");

    a.trigger("hit");
    a.update(sk, 0.01f);
    check(a.stateName() == "Hit" && a.param("hit") == 0.0f, "trigger moves to Hit and is consumed");
    a.update(sk, 0.5f);
    check(a.stateName() == "Hit", "Hit waits for its exit time");
    a.update(sk, 0.6f);
    check(a.stateName() == "Loco" && !a.finished(sk), "back to locomotion at the end of the clip");

    // Upper layer: C moves the spine to 5 but its root key (99) is outside the mask.
    a.setParam("aim", 1.0f);
    a.update(sk, 0.01f);
    engine::evaluatePose(sk, &a, pose);
    check(a.stateName(1) == "Aim" && std::abs(pose[1][3].x - 5.0f) < 1e-3f && std::abs(pose[0][3].x) < 1e-3f, "masked layer moves only the spine");
    check(std::abs(pose[2][3].x - 5.0f) < 1e-3f, "bones under the mask follow");
    a.setParam("aim", 0.0f);
    a.update(sk, 0.0f);  // leaves Aim
    a.update(sk, 0.25f); // fading out over 0.5 s: halfway
    engine::evaluatePose(sk, &a, pose);
    check(a.stateName(1) == "Off" && std::abs(pose[1][3].x - 2.5f) < 0.05f, "empty state fades the layer out");

    // Explicit clips override the graph; stop() hands control back.
    a.play(sk, "B", true, 1.0f, 0.0f);
    check(!a.graphActive() && std::abs(rootX() - 10.0f) < 1e-3f, "play() overrides the graph");
    a.stop();
    check(a.graphActive(), "stop() returns to the graph");

    // From any state to the 2D blend space; corner and centre weights.
    a.setParam("speed", 60.0f);
    a.setParam("x", 1.0f);
    a.setParam("y", 1.0f);
    a.update(sk, 0.0f);
    check(a.stateName() == "Grid" && std::abs(rootX() - 10.0f) < 1e-3f, "blend 2D at a corner plays that sample");
    a.setParam("x", 0.5f);
    a.setParam("y", 0.5f);
    check(std::abs(rootX() - 5.0f) < 1e-3f, "blend 2D in the centre mixes evenly");

    engine::Animator shared;
    auto cached = engine::loadAnimGraph(dir / "t.animgraph");
    check(cached && cached == engine::loadAnimGraph(dir / "t.animgraph"), "graphs are cached per path");
    check(engine::loadAnimGraph(ENGINE_TEST_ASSETS "/animgraphs/human.animgraph", &error) != nullptr, "human.animgraph loads: " + error);
    std::filesystem::remove_all(dir);
}

void testAnimIk()
{
    // Leg: hip at 1 m, knee bent slightly forward, foot on the ground; a target bone beside it.
    engine::Skeleton sk;
    sk.nodes = {{"Root", -1, {}, {1, 0, 0, 0}, {1, 1, 1}},
                {"Hip", 0, {0, 1, 0}, {1, 0, 0, 0}, {1, 1, 1}},
                {"Knee", 1, {0, -0.5f, 0.05f}, {1, 0, 0, 0}, {1, 1, 1}},
                {"Foot", 2, {0, -0.5f, -0.05f}, {1, 0, 0, 0}, {1, 1, 1}},
                {"Target", 0, {0.3f, 0.5f, 0}, {1, 0, 0, 0}, {1, 1, 1}}};
    auto clip = [](const char* name, glm::vec3 target) {
        engine::Skeleton::Clip c{name, 1.0f, {}};
        c.channels.push_back({4, engine::Skeleton::Path::Translation, false, {0.0f, 1.0f}, {glm::vec4(target, 0), glm::vec4(target, 0)}});
        return c;
    };
    sk.clips = {clip("Ref", {0.3f, 0.5f, 0}), clip("Moved", {0.3f, 0.7f, 0.2f})};
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "animik_test";
    std::filesystem::create_directories(dir);
    {
        std::ofstream f(dir / "ik.animgraph");
        f << "param \"grip\" float 0\nfoot_max_adjust 0.4\n"
             "ik foot \"Leg\" \"Hip\" \"Knee\" \"Foot\" 1 \"\" \"\" \"\" 0\n"
             "layer \"Base\"\nstate \"S\" clip\nsample \"Ref\"\n";
        std::ofstream h(dir / "hand.animgraph");
        h << "param \"grip\" float 0\n"
             "ik hand \"Grip\" \"Hip\" \"Knee\" \"Foot\" 1 \"grip\" \"Target\" \"Ref\" 0\n"
             "layer \"Base\"\nstate \"S\" clip\nsample \"Moved\"\n";
    }
    std::string error;
    auto graph = engine::loadAnimGraph(dir / "ik.animgraph", &error);
    check(graph && graph->ik.size() == 1 && graph->footMaxAdjust == 0.4f, "IK chains load: " + error);
    engine::AnimGraph copy;
    check(engine::saveAnimGraphFile(*graph, dir / "copy.animgraph") && engine::loadAnimGraphFile(copy, dir / "copy.animgraph") &&
              copy.ik.size() == 1 && copy.ik[0].end == "Foot" && copy.ik[0].kind == engine::AnimGraph::IkKind::Foot,
          "IK chains round trip");
    engine::Animator a;
    a.setGraph(graph);
    for (int i = 0; i < 20; ++i)
        a.update(sk, 0.05f); // eases the IK weight in
    std::vector<glm::mat4> pose;
    engine::evaluatePose(sk, &a, pose);
    check(std::abs(pose[3][3].y) < 1e-4f, "without ground input the foot stays animated");
    a.graphState.footIk = true;
    a.graphState.footLift = {0.2f};
    a.graphState.pelvisDrop = 0.0f;
    engine::evaluatePose(sk, &a, pose);
    const glm::vec3 hip(pose[1][3]), knee(pose[2][3]), foot(pose[3][3]);
    check(std::abs(foot.y - 0.2f) < 1e-3f && std::abs(foot.x) < 1e-3f, "foot IK lifts the foot onto a 20 cm step");
    check(std::abs(glm::length(knee - hip) - std::sqrt(0.25f + 0.0025f)) < 1e-3f &&
              std::abs(glm::length(foot - knee) - std::sqrt(0.25f + 0.0025f)) < 1e-3f && knee.z > 0.0f,
          "bone lengths kept, knee still bends forward");
    a.graphState.footLift = {0.0f};
    a.graphState.pelvisDrop = -0.15f;
    engine::evaluatePose(sk, &a, pose);
    check(std::abs(pose[1][3].y - 0.85f) < 1e-3f && std::abs(pose[3][3].y) < 1e-3f, "the body drops, the foot stays on its ground");

    // Hand: keeps the reference offset to Target (foot 1 m below and 0.3 m beside it) after Target moved.
    engine::Animator h;
    h.setGraph(engine::loadAnimGraph(dir / "hand.animgraph"));
    h.update(sk, 0.0f);
    engine::evaluatePose(sk, &h, pose);
    check(std::abs(pose[3][3].y) < 1e-3f, "hand IK off while its weight parameter is 0");
    h.setParam("grip", 1.0f);
    for (int i = 0; i < 10; ++i)
        h.update(sk, 0.05f);
    engine::evaluatePose(sk, &h, pose);
    const glm::vec3 want = glm::vec3(0.3f, 0.7f, 0.2f) + glm::vec3(-0.3f, -0.5f, 0.0f);
    check(glm::length(glm::vec3(pose[3][3]) - want) < 1e-3f, "hand IK keeps the grip on the moved target");
    std::filesystem::remove_all(dir);
}

void testRagdoll()
{
    // A soldier standing on the floor goes limp: the bodies fall, settle on the floor and stay together.
    auto s = flatFloor();
    engine::Entity e;
    e.position = {0, 0, 0};
    e.modelPath = ENGINE_TEST_ASSETS "/models/characters/humans/soldier.glb";
    e.model = engine::loadCharacterModel(e.modelPath);
    e.animGraphPath = std::filesystem::path(ENGINE_TEST_ASSETS "/animgraphs/human.animgraph").lexically_normal();
    e.animator.setGraph(engine::loadAnimGraph(e.animGraphPath));
    check(e.model && e.animator.graph && e.animator.graph->ragdoll.size() == 13, "human.animgraph lists 13 ragdoll bones");
    s.entities.push_back(e);
    engine::updateAnimations(s, 0.1f);
    engine::PhysicsWorld world;
    world.setLevel(s);
    engine::Ragdoll ragdoll;
    check(engine::startRagdoll(world, s.entities[0], ragdoll, glm::vec3(1.0f, 0.0f, 0.0f)) && world.ragdollCount() == 1, "ragdoll created");
    const engine::Skeleton& sk = *s.entities[0].model->skeleton;
    int pelvis = -1, head = -1;
    for (std::size_t n = 0; n < sk.nodes.size(); ++n) {
        if (sk.nodes[n].name == "Bip01 Pelvis")
            pelvis = static_cast<int>(n);
        if (sk.nodes[n].name == "Bip01 Head")
            head = static_cast<int>(n);
    }
    std::vector<glm::mat4> pose;
    check(engine::ragdollPose(world, ragdoll, sk, pose) && pose.size() == sk.nodes.size(), "ragdoll pose for every node");
    const glm::mat4 m = s.entities[0].modelTransform();
    const float headStart = glm::vec3(m * pose[static_cast<std::size_t>(head)][3]).y;
    check(std::abs(glm::length(glm::vec3(m * pose[static_cast<std::size_t>(head)][3]) - glm::vec3(m * ragdoll.start[static_cast<std::size_t>(head)][3]))) < 0.01f,
          "the ragdoll starts in the animated pose");
    for (int i = 0; i < 240; ++i)
        world.step(1.0f / 60.0f);
    check(engine::ragdollPose(world, ragdoll, sk, pose), "pose after falling");
    const glm::vec3 headEnd(m * pose[static_cast<std::size_t>(head)][3]), pelvisEnd(m * pose[static_cast<std::size_t>(pelvis)][3]);
    std::printf("  ragdoll: head %.2f -> %.2f m, pelvis at %.2f %.2f %.2f\n", headStart, headEnd.y, pelvisEnd.x, pelvisEnd.y, pelvisEnd.z);
    check(headStart > 1.4f && headEnd.y < 0.5f && pelvisEnd.y > -0.05f && pelvisEnd.y < 0.4f, "the body falls and lies on the floor");
    check(glm::distance(headEnd, pelvisEnd) < 1.2f && std::abs(pelvisEnd.x) < 3.0f && std::abs(pelvisEnd.z) < 3.0f, "the parts stay together");
    // Shots find the ragdoll and push it.
    const auto hit = world.raycast(pelvisEnd + glm::vec3(0, 2, 0), glm::vec3(0, -1, 0), 3.0f);
    check(hit && hit->ragdoll == ragdoll.handle && hit->surface == "flesh", "raycasts hit the ragdoll (flesh)");
    s.entities[0].animator.poseOverride = pose;
    std::vector<glm::mat4> drawn;
    engine::evaluatePose(sk, &s.entities[0].animator, drawn);
    check(drawn.size() == pose.size() && drawn[static_cast<std::size_t>(head)] == pose[static_cast<std::size_t>(head)], "the pose override is drawn");
    engine::stopRagdoll(world, ragdoll);
    check(world.ragdollCount() == 0, "ragdoll removed");
}

void testTaskGraph()
{
    engine::TaskGraph graph;
    std::mutex m;
    std::vector<std::string> order;
    const auto mainId = std::this_thread::get_id();
    std::atomic<bool> mainOk{true};
    auto log = [&](const char* name) {
        std::lock_guard lock(m);
        order.push_back(name);
    };
    const auto a = graph.add("a", [&] { log("a"); });
    const auto b = graph.add("b", [&] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); log("b"); });
    const auto c = graph.add("c", [&] {
        mainOk = mainOk && std::this_thread::get_id() == mainId;
        log("c");
    }, {a}, true);
    graph.add("d", [&] { log("d"); }, {b, c});
    for (int run = 0; run < 50; ++run) {
        order.clear();
        graph.run();
        auto pos = [&](const char* n) { return std::find(order.begin(), order.end(), n) - order.begin(); };
        if (order.size() != 4 || pos("a") > pos("c") || pos("d") != 3) {
            check(false, "task graph keeps dependencies");
            return;
        }
    }
    check(mainOk, "main-thread tasks run on the caller");
    check(graph.taskMs(b) >= 4.0, "task times are measured");
    // Many independent tasks finish (some on workers, the rest helped by the caller).
    engine::TaskGraph wide;
    std::atomic<int> count{0};
    for (int i = 0; i < 64; ++i)
        wide.add("w", [&] { ++count; });
    wide.run();
    check(count == 64, "wide graph runs every task once");
}

// Tests that load the real models, textures and levels (Git LFS). A checkout without LFS
// (the Windows and FreeBSD CI jobs) has pointer files instead; those tests are skipped there.
bool lfsAssetsPresent()
{
    std::ifstream in(ENGINE_TEST_ASSETS "/models/zone/vegetation/spruce_a_lod1.glb", std::ios::binary);
    char magic[4] = {};
    return in.read(magic, 4) && std::memcmp(magic, "glTF", 4) == 0;
}

int main()
{
    // Hermetic cook cache for the tests.
    engine::textureCookConfig().cacheDir = std::filesystem::current_path() / "texture_cache";
    std::filesystem::remove_all(engine::textureCookConfig().cacheDir);
    std::filesystem::remove_all(engine::textureCookConfig().cacheDir.parent_path() / "navmesh");
    const std::set<std::string> needAssets = {"testAnimatedCharacter",      "testEnemyAnimations", "testEnemyAnimGraph", "testRagdoll", "testHierarchyAndDependencies",
                                              "testLevelWithModel",         "testMaterialsAndTextures", "testPhysicsProps",
                                              "testSharedMeshes",           "testTexturedWeaponModel", "testWeaponFilesAndLoadout"};
    const bool assets = lfsAssetsPresent();
    if (!assets)
        std::printf("Git LFS assets are not checked out: skipping %zu tests that load them\n", needAssets.size());
    const std::pair<const char*, void (*)()> tests[] = {
        {"testTextureCooking", testTextureCooking},
        {"testSharedMeshes", testSharedMeshes},
        {"testTerrain", testTerrain},
        {"testFixedStep", testFixedStep},
        {"testFallsAndLands", testFallsAndLands},
        {"testWallBlocks", testWallBlocks},
        {"testClimbsSteps", testClimbsSteps},
        {"testJump", testJump},
        {"testSceneRoundTrip", testSceneRoundTrip},
        {"testGltfRamp", testGltfRamp},
        {"testLevelWithModel", testLevelWithModel},
        {"testRaycast", testRaycast},
        {"testModelCollisionFlags", testModelCollisionFlags},
        {"testScriptApi", testScriptApi},
        {"testScriptErrorIsReported", testScriptErrorIsReported},
        {"testEnemyChasesAndAttacks", testEnemyChasesAndAttacks},
        {"testEnemyBlockedBySight", testEnemyBlockedBySight},
        {"testWeaponFireRateAndReload", testWeaponFireRateAndReload},
        {"testShotHitsEnemyNotWallBehind", testShotHitsEnemyNotWallBehind},
        {"testEnemyDiesAndReactsToDamage", testEnemyDiesAndReactsToDamage},
        {"testTexturedWeaponModel", testTexturedWeaponModel},
        {"testWeaponFilesAndLoadout", testWeaponFilesAndLoadout},
        {"testSemiAutomatic", testSemiAutomatic},
        {"testKnifeAndGuns", testKnifeAndGuns},
        {"testHierarchyAndDependencies", testHierarchyAndDependencies},
        {"testJobSystem", testJobSystem},
        {"testRotateGizmo", testRotateGizmo},
        {"testRotatedBox", testRotatedBox},
        {"testScriptPropertiesFromEditor", testScriptPropertiesFromEditor},
        {"testPick", testPick},
        {"testAnimatedCharacter", testAnimatedCharacter},
        {"testEnemyAnimations", testEnemyAnimations},
        {"testScriptSyntaxCheck", testScriptSyntaxCheck},
        {"testMaterialsAndTextures", testMaterialsAndTextures},
        {"testAudio", testAudio},
        {"testNavMesh", testNavMesh},
        {"testObjectIds", testObjectIds},
        {"testCVars", testCVars},
        {"testLights", testLights},
        {"testScriptSaveRestore", testScriptSaveRestore},
        {"testLog", testLog},
        {"testEnsure", testEnsure},
        {"testSurfaces", testSurfaces},
        {"testAnomalies", testAnomalies},
        {"testPhysicsProps", testPhysicsProps},
        {"testPlantCollision", testPlantCollision},
        {"testThickets", testThickets},
        {"testEntityIO", testEntityIO},
        {"testTimeOfDay", testTimeOfDay},
        {"testAssetPalette", testAssetPalette},
        {"testWater", testWater},
        {"testWeatherCycle", testWeatherCycle},
        {"testMemoryBudgets", testMemoryBudgets},
        {"testAssetRegistry", testAssetRegistry},
        {"testHotReload", testHotReload},
        {"testPrefabs", testPrefabs},
        {"testRangeAllocator", testRangeAllocator},
        {"testImpostorSettings", testImpostorSettings},
        {"testVolumetricFogSettings", testVolumetricFogSettings},
        {"testInteriorVolumes", testInteriorVolumes},
        {"testProbes", testProbes},
        {"testMaterialInstances", testMaterialInstances},
        {"testVertexPaint", testVertexPaint},
        {"testParticleEvents", testParticleEvents},
        {"testDistortParticles", testDistortParticles},
        {"testTaskGraph", testTaskGraph},
        {"testAnimGraph", testAnimGraph},
        {"testAnimIk", testAnimIk},
        {"testRagdoll", testRagdoll},
        {"testEnemyAnimGraph", testEnemyAnimGraph},
        {"testFactions", testFactions},
        {"testALife", testALife},
        {"testFauna", testFauna},
        {"testAnomalyStorm", testAnomalyStorm},
        {"testQuestsAndDialogs", testQuestsAndDialogs},
        {"testTradeAndWear", testTradeAndWear},
        {"testScriptDebugger", testScriptDebugger},
        {"testDemo", testDemo},
        {"testMeshCache", testMeshCache},
        {"testVfs", testVfs},
        {"testLocalization", testLocalization},
        {"testThirdPartyLicenses", testThirdPartyLicenses},
        {"testAssetCredits", testAssetCredits},
        {"testWorldStreaming", testWorldStreaming},
    };
    for (const auto& [name, run] : tests)
        if (assets || !needAssets.count(name)) {
            // Name first and flushed: a test that crashes the process (Windows CI) is still named in the log.
            std::printf("== %s\n", name);
            std::fflush(stdout);
            run();
        }
    std::printf(failures ? "%d test(s) failed\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
