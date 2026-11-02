#include "engine/Scene.h"
#include "engine/Vfs.h"
#include "engine/Probes.h"
#include <cmath>
#include <fstream>
#include "engine/JobSystem.h"
#include "engine/Log.h"
#include "engine/Prefab.h"
#include "engine/VertexPaint.h"

#include <algorithm>
#include <array>
#include <map>
#include <glm/gtc/matrix_transform.hpp>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <chrono>
#include <mutex>
#include <unordered_set>
#include <set>
#include <type_traits>
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

namespace {
std::string relativeTo(const std::filesystem::path& p, const std::filesystem::path& levelDir)
{
    const std::filesystem::path rel = std::filesystem::absolute(p).lexically_relative(levelDir);
    return pathToUtf8(rel.empty() ? std::filesystem::absolute(p) : rel); // e.g. another drive on Windows
}

void writeIdAndParent(std::ostream& out, const std::string& parent, ObjectId id)
{
    if (!parent.empty())
        out << "  parent " << std::quoted(parent) << '\n';
    if (id != 0)
        out << "  id " << objectIdToString(id) << '\n';
}
} // namespace

void writeBox(std::ostream& out, const Box& b, const std::filesystem::path& levelDir)
{
    out << "box " << std::quoted(b.name) << ' '
        << b.position.x << ' ' << b.position.y << ' ' << b.position.z << ' '
        << b.size.x << ' ' << b.size.y << ' ' << b.size.z << ' '
        << b.color.r << ' ' << b.color.g << ' ' << b.color.b << '\n';
    writeIdAndParent(out, b.parent, b.id);
    if (b.rotated())
        out << "  rotation " << b.rotation.x << ' ' << b.rotation.y << ' ' << b.rotation.z << '\n';
    if (!b.materialPath.empty())
        out << "  material " << std::quoted(relativeTo(b.materialPath, levelDir)) << '\n';
    if (b.uvModified())
        out << "  uv " << b.uvScale.x << ' ' << b.uvScale.y << ' ' << b.uvOffset.x << ' ' << b.uvOffset.y << ' '
            << b.uvRotation << '\n';
}

void writeModel(std::ostream& out, const Model& m, const std::filesystem::path& levelDir, bool withBounds)
{
    auto relative = [&](const std::filesystem::path& p) { return relativeTo(p, levelDir); };
    out << "model " << std::quoted(m.name) << ' ' << std::quoted(relative(m.sourcePath)) << ' '
        << m.position.x << ' ' << m.position.y << ' ' << m.position.z << '\n';
    writeIdAndParent(out, m.parent, m.id);
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
    if (m.impostorDistance > 0.0f)
        out << "  impostor " << m.impostorDistance << '\n';
    if (m.dynamic)
        out << "  physics " << m.massKg << ' ' << std::quoted(m.collider) << '\n';
    if (m.paint && !m.paint->empty())
        for (std::size_t i = 0; i < m.paint->parts.size(); ++i)
            out << "  paint " << i << ' ' << encodeVertexPaint(m.paint->parts[i]) << '\n';
    if (withBounds && m.localBounds.min != m.localBounds.max) // lets the game defer far models
        out << "  bounds " << m.localBounds.min.x << ' ' << m.localBounds.min.y << ' ' << m.localBounds.min.z << ' '
            << m.localBounds.max.x << ' ' << m.localBounds.max.y << ' ' << m.localBounds.max.z << '\n';
}

void writeEntity(std::ostream& out, const Entity& e, const std::filesystem::path& levelDir)
{
    const std::string script = e.scriptPath.empty() ? std::string() : relativeTo(e.scriptPath, levelDir);
    out << "entity " << std::quoted(e.name) << ' ' << std::quoted(e.type) << ' '
        << e.position.x << ' ' << e.position.y << ' ' << e.position.z << ' ' << e.yawDegrees << ' '
        << e.size.x << ' ' << e.size.y << ' ' << e.size.z << ' '
        << e.color.r << ' ' << e.color.g << ' ' << e.color.b << ' ' << std::quoted(script) << '\n';
    writeIdAndParent(out, e.parent, e.id);
    if (!e.modelPath.empty())
        out << "  character " << std::quoted(relativeTo(e.modelPath, levelDir))
            << ' ' << e.modelScale << ' ' << e.modelYaw << ' ' << std::quoted(e.animation) << '\n';
    if (!e.modelPath.empty() && !e.animGraphPath.empty())
        out << "  animgraph " << std::quoted(relativeTo(e.animGraphPath, levelDir)) << '\n';
    for (const auto& [key, value] : e.properties)
        out << "  prop " << std::quoted(key) << ' ' << std::quoted(value) << '\n';
    for (const EntityOutput& o : e.outputs)
        out << "  output " << std::quoted(o.output) << ' ' << std::quoted(o.target) << ' ' << std::quoted(o.input) << ' '
            << std::quoted(o.parameter) << ' ' << o.delay << ' ' << (o.once ? 1 : 0) << '\n';
}

namespace {
// Everything of a level that is not an object (a .prefab file has only objects).
bool writeLevelSettings(std::ostream& out, const Scene& scene, const std::filesystem::path& file, const std::filesystem::path& levelDir,
                        std::string* error)
{
    out << "player_start " << scene.playerStart.x << ' ' << scene.playerStart.y << ' ' << scene.playerStart.z << '\n';
    out << "sun " << scene.sunDirection.x << ' ' << scene.sunDirection.y << ' ' << scene.sunDirection.z << '\n';
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
        if (e.fogVolumetricRange != d.fogVolumetricRange || e.fogLightShafts != d.fogLightShafts ||
            e.fogAnisotropy != d.fogAnisotropy || e.fogNoise != d.fogNoise || e.fogLocalLights != d.fogLocalLights) {
            f1("fog_volumetric_range", e.fogVolumetricRange);
            f1("fog_light_shafts", e.fogLightShafts);
            f1("fog_anisotropy", e.fogAnisotropy);
            f1("fog_noise", e.fogNoise);
            f1("fog_local_lights", e.fogLocalLights);
        }
        if (e.ssrIntensity != d.ssrIntensity)
            f1("ssr_intensity", e.ssrIntensity);
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
        if (const AnomalyStormSettings& a = e.anomalyStorm; a != AnomalyStormSettings{})
            out << "anomaly_storm " << (a.enabled ? 1 : 0) << ' ' << a.minMinutes << ' ' << a.maxMinutes << ' ' << a.warningSeconds << ' '
                << a.durationSeconds << ' ' << a.damage << ' ' << a.npcDamage << ' ' << a.tint.x << ' ' << a.tint.y << ' ' << a.tint.z
                << '\n';
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
    {
        const WeatherCycle& w = scene.environment.weather;
        if (w != WeatherCycle{}) {
            out << "weather_cycle " << (w.enabled ? 1 : 0) << ' ' << w.minMinutes << ' ' << w.maxMinutes << ' ' << w.transitionMinutes
                << ' ' << std::quoted(w.start) << '\n';
            for (const WeatherPreset& p : w.presets)
                out << "weather_preset " << std::quoted(p.name) << ' ' << p.cloudCover << ' ' << p.fogDensity << ' ' << p.rain << ' '
                    << p.wind << ' ' << p.storm << ' ' << p.exposure << ' ' << p.chance << '\n';
        }
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
            if (f.impostorDistance > 0.0f)
                out << "foliage_impostor " << f.impostorDistance << '\n';
        }
    }
    if (!scene.loading.image.empty())
        out << "loading_image " << std::quoted(relative(scene.loading.image)) << '\n';
    if (!scene.loading.title.empty())
        out << "loading_title " << std::quoted(scene.loading.title) << '\n';
    if (!scene.loading.hint.empty())
        out << "loading_hint " << std::quoted(scene.loading.hint) << '\n';
    return true;
}

void writeLevelPlayer(std::ostream& out, const Scene& scene, const std::filesystem::path& levelDir)
{
    const PlayerSettings& pl = scene.player;
    out << "player " << pl.walkSpeed << ' ' << pl.sprintSpeed << ' ' << pl.jumpSpeed << ' ' << pl.gravity << ' '
        << pl.airControl << ' ' << pl.height << ' ' << pl.eyeHeight << ' ' << pl.stepHeight << ' '
        << pl.maxSlopeDegrees << ' ' << pl.maxHealth << '\n';
    if (pl.swimSpeed != PlayerSettings{}.swimSpeed)
        out << "player_swim " << pl.swimSpeed << '\n';
    if (pl.bushSlow != PlayerSettings{}.bushSlow)
        out << "player_bush " << pl.bushSlow << '\n';
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
    if (scene.memoryBudget != MemoryBudget{}) {
        out << "memory_budget";
        for (float mb : scene.memoryBudget.mb)
            out << ' ' << mb;
        out << '\n';
    }
    if (scene.streaming != StreamingSettings{}) {
        const StreamingSettings& st = scene.streaming;
        out << "streaming " << (st.enabled ? 1 : 0) << ' ' << st.cellSize << ' ' << st.loadRadius << ' ' << st.unloadRadius << ' '
            << st.budgetMB << ' ' << (st.keepFarLod ? 1 : 0) << '\n';
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
}
} // namespace

bool saveScene(const Scene& scene, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        setError(error, "Cannot write " + file.string());
        return false;
    }
    out << std::setprecision(6);
    out << (file.extension() == ".prefab" ? "# FPS engine prefab (objects in the level format)\nversion 6\n" : "# FPS engine level\nversion 6\n");
    const std::filesystem::path levelDir = std::filesystem::absolute(file).parent_path();
    const bool prefabFile = file.extension() == ".prefab";
    if (!prefabFile && !writeLevelSettings(out, scene, file, levelDir, error))
        return false;
    // Prefab instances: their objects and groups are written as the instance line and
    // the differences from the prefab (engine/Prefab.h); detached objects as normal ones.
    std::vector<PrefabInstance> instances = scene.prefabs;
    std::set<std::string> instanceNames;
    std::set<const void*> detached;
    std::set<std::string> instanceGroups;
    for (PrefabInstance& inst : instances) {
        std::vector<int> det;
        computePrefabOverrides(scene, inst, levelDir, &det);
        for (int o : det)
            detached.insert(scene.isBox(o) ? static_cast<const void*>(&scene.boxes[static_cast<std::size_t>(o)])
                            : scene.isModel(o) ? static_cast<const void*>(&scene.model(o))
                                               : static_cast<const void*>(&scene.entity(o)));
        for (const Group& g : scene.groups)
            if (g.prefabInstance == inst.name && g.name == inst.name)
                inst.parent = g.parent; // the instance group may have been moved in the hierarchy
        instanceNames.insert(inst.name);
    }
    for (const Group& g : scene.groups)
        if (instanceNames.count(g.prefabInstance))
            instanceGroups.insert(g.name);
    std::map<std::string, std::string> instanceParent;
    for (const PrefabInstance& inst : instances)
        instanceParent[inst.name] = inst.parent;
    // True when the object is written by its instance; a detached object is written
    // here as a normal one (moved out of the instance's groups, which are not saved).
    auto skip = [&](const auto& o) {
        if (o.prefabInstance.empty() || !instanceNames.count(o.prefabInstance))
            return false;
        if (detached.count(&o)) {
            auto copy = o;
            if (instanceGroups.count(copy.parent))
                copy.parent = instanceParent[o.prefabInstance];
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, Box>)
                writeBox(out, copy, levelDir);
            else if constexpr (std::is_same_v<T, Model>)
                writeModel(out, copy, levelDir, scene.streaming.enabled);
            else
                writeEntity(out, copy, levelDir);
        }
        return true;
    };
    for (const Group& g : scene.groups)
        if (!instanceGroups.count(g.name))
            out << "group " << std::quoted(g.name) << ' ' << std::quoted(g.parent) << '\n';
    for (const PrefabInstance& inst : instances) {
        out << "prefab " << std::quoted(inst.name) << ' ' << std::quoted(relativeTo(inst.path, levelDir)) << ' '
            << inst.position.x << ' ' << inst.position.y << ' ' << inst.position.z << ' ' << inst.yawDegrees << '\n';
        writeIdAndParent(out, inst.parent, inst.id);
        for (const PrefabOverride& o : inst.overrides) {
            out << "  override " << std::quoted(o.object) << ' ';
            switch (o.kind) {
            case PrefabOverride::Kind::Transform:
                out << "transform " << o.position.x << ' ' << o.position.y << ' ' << o.position.z << ' ' << o.rotation.x << ' '
                    << o.rotation.y << ' ' << o.rotation.z << ' ' << o.scale.x << ' ' << o.scale.y << ' ' << o.scale.z;
                break;
            case PrefabOverride::Kind::Property:
                out << "prop " << std::quoted(o.key) << ' ' << std::quoted(o.value);
                break;
            case PrefabOverride::Kind::Removed:
                out << "removed";
                break;
            }
            out << '\n';
        }
    }
    for (const Box& b : scene.boxes)
        if (!skip(b))
            writeBox(out, b, levelDir);
    for (const Model& m : scene.models)
        if (!skip(m))
            writeModel(out, m, levelDir, scene.streaming.enabled);
    if (!prefabFile)
        writeLevelPlayer(out, scene, levelDir);
    for (const Entity& e : scene.entities)
        if (!skip(e))
            writeEntity(out, e, levelDir);
    if (!out) {
        setError(error, "Write failed: " + file.string());
        return false;
    }
    return true;
}

LoadingScreen readLoadingScreen(const std::filesystem::path& file)
{
    LoadingScreen result;
    engine::VfsStream in(file);
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

bool loadScene(Scene& scene, const std::filesystem::path& file, std::string* error, LoadProgress* progress, const glm::vec3* streamFrom)
{
    engine::VfsStream in(file);
    if (!in) {
        setError(error, "Cannot open " + file.string());
        return false;
    }
    Scene result;
    std::string line;
    int lineNo = 0;
    enum class LastObject { None, Box, Model, Entity, Prefab } lastObject = LastObject::None;
    // Files used several times (fence sections, crates) are parsed once. All
    // model files of the level are read in parallel first (the slow part:
    // glTF parsing and texture decoding), then the lines are applied in order.
    std::map<std::filesystem::path, Model> loaded;
    // World streaming: which models start deferred (far static models with saved bounds),
    // and the one LOD file each of them keeps (its coarsest) when keepFarLod is on.
    std::vector<char> deferModel;
    std::vector<std::filesystem::path> keepLod;
    {
        struct ScanModel {
            std::filesystem::path file;
            glm::vec3 position{0.0f};
            bool hasBounds = false, dynamic = false;
            Aabb local;
            std::vector<std::filesystem::path> lods;
        };
        std::vector<ScanModel> scanned;
        std::vector<std::pair<std::filesystem::path, int>> otherFiles; // foliage: always loaded
        StreamingSettings streaming;
        glm::vec3 start(0.0f);
        bool inModel = false;
        engine::VfsStream scan(file);
        std::string scanLine;
        auto resolve = [&](const std::string& path) {
            std::filesystem::path p = pathFromUtf8(path);
            if (p.is_relative())
                p = std::filesystem::absolute(file).parent_path() / p;
            return p.lexically_normal();
        };
        while (std::getline(scan, scanLine)) {
            std::istringstream ls(scanLine);
            std::string key, name, path;
            ls >> key;
            const bool sub = !scanLine.empty() && scanLine[0] == ' ';
            if (!sub)
                inModel = false;
            if (key == "model") {
                ScanModel m;
                if (ls >> std::quoted(name) >> std::quoted(path) >> m.position.x >> m.position.y >> m.position.z) {
                    m.file = resolve(path);
                    scanned.push_back(std::move(m));
                    inModel = true;
                }
            } else if (key == "lod" && inModel && ls >> std::quoted(path)) {
                scanned.back().lods.push_back(resolve(path));
            } else if (key == "bounds" && inModel) {
                Aabb& b = scanned.back().local;
                scanned.back().hasBounds = static_cast<bool>(ls >> b.min.x >> b.min.y >> b.min.z >> b.max.x >> b.max.y >> b.max.z);
            } else if (key == "physics" && inModel) {
                scanned.back().dynamic = true;
            } else if ((key == "foliage_type" || key == "foliage_lod") && ls >> std::quoted(path) && !path.empty()) {
                otherFiles.emplace_back(resolve(path), 0);
            } else if (key == "player_start") {
                ls >> start.x >> start.y >> start.z;
            } else if (key == "streaming") {
                int en = 0, keep = 1;
                ls >> en >> streaming.cellSize >> streaming.loadRadius >> streaming.unloadRadius >> streaming.budgetMB >> keep;
                streaming.enabled = en != 0;
            }
        }
        const glm::vec3 from = streamFrom && !std::isnan(streamFrom->x) ? *streamFrom : start;
        deferModel.assign(scanned.size(), 0);
        keepLod.assign(scanned.size(), {});
        std::vector<std::filesystem::path> files;
        auto need = [&files](const std::filesystem::path& p) {
            if (!p.empty() && std::find(files.begin(), files.end(), p) == files.end())
                files.push_back(p);
        };
        for (std::size_t i = 0; i < scanned.size(); ++i) {
            const ScanModel& m = scanned[i];
            bool defer = false;
            if (streamFrom && streaming.enabled && m.hasBounds && !m.dynamic) {
                // The cell the model's centre falls in (rotation and scale ignored here: the
                // cell box is what counts, and the streamer re-checks with the real bounds).
                const glm::vec3 c = m.position + (m.local.min + m.local.max) * 0.5f;
                const float cs = std::max(streaming.cellSize, 1.0f);
                const glm::vec2 cell(std::floor(c.x / cs), std::floor(c.z / cs));
                const glm::vec2 lo = cell * cs, hi = lo + glm::vec2(cs);
                const glm::vec2 nearest = glm::clamp(glm::vec2(from.x, from.z), lo, hi);
                defer = glm::distance(nearest, glm::vec2(from.x, from.z)) > streaming.loadRadius;
            }
            deferModel[i] = defer ? 1 : 0;
            if (!defer) {
                need(m.file);
                for (const auto& l : m.lods)
                    need(l);
            } else if (streaming.keepFarLod && !m.lods.empty()) {
                keepLod[i] = m.lods.back();
                need(keepLod[i]);
            }
        }
        for (const auto& [p, unused] : otherFiles)
            need(p);
        std::vector<Model> models(files.size());
        std::vector<char> ok(files.size(), 0);
        if (progress) {
            progress->total = static_cast<int>(files.size()) + 2; // + level lines + physics/scripts afterwards
            progress->done = 0;
        }
        const auto modelsStart = std::chrono::steady_clock::now();
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
        if (progress) {
            progress->modelSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - modelsStart).count();
            progress->setStep("level");
        }
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
                {"bloom_threshold", &e.bloomThreshold}, {"ssao_radius", &e.ssaoRadius}, {"ssao_intensity", &e.ssaoIntensity}, {"ssr_intensity", &e.ssrIntensity},
                {"shadow_distance", &e.shadowDistance}, {"saturation", &e.saturation}, {"contrast", &e.contrast},
                {"vignette", &e.vignette}, {"grain", &e.grain}, {"rain", &e.rain}, {"wind", &e.wind}, {"storm", &e.storm}, {"puddles", &e.puddles}, {"wind_direction", &e.windDirection}, {"wind_gusts", &e.windGusts},
                {"auto_exposure", &e.autoExposure}, {"exposure_min_ev", &e.exposureMinEv}, {"exposure_max_ev", &e.exposureMaxEv},
                {"adapt_brighten", &e.adaptBrighten}, {"adapt_darken", &e.adaptDarken}, {"temperature", &e.temperature},
                {"green_magenta", &e.greenMagenta}, {"fog_volumetric_range", &e.fogVolumetricRange},
                {"fog_light_shafts", &e.fogLightShafts}, {"fog_anisotropy", &e.fogAnisotropy}, {"fog_noise", &e.fogNoise},
                {"fog_local_lights", &e.fogLocalLights}};
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
        } else if (key == "weather_cycle") {
            WeatherCycle& w = result.environment.weather;
            int enabled = 0;
            ok = static_cast<bool>(ls >> enabled >> w.minMinutes >> w.maxMinutes >> w.transitionMinutes);
            w.enabled = enabled != 0;
            if (ok && !(ls >> std::quoted(w.start)))
                w.start.clear();
        } else if (key == "weather_preset") {
            WeatherPreset p;
            ok = static_cast<bool>(ls >> std::quoted(p.name) >> p.cloudCover >> p.fogDensity >> p.rain >> p.wind >> p.storm >> p.exposure >>
                                   p.chance);
            if (ok)
                result.environment.weather.presets.push_back(p);
        } else if (key == "anomaly_storm") {
            AnomalyStormSettings& a = result.environment.anomalyStorm;
            int on = 0;
            ok = static_cast<bool>(ls >> on >> a.minMinutes >> a.maxMinutes >> a.warningSeconds >> a.durationSeconds >> a.damage >>
                                   a.npcDamage >> a.tint.x >> a.tint.y >> a.tint.z);
            a.enabled = on != 0;
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
        } else if (key == "foliage_impostor") {
            // Flat stand-in beyond this distance for the foliage type above (Renderer impostors).
            ok = result.terrain && !result.terrain->foliageTypes.empty() &&
                 static_cast<bool>(ls >> result.terrain->foliageTypes.back().impostorDistance);
        } else if (key == "impostor") {
            ok = lastObject == LastObject::Model && static_cast<bool>(ls >> result.models.back().impostorDistance);
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
        } else if (key == "paint") {
            // Vertex paint of one mesh part of the model above (engine/VertexPaint.h).
            std::size_t part = 0;
            std::string text;
            ok = lastObject == LastObject::Model && static_cast<bool>(ls >> part >> text) && part < 4096;
            if (ok) {
                Model& m = result.models.back();
                if (!m.paint)
                    m.paint = std::make_shared<VertexPaint>();
                if (m.paint->parts.size() <= part)
                    m.paint->parts.resize(part + 1);
                ok = decodeVertexPaint(text, m.paint->parts[part]);
            }
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
                const std::size_t modelIndex = result.models.size() - 1;
                const bool skip = m.deferred && modelIndex < keepLod.size() && keepLod[modelIndex] != p.lexically_normal();
                if (skip) { // deferred model: only its coarsest level is read now
                    if (!m.lodPath.empty())
                        m.farLods.push_back({p.lexically_normal(), nullptr, distance});
                    else {
                        m.lodDistance = distance;
                        m.lodPath = p.lexically_normal();
                    }
                } else if (!m.lodPath.empty()) { // second and later `lod` lines: farther levels
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
        } else if (key == "bounds") {
            ok = lastObject == LastObject::Model;
            if (ok) { // saved model-space bounds: used while the mesh is deferred
                Aabb b;
                ok = static_cast<bool>(ls >> b.min.x >> b.min.y >> b.min.z >> b.max.x >> b.max.y >> b.max.z);
                if (ok && result.models.back().deferred)
                    result.models.back().localBounds = b;
            }
        } else if (key == "streaming") {
            StreamingSettings& st = result.streaming;
            int en = 0, keep = 1;
            ok = static_cast<bool>(ls >> en >> st.cellSize >> st.loadRadius >> st.unloadRadius >> st.budgetMB >> keep);
            st.enabled = en != 0;
            st.keepFarLod = keep != 0;
        } else if (key == "memory_budget") {
            MemoryBudget& b = result.memoryBudget;
            for (float& mb : b.mb) { // fewer values (older files): the rest keep their defaults
                float v = 0.0f;
                if (!(ls >> v))
                    break;
                mb = v;
            }
            ok = true;
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
                if (lastObject == LastObject::Prefab)
                    result.prefabs.back().id = id;
                else if (lastObject == LastObject::Box)
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
                if (lastObject == LastObject::Prefab)
                    result.prefabs.back().parent = parent;
                else if (lastObject == LastObject::Box)
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
            const std::size_t modelIndex = result.models.size();
            if (ok && modelIndex < deferModel.size() && deferModel[modelIndex]) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                m.sourcePath = p.lexically_normal(); // mesh comes later (WorldStreamer); bounds from the `bounds` line
                m.deferred = true;
                result.models.push_back(std::move(m));
                lastObject = LastObject::Model;
            } else if (ok) {
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
        } else if (key == "player_bush") {
            ok = static_cast<bool>(ls >> result.player.bushSlow);
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
        } else if (key == "animgraph") {
            std::string path;
            ok = !result.entities.empty() && static_cast<bool>(ls >> std::quoted(path));
            if (ok) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = std::filesystem::absolute(file).parent_path() / p;
                Entity& e = result.entities.back();
                e.animGraphPath = p.lexically_normal();
                std::string graphError;
                e.animator.setGraph(loadAnimGraph(e.animGraphPath, &graphError));
                if (!e.animator.graph)
                    LOG_WARN("level", "%s", graphError.c_str()); // the character still plays its clip
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
        } else if (key == "prefab") {
            // Prefab instance; expanded into objects once the whole file is read.
            PrefabInstance p;
            std::string path;
            ok = static_cast<bool>(ls >> std::quoted(p.name) >> std::quoted(path) >> p.position.x >> p.position.y >> p.position.z >>
                                   p.yawDegrees);
            if (ok) {
                std::filesystem::path f = pathFromUtf8(path);
                if (f.is_relative())
                    f = std::filesystem::absolute(file).parent_path() / f;
                p.path = f.lexically_normal();
                result.prefabs.push_back(std::move(p));
                lastObject = LastObject::Prefab;
            }
        } else if (key == "override") {
            PrefabOverride o;
            std::string kind;
            ok = lastObject == LastObject::Prefab && static_cast<bool>(ls >> std::quoted(o.object) >> kind);
            if (ok && kind == "transform") {
                o.kind = PrefabOverride::Kind::Transform;
                ok = static_cast<bool>(ls >> o.position.x >> o.position.y >> o.position.z >> o.rotation.x >> o.rotation.y >> o.rotation.z >>
                                       o.scale.x >> o.scale.y >> o.scale.z);
            } else if (ok && kind == "prop") {
                o.kind = PrefabOverride::Kind::Property;
                ok = static_cast<bool>(ls >> std::quoted(o.key) >> std::quoted(o.value));
            } else if (ok && kind == "removed") {
                o.kind = PrefabOverride::Kind::Removed;
            } else {
                ok = false;
            }
            if (ok)
                result.prefabs.back().overrides.push_back(std::move(o));
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
    for (PrefabInstance& p : result.prefabs) {
        if (p.id == 0)
            p.id = newObjectId();
        std::string prefabError;
        if (!expandPrefab(result, p, &prefabError)) // the instance stays (and is saved) without objects
            LOG_WARN("level", "%s: prefab %s: %s", pathToUtf8(file.filename()).c_str(), p.name.c_str(), prefabError.c_str());
    }
    assignObjectIds(result); // levels before version 6, objects added by hand
    {
        const std::filesystem::path baked = probeFile(file);
        std::error_code ec;
        if (fileExists(baked)) {
            auto probes = std::make_shared<ProbeSet>();
            std::string probeError;
            if (loadProbes(*probes, baked, &probeError))
                result.probes = std::move(probes);
            else
                LOG_WARN("scene", "%s", probeError.c_str());
        }
    }
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
    if (pick != this || (!lod && distance > lodDistance * lodScale))
        for (const FarLod& far : farLods)
            if (far.model && distance > far.distance * lodScale)
                pick = far.model.get();
    if (pick->parts.empty()) { // streamed out: the coarsest level that is in memory stands in
        for (auto it = farLods.rbegin(); it != farLods.rend(); ++it)
            if (it->model && !it->model->parts.empty())
                return *it->model;
        if (lod && !lod->parts.empty())
            return *lod;
    }
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

namespace {
// Foot IK input: the ground under each animated foot relative to the character's base.
void feedFootIk(Entity& e, const Skeleton& skeleton, float dt, const GroundQuery& ground, std::vector<glm::mat4>& pose)
{
    Animator& a = e.animator;
    AnimGraphState& st = a.graphState;
    const AnimGraph& g = *a.graph;
    const bool any = std::any_of(g.ik.begin(), g.ik.end(), [](const AnimGraph::IkChain& c) { return c.kind == AnimGraph::IkKind::Foot; });
    if (!any || st.ikNodes.size() != g.ik.size() || st.footLift.size() != g.ik.size()) {
        st.footIk = false;
        return;
    }
    const bool active = ground && e.alive && a.graphActive();
    evaluatePose(skeleton, &a, pose, false);
    const glm::mat4 m = e.modelTransform();
    const float scale = std::max(1e-4f, glm::length(glm::vec3(m[0]))); // model units -> m
    const float range = g.footMaxAdjust;
    float drop = 0.0f;
    std::array<float, 8> lift{};
    for (std::size_t i = 0; i < g.ik.size() && i < lift.size(); ++i) {
        const int end = st.ikNodes[i][2];
        if (!active || g.ik[i].kind != AnimGraph::IkKind::Foot || end < 0)
            continue;
        const glm::vec3 foot = glm::vec3(m * pose[static_cast<std::size_t>(end)][3]);
        const std::optional<float> y = ground(glm::vec3(foot.x, e.position.y + range + 0.3f, foot.z));
        lift[i] = y ? std::clamp(*y - e.position.y, -range, range) : 0.0f;
        drop = std::min(drop, lift[i]);
    }
    const float k = std::min(1.0f, dt * 12.0f); // ease so stepping onto a ledge does not pop
    for (std::size_t i = 0; i < g.ik.size() && i < lift.size(); ++i)
        st.footLift[i] += (lift[i] / scale - st.footLift[i]) * k;
    st.pelvisDrop += (drop / scale - st.pelvisDrop) * k;
    st.footIk = true;
}
} // namespace

void updateAnimations(Scene& scene, float dt, const GroundQuery& ground)
{
    // Each entity's animator only touches its own state: spread over the job system.
    JobSystem::global().parallelFor(scene.entities.size(), 32, [&scene, dt, &ground](std::size_t begin, std::size_t end) {
        std::vector<glm::mat4> pose;
        for (std::size_t i = begin; i < end; ++i) {
            Entity& e = scene.entities[i];
            e.flash = std::max(0.0f, e.flash - dt);
            if (!e.model || !e.model->skeleton || e.offline)
                continue;
            const Skeleton& skeleton = *e.model->skeleton;
            if (e.animator.current.clip < 0 && !e.animator.graph && !e.animation.empty())
                e.animator.play(skeleton, e.animation, true, 1.0f, 0.0f);
            e.animator.update(skeleton, dt);
            if (e.animator.graph && !e.animator.graph->ik.empty())
                feedFootIk(e, skeleton, dt, ground, pose);
        }
    });
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
    return fileExists(lod) ? lod : std::filesystem::path();
}

Trunk trunkOf(const Model& model)
{
    static std::mutex mutex;
    static std::map<std::uint64_t, Trunk> cache;
    const std::uint64_t key = model.parts.empty() ? 0 : model.parts.front().id;
    {
        std::lock_guard lock(mutex);
        if (const auto it = cache.find(key); key && it != cache.end())
            return it->second;
    }
    Trunk t;
    float top = 0.0f;
    // Cross-sections of the mesh at 0.5, 1 and 1.4 m: where its edges pass these heights.
    std::vector<glm::vec2> cut;
    for (const MeshPart& part : model.parts) {
        for (const glm::vec3& p : part.positions)
            top = std::max(top, p.y);
        for (std::size_t i = 0; i + 2 < part.indices.size(); i += 3)
            for (int e = 0; e < 3; ++e) {
                const std::uint32_t ia = part.indices[i + static_cast<std::size_t>(e)], ib = part.indices[i + static_cast<std::size_t>((e + 1) % 3)];
                if (ia >= part.positions.size() || ib >= part.positions.size())
                    continue;
                const glm::vec3 a = part.positions[ia], b = part.positions[ib];
                for (const float h : {0.5f, 1.0f, 1.4f})
                    if ((a.y - h) * (b.y - h) < 0.0f) {
                        const glm::vec3 q = a + (b - a) * ((h - a.y) / (b.y - a.y));
                        cut.emplace_back(q.x, q.z);
                    }
            }
    }
    if (!cut.empty()) {
        auto percentile = [](std::vector<float> v, float f) {
            const std::size_t k = std::min(v.size() - 1, static_cast<std::size_t>(static_cast<float>(v.size()) * f));
            std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
            return v[k];
        };
        std::vector<float> xs, zs;
        for (const glm::vec2& p : cut) {
            xs.push_back(p.x);
            zs.push_back(p.y);
        }
        t.center = {percentile(xs, 0.5f), percentile(zs, 0.5f)};
        auto distances = [&] {
            std::vector<float> d;
            d.reserve(cut.size());
            for (const glm::vec2& p : cut)
                d.push_back(glm::distance(p, t.center));
            return d;
        };
        // The stem is the closest ring of points; branches and leaves lie farther out.
        const float window = std::max(0.06f, percentile(distances(), 0.25f)) * 1.5f;
        glm::vec2 sum(0.0f);
        int n = 0;
        for (const glm::vec2& p : cut)
            if (glm::distance(p, t.center) <= window) {
                sum += p;
                ++n;
            }
        if (n > 0)
            t.center = sum / static_cast<float>(n);
        std::vector<float> near;
        for (const float d : distances())
            if (d <= window)
                near.push_back(d);
        t.radius = std::clamp(near.empty() ? 0.15f : percentile(near, 0.6f), 0.06f, 0.6f);
    }
    if (top > 0.0f)
        t.height = std::clamp(top, 1.0f, 8.0f);
    std::lock_guard lock(mutex);
    if (key)
        cache[key] = t;
    return t;
}

} // namespace engine

namespace engine {

void Thickets::add(const Bush& bush)
{
    const auto index = static_cast<std::uint32_t>(bushes_.size());
    bushes_.push_back(bush);
    const int x0 = static_cast<int>(std::floor((bush.center.x - bush.radius) / kCell));
    const int x1 = static_cast<int>(std::floor((bush.center.x + bush.radius) / kCell));
    const int z0 = static_cast<int>(std::floor((bush.center.y - bush.radius) / kCell));
    const int z1 = static_cast<int>(std::floor((bush.center.y + bush.radius) / kCell));
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x)
            grid_[key(x, z)].push_back(index);
}

void Thickets::build(const Scene& scene)
{
    bushes_.clear();
    grid_.clear();
    // A bush from a world box: the footprint circle covers 80 % of the box (crowns are round).
    auto addBox = [this](const Aabb& box) {
        const float height = box.max.y - box.min.y;
        if (height < kMinHeight || height > kMaxHeight)
            return;
        const glm::vec2 half = glm::vec2(box.max.x - box.min.x, box.max.z - box.min.z) * 0.5f;
        if (2.0f * std::max(half.x, half.y) < kMinWidth)
            return;
        Bush b;
        b.center = glm::vec2(box.min.x + half.x, box.min.z + half.y);
        b.radius = std::clamp(0.8f * 0.5f * (half.x + half.y), 0.2f, 3.0f);
        b.bottom = box.min.y;
        b.top = box.max.y;
        add(b);
    };
    for (const Model& m : scene.models)
        if (!m.dynamic && !m.skeleton && m.sway >= kBushSway)
            addBox(m.bounds());
    if (const Terrain* t = scene.terrain.get()) {
        for (const FoliageInstance& f : t->foliage) {
            if (f.type >= t->foliageTypes.size())
                continue;
            const FoliageType& type = t->foliageTypes[f.type];
            if (!type.model || (type.collision && type.sway < kBushSway))
                continue; // trees: their trunk collides
            const Aabb& local = type.model->localBounds;
            const float s = f.scale;
            // A square of the mean footprint (any yaw); the width test uses the widest side.
            const float wide = s * std::max(local.max.x - local.min.x, local.max.z - local.min.z);
            if (wide < kMinWidth)
                continue;
            const float r = 0.25f * s * ((local.max.x - local.min.x) + (local.max.z - local.min.z));
            addBox({f.position + glm::vec3(-r, local.min.y * s, -r), f.position + glm::vec3(r, local.max.y * s, r)});
        }
    }
}

float Thickets::densityAt(glm::vec3 feet, float radius) const
{
    if (bushes_.empty())
        return 0.0f;
    const auto it = grid_.find(key(static_cast<int>(std::floor(feet.x / kCell)), static_cast<int>(std::floor(feet.z / kCell))));
    if (it == grid_.end())
        return 0.0f;
    float density = 0.0f;
    for (const std::uint32_t i : it->second) {
        const Bush& b = bushes_[i];
        if (feet.y > b.top - 0.2f || feet.y + 1.8f < b.bottom)
            continue; // standing on top of it (a wall, a wreck) or far below
        const float d = glm::length(glm::vec2(feet.x, feet.z) - b.center);
        const float reach = b.radius + radius;
        if (d >= reach)
            continue;
        density = std::max(density, std::clamp((reach - d) / std::max(b.radius, 0.2f), 0.0f, 1.0f));
    }
    return density;
}

} // namespace engine
