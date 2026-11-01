#include "engine/Scene.h"
#include <cmath>
#include <fstream>
#include "engine/JobSystem.h"

#include <algorithm>
#include <map>
#include <glm/gtc/matrix_transform.hpp>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <chrono>
#include <mutex>
#include <unordered_set>
#include <random>

namespace engine {

namespace {
std::filesystem::path pathFromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string pathToUtf8(const std::filesystem::path& p)
{
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

void setError(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
}
} // namespace

Scene makeDefaultScene()
{
    Scene s;
    s.playerStart = {0.0f, 0.0f, 8.0f};
    const glm::vec3 stone{0.62f, 0.60f, 0.56f};
    const glm::vec3 wall{0.48f, 0.52f, 0.58f};
    const glm::vec3 accent{0.80f, 0.45f, 0.20f};

    s.boxes.push_back({"Floor", {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f}, stone});
    s.boxes.push_back({"Wall North", {0.0f, 2.0f, -20.0f}, {40.0f, 4.0f, 1.0f}, wall});
    s.boxes.push_back({"Wall South", {0.0f, 2.0f, 20.0f}, {40.0f, 4.0f, 1.0f}, wall});
    s.boxes.push_back({"Wall West", {-20.0f, 2.0f, 0.0f}, {1.0f, 4.0f, 40.0f}, wall});
    s.boxes.push_back({"Wall East", {20.0f, 2.0f, 0.0f}, {1.0f, 4.0f, 40.0f}, wall});
    s.boxes.push_back({"Crate A", {-4.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, accent});
    s.boxes.push_back({"Crate B", {-4.0f, 1.5f, -1.2f}, {1.0f, 1.0f, 1.0f}, accent});
    s.boxes.push_back({"Pillar", {5.0f, 2.0f, -5.0f}, {1.5f, 4.0f, 1.5f}, wall});
    // A staircase of 0.3 m steps leading onto a platform.
    for (int i = 0; i < 6; ++i) {
        const float h = 0.3f * static_cast<float>(i + 1);
        s.boxes.push_back({"Step " + std::to_string(i + 1),
                           {8.0f, h * 0.5f, 6.0f - static_cast<float>(i) * 0.6f},
                           {3.0f, h, 0.6f}, stone});
    }
    s.boxes.push_back({"Platform", {8.0f, 0.9f, -1.8f}, {6.0f, 1.8f, 4.0f}, stone});
    return s;
}

bool saveScene(const Scene& scene, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        setError(error, "Cannot write " + file.string());
        return false;
    }
    out << std::setprecision(6);
    out << "# FPS engine level\nversion 6\n";
    out << "player_start " << scene.playerStart.x << ' ' << scene.playerStart.y << ' ' << scene.playerStart.z << '\n';
    out << "sun " << scene.sunDirection.x << ' ' << scene.sunDirection.y << ' ' << scene.sunDirection.z << '\n';
    const std::filesystem::path levelDir = std::filesystem::absolute(file).parent_path();
    auto relative = [&](const std::filesystem::path& p) {
        const std::filesystem::path rel = std::filesystem::absolute(p).lexically_relative(levelDir);
        return pathToUtf8(rel.empty() ? std::filesystem::absolute(p) : rel);
    };
    {
        const Environment& e = scene.environment;
        auto v3 = [&out](const char* key, glm::vec3 v) { out << "env " << key << ' ' << v.x << ' ' << v.y << ' ' << v.z << '\n'; };
        auto f1 = [&out](const char* key, float v) { out << "env " << key << ' ' << v << '\n'; };
        v3("sun_color", e.sunColor);
        f1("sun_intensity", e.sunIntensity);
        v3("sky_zenith", e.skyZenith);
        v3("sky_horizon", e.skyHorizon);
        v3("ground_color", e.groundColor);
        f1("cloud_cover", e.cloudCover);
        f1("ambient_intensity", e.ambientIntensity);
        v3("fog_color", e.fogColor);
        f1("fog_density", e.fogDensity);
        f1("fog_height", e.fogHeight);
        f1("fog_height_falloff", e.fogHeightFalloff);
        f1("fog_sun_scatter", e.fogSunScatter);
        f1("exposure", e.exposure);
        f1("bloom_intensity", e.bloomIntensity);
        f1("bloom_threshold", e.bloomThreshold);
        f1("ssao_radius", e.ssaoRadius);
        f1("ssao_intensity", e.ssaoIntensity);
        f1("shadow_distance", e.shadowDistance);
        f1("saturation", e.saturation);
        f1("contrast", e.contrast);
        v3("tint", e.tint);
        f1("vignette", e.vignette);
        f1("grain", e.grain);
        const Environment d;
        if (e.autoExposure != d.autoExposure || e.exposureMinEv != d.exposureMinEv || e.exposureMaxEv != d.exposureMaxEv ||
            e.adaptBrighten != d.adaptBrighten || e.adaptDarken != d.adaptDarken) {
            f1("auto_exposure", e.autoExposure);
            f1("exposure_min_ev", e.exposureMinEv);
            f1("exposure_max_ev", e.exposureMaxEv);
            f1("adapt_brighten", e.adaptBrighten);
            f1("adapt_darken", e.adaptDarken);
        }
        if (e.temperature != d.temperature)
            f1("temperature", e.temperature);
        if (e.greenMagenta != d.greenMagenta)
            f1("green_magenta", e.greenMagenta);
        if (e.lift != d.lift)
            v3("lift", e.lift);
        if (e.gamma != d.gamma)
            v3("gamma", e.gamma);
        if (e.gain != d.gain)
            v3("gain", e.gain);
        if (!e.lut.empty())
            out << "color_lut " << std::quoted(relative(e.lut)) << ' ' << e.lutStrength << '\n';
        f1("rain", e.rain);
        f1("wind", e.wind);
        if (e.storm != d.storm)
            f1("storm", e.storm);
        if (e.windDirection != d.windDirection || e.windGusts != d.windGusts) {
            f1("wind_direction", e.windDirection);
            f1("wind_gusts", e.windGusts);
        }
        if (e.puddles != d.puddles)
            f1("puddles", e.puddles);
        if (!e.thunderSound.empty())
            out << "thunder_sound " << std::quoted(relative(e.thunderSound)) << '\n';
        if (!e.rainSound.empty() || !e.windSound.empty())
            out << "weather_sounds " << std::quoted(e.rainSound.empty() ? std::string() : relative(e.rainSound)) << ' '
                << std::quoted(e.windSound.empty() ? std::string() : relative(e.windSound)) << '\n';
        if (!e.ambience.empty())
            out << "ambience " << std::quoted(relative(e.ambience)) << '\n';
        if (e.reverb != d.reverb || e.reverbAmount != d.reverbAmount)
            out << "reverb " << std::quoted(e.reverb) << ' ' << e.reverbAmount << '\n';
        const TimeOfDay& t = e.time;
        const TimeOfDay dt;
        if (t.enabled || t.cloudShadows != dt.cloudShadows || !t.keys.empty() || t.hour != dt.hour || t.dayMinutes != dt.dayMinutes)
            out << "time_of_day " << (t.enabled ? 1 : 0) << ' ' << t.hour << ' ' << t.dayMinutes << ' ' << t.latitude << ' '
                << t.dayOfYear << ' ' << t.north << ' ' << t.moonPhase << ' ' << t.nightBrightness << ' ' << t.cloudShadows << '\n';
        for (const WeatherKey& k : t.keys)
            out << "weather_key " << k.hour << ' ' << k.cloudCover << ' ' << k.fogDensity << ' ' << k.rain << ' ' << k.wind << ' '
                << k.exposure << ' ' << k.storm << '\n';
    }
    if (!scene.map.image.empty())
        out << "map_image " << std::quoted(relative(scene.map.image)) << '\n';
    if (scene.map.size > 0.0f)
        out << "map_bounds " << scene.map.min.x << ' ' << scene.map.min.y << ' ' << scene.map.size << '\n';
    if (scene.terrain && scene.terrain->valid()) {
        // Heights, splat map and foliage go to a binary file next to the level.
        std::filesystem::path data = file;
        data.replace_extension(".terrain");
        std::string terrainError;
        if (!saveTerrainData(*scene.terrain, data, &terrainError)) {
            setError(error, terrainError);
            return false;
        }
        out << "terrain " << std::quoted(pathToUtf8(data.filename())) << '\n';
        for (int i = 0; i < Terrain::kLayers; ++i) {
            const TerrainLayer& l = scene.terrain->layers[static_cast<std::size_t>(i)];
            if (!l.materialPath.empty() || i == 0)
                out << "terrain_layer " << i << ' ' << std::quoted(l.materialPath.empty() ? std::string() : relative(l.materialPath))
                    << ' ' << l.tiling << '\n';
        }
        for (const FoliageType& f : scene.terrain->foliageTypes) {
            out << "foliage_type " << std::quoted(f.modelPath.empty() ? std::string() : relative(f.modelPath)) << ' ' << f.density << ' ' << f.minScale << ' '
                << f.maxScale << ' ' << f.cullDistance << ' ' << f.collision << ' ' << f.alignToSlope << ' ' << f.castShadows << ' '
                << f.farDensity << ' ' << f.sway << ' ' << f.fade;
            for (float d : f.layerDensity)
                out << ' ' << d;
            out << '\n';
            for (const FoliageType::Lod& lod : f.lods)
                out << "foliage_lod " << std::quoted(relative(lod.modelPath)) << ' ' << lod.distance << '\n';
        }
    }
    if (!scene.loading.image.empty())
        out << "loading_image " << std::quoted(relative(scene.loading.image)) << '\n';
    if (!scene.loading.title.empty())
        out << "loading_title " << std::quoted(scene.loading.title) << '\n';
    if (!scene.loading.hint.empty())
        out << "loading_hint " << std::quoted(scene.loading.hint) << '\n';
    for (const Group& g : scene.groups)
        out << "group " << std::quoted(g.name) << ' ' << std::quoted(g.parent) << '\n';
    auto writeParent = [&out](const std::string& parent, ObjectId id = 0) {
        if (!parent.empty())
            out << "  parent " << std::quoted(parent) << '\n';
        if (id != 0)
            out << "  id " << objectIdToString(id) << '\n';
    };
    for (const Box& b : scene.boxes) {
        out << "box " << std::quoted(b.name) << ' '
            << b.position.x << ' ' << b.position.y << ' ' << b.position.z << ' '
            << b.size.x << ' ' << b.size.y << ' ' << b.size.z << ' '
            << b.color.r << ' ' << b.color.g << ' ' << b.color.b << '\n';
        writeParent(b.parent, b.id);
        if (b.rotated())
            out << "  rotation " << b.rotation.x << ' ' << b.rotation.y << ' ' << b.rotation.z << '\n';
        if (!b.materialPath.empty())
            out << "  material " << std::quoted(relative(b.materialPath)) << '\n';
        if (b.uvModified())
            out << "  uv " << b.uvScale.x << ' ' << b.uvScale.y << ' ' << b.uvOffset.x << ' ' << b.uvOffset.y << ' '
                << b.uvRotation << '\n';
    }
    for (const Model& m : scene.models) {
        std::filesystem::path rel = std::filesystem::absolute(m.sourcePath).lexically_relative(levelDir);
        if (rel.empty())
            rel = std::filesystem::absolute(m.sourcePath); // e.g. another drive on Windows
        out << "model " << std::quoted(m.name) << ' ' << std::quoted(pathToUtf8(rel)) << ' '
            << m.position.x << ' ' << m.position.y << ' ' << m.position.z << '\n';
        writeParent(m.parent, m.id);
        if (m.rotation != glm::vec3(0.0f) || m.scale != 1.0f)
            out << "  transform " << m.rotation.x << ' ' << m.rotation.y << ' ' << m.rotation.z << ' ' << m.scale << '\n';
        for (const auto& [slot, path] : m.materialOverrides)
            out << "  material_slot " << std::quoted(slot) << ' ' << std::quoted(relative(path)) << '\n';
        if (!m.lodPath.empty())
            out << "  lod " << std::quoted(relative(m.lodPath)) << ' ' << m.lodDistance << '\n';
        if (!m.lodPath.empty())
            for (const Model::FarLod& far : m.farLods)
                out << "  lod " << std::quoted(relative(far.path)) << ' ' << far.distance << '\n';
        if (m.cullDistance > 0.0f)
            out << "  cull_distance " << m.cullDistance << '\n';
        if (!m.collision)
            out << "  collision 0\n";
        if (!m.castShadows)
            out << "  cast_shadows 0\n";
        if (m.sway > 0.0f)
            out << "  sway " << m.sway << '\n';
        if (m.dynamic)
            out << "  physics " << m.massKg << ' ' << std::quoted(m.collider) << '\n';
    }
    const PlayerSettings& pl = scene.player;
    out << "player " << pl.walkSpeed << ' ' << pl.sprintSpeed << ' ' << pl.jumpSpeed << ' ' << pl.gravity << ' '
        << pl.airControl << ' ' << pl.height << ' ' << pl.eyeHeight << ' ' << pl.stepHeight << ' '
        << pl.maxSlopeDegrees << ' ' << pl.maxHealth << '\n';
    if (pl.swimSpeed != PlayerSettings{}.swimSpeed)
        out << "player_swim " << pl.swimSpeed << '\n';
    if (pl.radiationDamage != PlayerSettings{}.radiationDamage || pl.radiationDecay != PlayerSettings{}.radiationDecay)
        out << "player_radiation " << pl.radiationDamage << ' ' << pl.radiationDecay << '\n';
    {
        const PlayerSettings d;
        if (pl.flashlight != d.flashlight || pl.flashlightShadows != d.flashlightShadows || pl.flashlightColor != d.flashlightColor ||
            pl.flashlightIntensity != d.flashlightIntensity || pl.flashlightRange != d.flashlightRange ||
            pl.flashlightAngle != d.flashlightAngle || pl.flashlightLag != d.flashlightLag ||
            pl.flashlightBattery != d.flashlightBattery || pl.flashlightRecharge != d.flashlightRecharge)
            out << "player_flashlight " << pl.flashlight << ' ' << pl.flashlightShadows << ' ' << pl.flashlightColor.r << ' '
                << pl.flashlightColor.g << ' ' << pl.flashlightColor.b << ' ' << pl.flashlightIntensity << ' ' << pl.flashlightRange << ' '
                << pl.flashlightAngle << ' ' << pl.flashlightLag << ' ' << pl.flashlightBattery << ' ' << pl.flashlightRecharge << '\n';
    }
    {
        auto rel = [&](const std::filesystem::path& p) {
            if (p.empty())
                return std::string();
            const std::filesystem::path r = std::filesystem::absolute(p).lexically_relative(levelDir);
            return pathToUtf8(r.empty() ? std::filesystem::absolute(p) : r);
        };
        if (!pl.footstepSound.empty() || !pl.landSound.empty() || !pl.hurtSound.empty())
            out << "player_sounds " << std::quoted(rel(pl.footstepSound)) << ' ' << std::quoted(rel(pl.landSound)) << ' '
                << std::quoted(rel(pl.hurtSound)) << '\n';
    }
    {
        const NavSettings& n = scene.nav;
        out << "navmesh " << (n.enabled ? 1 : 0) << ' ' << n.cellSize << ' ' << n.cellHeight << ' ' << n.agentRadius << ' '
            << n.agentHeight << ' ' << n.agentClimb << ' ' << n.maxSlope << ' ' << n.tileSize << ' ' << n.minRegion << '\n';
    }
    for (const std::filesystem::path& w : scene.loadout) {
        const std::filesystem::path rel = std::filesystem::absolute(w).lexically_relative(levelDir);
        out << "weapon " << std::quoted(pathToUtf8(rel.empty() ? std::filesystem::absolute(w) : rel)) << '\n';
    }
    for (const Entity& e : scene.entities) {
        std::string script;
        if (!e.scriptPath.empty()) {
            std::filesystem::path rel = std::filesystem::absolute(e.scriptPath).lexically_relative(levelDir);
            script = pathToUtf8(rel.empty() ? std::filesystem::absolute(e.scriptPath) : rel);
        }
        out << "entity " << std::quoted(e.name) << ' ' << std::quoted(e.type) << ' '
            << e.position.x << ' ' << e.position.y << ' ' << e.position.z << ' ' << e.yawDegrees << ' '
            << e.size.x << ' ' << e.size.y << ' ' << e.size.z << ' '
            << e.color.r << ' ' << e.color.g << ' ' << e.color.b << ' ' << std::quoted(script) << '\n';
        writeParent(e.parent, e.id);
        if (!e.modelPath.empty()) {
            const std::filesystem::path rel = std::filesystem::absolute(e.modelPath).lexically_relative(levelDir);
            out << "  character " << std::quoted(pathToUtf8(rel.empty() ? std::filesystem::absolute(e.modelPath) : rel))
                << ' ' << e.modelScale << ' ' << e.modelYaw << ' ' << std::quoted(e.animation) << '\n';
        }
        for (const auto& [key, value] : e.properties)
            out << "  prop " << std::quoted(key) << ' ' << std::quoted(value) << '\n';
        for (const EntityOutput& o : e.outputs)
            out << "  output " << std::quoted(o.output) << ' ' << std::quoted(o.target) << ' ' << std::quoted(o.input) << ' '
                << std::quoted(o.parameter) << ' ' << o.delay << ' ' << (o.once ? 1 : 0) << '\n';
    }
    if (!out) {
        setError(error, "Write failed: " + file.string());
        return false;
    }
    return true;
}

LoadingScreen readLoadingScreen(const std::filesystem::path& file)
{
    LoadingScreen result;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key, value;
        ls >> key;
        if (key.rfind("loading_", 0) != 0 || !(ls >> std::quoted(value)))
            continue;
        if (key == "loading_image") {
            std::filesystem::path p = pathFromUtf8(value);
            result.image = p.is_relative() ? (std::filesystem::absolute(file).parent_path() / p).lexically_normal() : p;
        } else if (key == "loading_title") {
            result.title = value;
        } else if (key == "loading_hint") {
            result.hint = value;
        }
    }
    return result;
}

bool loadScene(Scene& scene, const std::filesystem::path& file, std::string* error, LoadProgress* progress)
{
    std::ifstream in(file);
    if (!in) {
        setError(error, "Cannot open " + file.string());
        return false;
    }
    Scene result;
    std::string line;
    int lineNo = 0;
    enum class LastObject { None, Box, Model, Entity } lastObject = LastObject::None;
    // Files used several times (fence sections, crates) are parsed once. All
    // model files of the level are read in parallel first (the slow part:
    // glTF parsing and texture decoding), then the lines are applied in order.
    std::map<std::filesystem::path, Model> loaded;
    {
        std::vector<std::filesystem::path> files;
        std::ifstream scan(file);
        std::string scanLine;
        while (std::getline(scan, scanLine)) {
            std::istringstream ls(scanLine);
            std::string key, name, path;
            ls >> key;
            if (key == "model")
                ls >> std::quoted(name) >> std::quoted(path);
            else if (key == "lod")
                ls >> std::quoted(path);
            else if (key == "foliage_type" || key == "foliage_lod")
                ls >> std::quoted(path);
            if (path.empty())
                continue;
            std::filesystem::path p = pathFromUtf8(path);
            if (p.is_relative())
                p = std::filesystem::absolute(file).parent_path() / p;
            p = p.lexically_normal();
            if (std::find(files.begin(), files.end(), p) == files.end())
                files.push_back(p);
        }
        std::vector<Model> models(files.size());
        std::vector<char> ok(files.size(), 0);
        if (progress) {
            progress->total = static_cast<int>(files.size()) + 2; // + level lines + physics/scripts afterwards
            progress->done = 0;
        }
        JobSystem::global().parallelFor(files.size(), 1, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                if (progress)
                    progress->setStep(pathToUtf8(files[i].filename()));
                ok[i] = loadModelFile(models[i], files[i]) ? 1 : 0;
                if (progress)
                    ++progress->done;
            }
        });
        for (std::size_t i = 0; i < files.size(); ++i)
            if (ok[i])
                loaded.emplace(files[i], std::move(models[i])); // failures are reported by the line that uses them
        if (progress)
            progress->setStep("level");
    }
    auto loadCached = [&loaded](Model& m, const std::filesystem::path& path, std::string* err) {
        if (const auto it = loaded.find(path); it != loaded.end()) {
            const std::string name = m.name;
            const glm::vec3 position = m.position;
            m = it->second;
            m.name = name;
            m.position = position;
            return true;
        }
        if (!loadModelFile(m, path, err))
            return false;
        loaded.emplace(path, m);
        return true;
    };
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#')
            continue;
        bool ok = true;
        if (key == "version") {
            int v = 0;
            ok = static_cast<bool>(ls >> v) && v >= 1 && v <= 6; // < 6: objects get IDs on load
        } else if (key == "env") {
            // Unknown environment keys are skipped so older editors can open newer levels.
            Environment& e = result.environment;
            std::string name;
            ok = static_cast<bool>(ls >> name);
            const std::map<std::string, glm::vec3*> vectors = {
                {"sun_color", &e.sunColor}, {"sky_zenith", &e.skyZenith}, {"sky_horizon", &e.skyHorizon},
                {"ground_color", &e.groundColor}, {"fog_color", &e.fogColor}, {"tint", &e.tint},
                {"lift", &e.lift}, {"gamma", &e.gamma}, {"gain", &e.gain}};
            const std::map<std::string, float*> floats = {
                {"sun_intensity", &e.sunIntensity}, {"cloud_cover", &e.cloudCover}, {"ambient_intensity", &e.ambientIntensity},
                {"fog_density", &e.fogDensity}, {"fog_height", &e.fogHeight}, {"fog_height_falloff", &e.fogHeightFalloff},
                {"fog_sun_scatter", &e.fogSunScatter}, {"exposure", &e.exposure}, {"bloom_intensity", &e.bloomIntensity},
                {"bloom_threshold", &e.bloomThreshold}, {"ssao_radius", &e.ssaoRadius}, {"ssao_intensity", &e.ssaoIntensity},
                {"shadow_distance", &e.shadowDistance}, {"saturation", &e.saturation}, {"contrast", &e.contrast},
                {"vignette", &e.vignette}, {"grain", &e.grain}, {"rain", &e.rain}, {"wind", &e.wind}, {"storm", &e.storm}, {"puddles", &e.puddles}, {"wind_direction", &e.windDirection}, {"wind_gusts", &e.windGusts},
                {"auto_exposure", &e.autoExposure}, {"exposure_min_ev", &e.exposureMinEv}, {"exposure_max_ev", &e.exposureMaxEv},
                {"adapt_brighten", &e.adaptBrighten}, {"adapt_darken", &e.adaptDarken}, {"temperature", &e.temperature},
                {"green_magenta", &e.greenMagenta}};
            if (ok) {
                if (auto it = vectors.find(name); it != vectors.end())
                    ok = static_cast<bool>(ls >> it->second->x >> it->second->y >> it->second->z);
                else if (auto f = floats.find(name); f != floats.end())
                    ok = static_cast<bool>(ls >> *f->second);
            }
        } else if (key == "ambience") {
            std::string path;
            ok = static_cast<bool>(ls >> std::quoted(path));
            if (ok && !path.empty()) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                result.environment.ambience = p.lexically_normal();
            }
        } else if (key == "time_of_day") {
            TimeOfDay& t = result.environment.time;
            int enabled = 0;
            ok = static_cast<bool>(ls >> enabled >> t.hour >> t.dayMinutes >> t.latitude >> t.dayOfYear >> t.north >> t.moonPhase >>
                                   t.nightBrightness >> t.cloudShadows);
            t.enabled = enabled != 0;
        } else if (key == "weather_key") {
            WeatherKey k;
            ok = static_cast<bool>(ls >> k.hour >> k.cloudCover >> k.fogDensity >> k.rain >> k.wind >> k.exposure);
            if (ok && !(ls >> k.storm)) // optional (older files)
                k.storm = 0.0f;
            if (ok)
                result.environment.time.keys.push_back(k);
        } else if (key == "thunder_sound") {
            std::string path;
            ok = static_cast<bool>(ls >> std::quoted(path));
            if (ok && !path.empty()) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                result.environment.thunderSound = p.lexically_normal();
            }
        } else if (key == "reverb") {
            ok = static_cast<bool>(ls >> std::quoted(result.environment.reverb) >> result.environment.reverbAmount);
        } else if (key == "color_lut") {
            std::string path;
            ok = static_cast<bool>(ls >> std::quoted(path) >> result.environment.lutStrength);
            if (ok && !path.empty()) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                result.environment.lut = p.lexically_normal();
            }
        } else if (key == "group") {
            Group g;
            ok = static_cast<bool>(ls >> std::quoted(g.name) >> std::quoted(g.parent));
            if (ok)
                result.groups.push_back(std::move(g));
        } else if (key == "map_image") {
            std::string value;
            ok = static_cast<bool>(ls >> std::quoted(value));
            std::filesystem::path p = pathFromUtf8(value);
            result.map.image = p.is_relative() ? (std::filesystem::absolute(file).parent_path() / p).lexically_normal() : p;
        } else if (key == "terrain") {
            std::string path;
            ok = static_cast<bool>(ls >> std::quoted(path));
            if (ok) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                if (!result.terrain)
                    result.terrain = std::make_shared<Terrain>();
                std::string terrainError;
                ok = loadTerrainData(*result.terrain, p.lexically_normal(), &terrainError);
                if (!ok) {
                    setError(error, terrainError);
                    return false;
                }
            }
        } else if (key == "terrain_layer") {
            int index = -1;
            std::string path;
            float tiling = 0.25f;
            ok = result.terrain && static_cast<bool>(ls >> index >> std::quoted(path) >> tiling) && index >= 0 &&
                 index < Terrain::kLayers;
            if (ok) {
                TerrainLayer& layer = result.terrain->layers[static_cast<std::size_t>(index)];
                layer.tiling = tiling;
                if (!path.empty()) {
                    std::filesystem::path p = pathFromUtf8(path);
                    if (p.is_relative())
                        p = std::filesystem::absolute(file).parent_path() / p;
                    layer.materialPath = p.lexically_normal();
                    layer.material = loadMaterial(layer.materialPath); // missing material: plain layer
                }
            }
        } else if (key == "foliage_type") {
            FoliageType ft;
            std::string path;
            ok = result.terrain && static_cast<bool>(ls >> std::quoted(path) >> ft.density >> ft.minScale >> ft.maxScale >>
                                                     ft.cullDistance >> ft.collision >> ft.alignToSlope);
            if (ok) {
                bool shadows = true; // optional (newer files)
                if (ls >> shadows)
                    ft.castShadows = shadows;
                float farDensity = 1.0f;
                if (ls >> farDensity)
                    ft.farDensity = std::clamp(farDensity, 0.0f, 1.0f);
                float sway = ft.collision ? 0.3f : 1.0f; // older files: trees stiffer than grass
                if (ls >> sway)
                    ft.sway = std::max(0.0f, sway);
                else
                    ft.sway = sway;
                float fade = 0.15f;
                if (ls >> fade)
                    ft.fade = std::clamp(fade, 0.0f, 1.0f);
                for (float& d : ft.layerDensity) {
                    float v = 1.0f;
                    if (!(ls >> v))
                        break;
                    d = std::max(0.0f, v);
                }
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                if (!path.empty()) {
                    ft.modelPath = p.lexically_normal();
                    auto model = std::make_shared<Model>();
                    if (loadCached(*model, ft.modelPath, nullptr))
                        ft.model = std::move(model);
                }
                result.terrain->foliageTypes.push_back(std::move(ft));
            }
        } else if (key == "foliage_lod") {
            // LOD level of the foliage type on the line above.
            std::string path;
            FoliageType::Lod lod;
            ok = result.terrain && !result.terrain->foliageTypes.empty() && static_cast<bool>(ls >> std::quoted(path) >> lod.distance);
            if (ok) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                lod.modelPath = p.lexically_normal();
                auto model = std::make_shared<Model>();
                if (loadCached(*model, lod.modelPath, nullptr))
                    lod.model = std::move(model);
                result.terrain->foliageTypes.back().lods.push_back(std::move(lod));
            }
        } else if (key == "map_bounds") {
            ok = static_cast<bool>(ls >> result.map.min.x >> result.map.min.y >> result.map.size);
        } else if (key == "loading_image" || key == "loading_title" || key == "loading_hint") {
            std::string value;
            ok = static_cast<bool>(ls >> std::quoted(value));
            if (key == "loading_image") {
                std::filesystem::path p = pathFromUtf8(value);
                result.loading.image = p.is_relative() ? (std::filesystem::absolute(file).parent_path() / p).lexically_normal() : p;
            } else if (key == "loading_title") {
                result.loading.title = value;
            } else {
                result.loading.hint = value;
            }
        } else if (key == "cull_distance") {
            ok = lastObject == LastObject::Model && static_cast<bool>(ls >> result.models.back().cullDistance);
        } else if (key == "physics") {
            ok = lastObject == LastObject::Model;
            if (ok) {
                Model& m = result.models.back();
                ok = static_cast<bool>(ls >> m.massKg >> std::quoted(m.collider));
                m.dynamic = ok;
            }
        } else if (key == "sway") {
            ok = lastObject == LastObject::Model && static_cast<bool>(ls >> result.models.back().sway);
        } else if (key == "cast_shadows") {
            ok = lastObject == LastObject::Model && static_cast<bool>(ls >> result.models.back().castShadows);
        } else if (key == "collision") {
            ok = lastObject == LastObject::Model && static_cast<bool>(ls >> result.models.back().collision);
        } else if (key == "lod") {
            // Distance LOD of the model on the line above.
            std::string path;
            ok = lastObject == LastObject::Model && static_cast<bool>(ls >> std::quoted(path));
            if (ok) {
                Model& m = result.models.back();
                float distance = m.lodPath.empty() ? 80.0f : std::max(m.lodDistance, m.farLods.empty() ? 0.0f : m.farLods.back().distance) * 2.0f;
                ls >> distance;
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                auto lod = std::make_shared<Model>();
                std::string modelError;
                if (!m.lodPath.empty()) { // second and later `lod` lines: farther levels
                    Model::FarLod far;
                    far.path = p.lexically_normal();
                    far.distance = distance;
                    if (loadCached(*lod, far.path, &modelError))
                        far.model = std::move(lod);
                    m.farLods.push_back(std::move(far));
                } else {
                    m.lodDistance = distance;
                    m.lodPath = p.lexically_normal();
                    if (loadCached(*lod, m.lodPath, &modelError))
                        m.lod = std::move(lod);
                }
                // A missing LOD file is not fatal: the full model is drawn at every distance.
            }
        } else if (key == "transform") {
            ok = lastObject == LastObject::Model;
            if (ok) {
                Model& m = result.models.back();
                ok = static_cast<bool>(ls >> m.rotation.x >> m.rotation.y >> m.rotation.z >> m.scale);
            }
        } else if (key == "id") {
            // Persistent ID of the object on the line above.
            std::string text;
            ok = static_cast<bool>(ls >> text) && lastObject != LastObject::None;
            if (ok) {
                const ObjectId id = objectIdFromString(text);
                if (lastObject == LastObject::Box)
                    result.boxes.back().id = id;
                else if (lastObject == LastObject::Model)
                    result.models.back().id = id;
                else
                    result.entities.back().id = id;
            }
        } else if (key == "parent") {
            // Hierarchy parent of the object on the line above.
            std::string parent;
            ok = static_cast<bool>(ls >> std::quoted(parent)) && lastObject != LastObject::None;
            if (ok) {
                if (lastObject == LastObject::Box)
                    result.boxes.back().parent = parent;
                else if (lastObject == LastObject::Model)
                    result.models.back().parent = parent;
                else
                    result.entities.back().parent = parent;
            }
        } else if (key == "rotation") {
            // Rotation of the box on the line above.
            ok = lastObject == LastObject::Box;
            if (ok) {
                glm::vec3& r = result.boxes.back().rotation;
                ok = static_cast<bool>(ls >> r.x >> r.y >> r.z);
            }
        } else if (key == "uv") {
            // Texture placement of the box on the line above.
            ok = lastObject == LastObject::Box;
            if (ok) {
                Box& b = result.boxes.back();
                ok = static_cast<bool>(ls >> b.uvScale.x >> b.uvScale.y >> b.uvOffset.x >> b.uvOffset.y >> b.uvRotation);
            }
        } else if (key == "player_start") {
            ok = static_cast<bool>(ls >> result.playerStart.x >> result.playerStart.y >> result.playerStart.z);
        } else if (key == "sun") {
            ok = static_cast<bool>(ls >> result.sunDirection.x >> result.sunDirection.y >> result.sunDirection.z);
        } else if (key == "box") {
            Box b;
            ok = static_cast<bool>(ls >> std::quoted(b.name)
                                      >> b.position.x >> b.position.y >> b.position.z
                                      >> b.size.x >> b.size.y >> b.size.z
                                      >> b.color.r >> b.color.g >> b.color.b);
            if (ok) {
                result.boxes.push_back(std::move(b));
                lastObject = LastObject::Box;
            }
        } else if (key == "model") {
            Model m;
            std::string path;
            ok = static_cast<bool>(ls >> std::quoted(m.name) >> std::quoted(path)
                                      >> m.position.x >> m.position.y >> m.position.z);
            if (ok) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                std::string modelError;
                if (!loadCached(m, p.lexically_normal(), &modelError)) {
                    setError(error, file.string() + ":" + std::to_string(lineNo) + ": " + modelError);
                    return false;
                }
                result.models.push_back(std::move(m));
                lastObject = LastObject::Model;
            }
        } else if (key == "player") {
            PlayerSettings& pl = result.player;
            ok = static_cast<bool>(ls >> pl.walkSpeed >> pl.sprintSpeed >> pl.jumpSpeed >> pl.gravity >> pl.airControl
                                      >> pl.height >> pl.eyeHeight >> pl.stepHeight >> pl.maxSlopeDegrees >> pl.maxHealth);
        } else if (key == "player_swim") {
            ok = static_cast<bool>(ls >> result.player.swimSpeed);
        } else if (key == "player_radiation") {
            ok = static_cast<bool>(ls >> result.player.radiationDamage >> result.player.radiationDecay);
        } else if (key == "player_flashlight") {
            PlayerSettings& pl = result.player;
            ok = static_cast<bool>(ls >> pl.flashlight >> pl.flashlightShadows >> pl.flashlightColor.r >> pl.flashlightColor.g >>
                                   pl.flashlightColor.b >> pl.flashlightIntensity >> pl.flashlightRange >> pl.flashlightAngle >>
                                   pl.flashlightLag >> pl.flashlightBattery >> pl.flashlightRecharge);
        } else if (key == "navmesh") {
            NavSettings& n = result.nav;
            int enabled = 1;
            ok = static_cast<bool>(ls >> enabled >> n.cellSize >> n.cellHeight >> n.agentRadius >> n.agentHeight >> n.agentClimb >>
                                   n.maxSlope >> n.tileSize >> n.minRegion);
            n.enabled = enabled != 0;
        } else if (key == "weather_sounds") {
            std::string paths[2];
            ok = static_cast<bool>(ls >> std::quoted(paths[0]) >> std::quoted(paths[1]));
            std::filesystem::path* slots[] = {&result.environment.rainSound, &result.environment.windSound};
            for (int i = 0; ok && i < 2; ++i) {
                if (paths[i].empty())
                    continue;
                std::filesystem::path p = pathFromUtf8(paths[i]);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                *slots[i] = p.lexically_normal();
            }
        } else if (key == "player_sounds") {
            std::string paths[3];
            ok = static_cast<bool>(ls >> std::quoted(paths[0]) >> std::quoted(paths[1]) >> std::quoted(paths[2]));
            std::filesystem::path* slots[] = {&result.player.footstepSound, &result.player.landSound, &result.player.hurtSound};
            for (int i = 0; ok && i < 3; ++i) {
                if (paths[i].empty())
                    continue;
                std::filesystem::path p = pathFromUtf8(paths[i]);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                *slots[i] = p.lexically_normal();
            }
        } else if (key == "output") {
            EntityOutput o;
            int once = 0;
            ok = static_cast<bool>(ls >> std::quoted(o.output) >> std::quoted(o.target) >> std::quoted(o.input) >> std::quoted(o.parameter) >>
                                   o.delay >> once) &&
                 lastObject == LastObject::Entity;
            if (ok) {
                o.once = once != 0;
                result.entities.back().outputs.push_back(std::move(o));
            }
        } else if (key == "prop") {
            std::string k, v;
            ok = static_cast<bool>(ls >> std::quoted(k) >> std::quoted(v)) && !result.entities.empty();
            if (ok)
                result.entities.back().properties[k] = v;
        } else if (key == "material" || key == "material_slot") {
            // Box material, or glTF material override of the last model.
            std::string slot, path;
            if (key == "material_slot")
                ok = static_cast<bool>(ls >> std::quoted(slot)) && !result.models.empty();
            else
                ok = !result.boxes.empty();
            ok = ok && static_cast<bool>(ls >> std::quoted(path));
            if (ok) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                p = p.lexically_normal();
                std::string materialError;
                auto material = loadMaterial(p, &materialError);
                if (!material) {
                    setError(error, file.string() + ":" + std::to_string(lineNo) + ": " + materialError);
                    return false;
                }
                if (key == "material") {
                    result.boxes.back().materialPath = p;
                    result.boxes.back().material = std::move(material);
                } else {
                    result.models.back().materialOverrides[slot] = p;
                    result.models.back().overrideMaterials[slot] = std::move(material);
                }
            }
        } else if (key == "character") {
            std::string path;
            ok = !result.entities.empty();
            Entity* e = ok ? &result.entities.back() : nullptr;
            ok = ok && static_cast<bool>(ls >> std::quoted(path) >> e->modelScale >> e->modelYaw >> std::quoted(e->animation));
            if (ok) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                e->modelPath = p.lexically_normal();
                std::string modelError;
                e->model = loadCharacterModel(e->modelPath, &modelError);
                if (!e->model) {
                    setError(error, file.string() + ":" + std::to_string(lineNo) + ": " + modelError);
                    return false;
                }
            }
        } else if (key == "weapon") {
            std::string path;
            ok = static_cast<bool>(ls >> std::quoted(path));
            if (ok) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                result.loadout.push_back(p.lexically_normal());
            }
        } else if (key == "entity") {
            Entity e;
            std::string script;
            ok = static_cast<bool>(ls >> std::quoted(e.name) >> std::quoted(e.type)
                                      >> e.position.x >> e.position.y >> e.position.z >> e.yawDegrees
                                      >> e.size.x >> e.size.y >> e.size.z
                                      >> e.color.r >> e.color.g >> e.color.b >> std::quoted(script));
            if (ok) {
                if (!script.empty()) {
                    std::filesystem::path p = pathFromUtf8(script);
                    if (p.is_relative())
                        p = std::filesystem::absolute(file).parent_path() / p;
                    e.scriptPath = p.lexically_normal();
                }
                result.entities.push_back(std::move(e));
                lastObject = LastObject::Entity;
            }
        } else {
            ok = false;
        }
        if (!ok) {
            setError(error, file.string() + ":" + std::to_string(lineNo) + ": invalid line: " + line);
            return false;
        }
    }
    if (progress)
        ++progress->done;
    assignObjectIds(result); // levels before version 6, objects added by hand
    scene = std::move(result);
    return true;
}

ObjectId newObjectId()
{
    static std::mutex mutex;
    static std::mt19937_64 rng = [] {
        std::random_device rd;
        std::seed_seq seed{rd(), rd(), rd(), rd(),
                           static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count())};
        return std::mt19937_64(seed);
    }();
    std::lock_guard lock(mutex);
    ObjectId id = 0;
    while (id == 0)
        id = rng();
    return id;
}

std::string objectIdToString(ObjectId id)
{
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(id));
    return buf;
}

ObjectId objectIdFromString(const std::string& text)
{
    if (text.empty() || text.size() > 16)
        return 0;
    ObjectId id = 0;
    for (char c : text) {
        int v;
        if (c >= '0' && c <= '9')
            v = c - '0';
        else if (c >= 'a' && c <= 'f')
            v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            v = c - 'A' + 10;
        else
            return 0;
        id = (id << 4) | static_cast<ObjectId>(v);
    }
    return id;
}

int assignObjectIds(Scene& scene)
{
    std::unordered_set<ObjectId> used;
    used.reserve(static_cast<std::size_t>(scene.objectCount()) * 2);
    int changed = 0;
    auto fix = [&](ObjectId& id) {
        if (id == 0 || !used.insert(id).second) {
            do
                id = newObjectId();
            while (!used.insert(id).second);
            ++changed;
        }
    };
    for (Box& b : scene.boxes)
        fix(b.id);
    for (Model& m : scene.models)
        fix(m.id);
    for (Entity& e : scene.entities)
        fix(e.id);
    return changed;
}

int findObjectById(const Scene& scene, ObjectId id)
{
    if (id == 0)
        return -1;
    for (std::size_t i = 0; i < scene.boxes.size(); ++i)
        if (scene.boxes[i].id == id)
            return static_cast<int>(i);
    for (std::size_t i = 0; i < scene.models.size(); ++i)
        if (scene.models[i].id == id)
            return scene.firstModel() + static_cast<int>(i);
    for (std::size_t i = 0; i < scene.entities.size(); ++i)
        if (scene.entities[i].id == id)
            return scene.firstEntity() + static_cast<int>(i);
    return -1;
}

glm::mat4 Model::transform() const
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m = glm::rotate(m, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::rotate(m, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(m, glm::vec3(scale));
}

glm::mat4 Box::transform() const
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m = glm::rotate(m, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::rotate(m, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(m, size);
}

Aabb Box::bounds() const
{
    if (!rotated())
        return Aabb::fromCenterSize(position, size);
    const glm::mat4 m = transform();
    Aabb box{glm::vec3(1e30f), glm::vec3(-1e30f)};
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 p = glm::vec3(m * glm::vec4((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.0f));
        box.min = glm::min(box.min, p);
        box.max = glm::max(box.max, p);
    }
    return box;
}

const Model& Model::lodFor(float distance, float lodScale) const
{
    const Model* pick = this;
    if (lod && distance > lodDistance * lodScale)
        pick = lod.get();
    if (pick != this)
        for (const FarLod& far : farLods)
            if (far.model && distance > far.distance * lodScale)
                pick = far.model.get();
    return *pick;
}

Aabb Model::bounds() const
{
    if (rotation == glm::vec3(0.0f) && scale == 1.0f)
        return {localBounds.min + position, localBounds.max + position};
    const glm::mat4 m = transform();
    Aabb box{glm::vec3(1e30f), glm::vec3(-1e30f)};
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner((i & 1) ? localBounds.max.x : localBounds.min.x, (i & 2) ? localBounds.max.y : localBounds.min.y,
                               (i & 4) ? localBounds.max.z : localBounds.min.z);
        const glm::vec3 p = glm::vec3(m * glm::vec4(corner, 1.0f));
        box.min = glm::min(box.min, p);
        box.max = glm::max(box.max, p);
    }
    return box;
}

glm::mat4 Entity::modelTransform() const
{
    if (!model)
        return glm::mat4(1.0f);
    const Aabb& b = model->localBounds;
    const float height = std::max(b.max.y - b.min.y, 1e-4f);
    const float fit = size.y / height * modelScale;
    const glm::vec3 center = (b.min + b.max) * 0.5f;
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    // Entity yaw 0 faces +X; glTF characters face +Z, hence the extra 90 degrees.
    m = glm::rotate(m, glm::radians(90.0f - yawDegrees + modelYaw), glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::scale(m, glm::vec3(fit));
    return glm::translate(m, glm::vec3(-center.x, -b.min.y, -center.z));
}

void updateAnimations(Scene& scene, float dt)
{
    for (Entity& e : scene.entities) {
        e.flash = std::max(0.0f, e.flash - dt);
        if (!e.model || !e.model->skeleton)
            continue;
        const Skeleton& skeleton = *e.model->skeleton;
        if (e.animator.current.clip < 0 && !e.animation.empty())
            e.animator.play(skeleton, e.animation, true, 1.0f, 0.0f);
        e.animator.update(skeleton, dt);
    }
}

Aabb Scene::objectBounds(int object) const
{
    if (isBox(object))
        return boxes[static_cast<std::size_t>(object)].bounds();
    if (isModel(object))
        return model(object).bounds();
    if (isEntity(object))
        return entity(object).markerBounds();
    return {};
}

int pickObject(const Scene& scene, glm::vec3 origin, glm::vec3 dir)
{
    int best = -1;
    float bestT = 0.0f;
    // Entities first: they are small and usually stand inside larger geometry.
    for (int i = scene.objectCount() - 1; i >= 0; --i) {
        std::optional<float> t;
        if (scene.isBox(i) && scene.boxes[static_cast<std::size_t>(i)].rotated()) {
            // Ray in the box's own space against the unit cube; distances stay in world units.
            const glm::mat4 inv = glm::inverse(scene.boxes[static_cast<std::size_t>(i)].transform());
            const glm::vec3 o = glm::vec3(inv * glm::vec4(origin, 1.0f));
            const glm::vec3 d = glm::vec3(inv * glm::vec4(dir, 0.0f));
            if (auto local = rayAabb(o, d, Aabb{glm::vec3(-0.5f), glm::vec3(0.5f)}))
                t = *local; // d is not normalized, so *local is the parameter along the world ray
        } else {
            t = rayAabb(origin, dir, scene.objectBounds(i));
        }
        if (t && (best < 0 || *t < bestT - 1e-4f)) {
            best = i;
            bestT = *t;
        }
    }
    return best;
}

void mapArea(const Scene& scene, glm::vec2& min, float& size)
{
    if (scene.map.size > 0.0f) {
        min = scene.map.min;
        size = scene.map.size;
        return;
    }
    glm::vec2 lo(1e30f), hi(-1e30f);
    for (int i = 0; i < scene.objectCount(); ++i) {
        if (scene.isEntity(i) && scene.entity(i).helper())
            continue; // sound zones and markers do not size the map
        const Aabb b = scene.objectBounds(i);
        lo = glm::min(lo, glm::vec2(b.min.x, b.min.z));
        hi = glm::max(hi, glm::vec2(b.max.x, b.max.z));
    }
    if (lo.x > hi.x) {
        lo = glm::vec2(-50.0f);
        hi = glm::vec2(50.0f);
    }
    size = std::max(hi.x - lo.x, hi.y - lo.y) * 1.08f + 10.0f;
    min = (lo + hi) * 0.5f - glm::vec2(size * 0.5f);
}

std::filesystem::path findLodFile(const std::filesystem::path& file)
{
    std::filesystem::path lod = file;
    lod.replace_filename(file.stem().u8string() + u8"_lod1" + file.extension().u8string());
    std::error_code ec;
    return std::filesystem::exists(lod, ec) ? lod : std::filesystem::path();
}

} // namespace engine
