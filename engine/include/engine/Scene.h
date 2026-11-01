#pragma once
#include "engine/Animation.h"
#include "engine/Geometry.h"
#include "engine/Material.h"
#include "engine/Memory.h"
#include "engine/NavMesh.h"
#include "engine/TextureCook.h"
#include "engine/Terrain.h"
#include "engine/TimeOfDay.h"
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <glm/gtc/type_precision.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <atomic>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace engine {

// Persistent 64-bit object ID: stays the same when the object is renamed, moved or
// re-parented, so saved games, links between objects, streaming and prefabs can
// refer to it. 0 = none yet (assignObjectIds gives one).
using ObjectId = std::uint64_t;
ObjectId newObjectId();               // random, never 0
std::string objectIdToString(ObjectId id); // 16 hex digits
ObjectId objectIdFromString(const std::string& text); // 0 when invalid

// Axis-aligned box: quick blockout geometry placed in the editor.
struct Box {
    Box() = default;
    Box(std::string n, glm::vec3 p, glm::vec3 s, glm::vec3 c)
        : name(std::move(n)), position(p), size(s), color(c) {}

    std::string name = "Box";
    ObjectId id = 0;                // persistent ID (level line `id`), see ObjectId
    std::string parent;             // name of the parent object or group in the hierarchy, empty = root
    std::string prefabInstance, prefabObject; // runtime: prefab instance it was expanded from (engine/Prefab.h)
    glm::vec3 position{0.0f};       // center
    glm::vec3 size{1.0f};
    glm::vec3 rotation{0.0f};       // degrees around X, Y, Z (applied Y, X, Z like models)
    glm::vec3 color{0.7f, 0.7f, 0.7f}; // sRGB, used when there is no material
    std::filesystem::path materialPath;  // absolute .material, empty = plain color
    std::shared_ptr<Material> material;  // runtime, loaded from materialPath
    // Texture placement on top of the material's world-space mapping (editor texture tools).
    glm::vec2 uvScale{1.0f};
    glm::vec2 uvOffset{0.0f};
    float uvRotation = 0.0f;        // degrees

    bool rotated() const { return rotation != glm::vec3(0.0f); }
    bool uvModified() const { return uvScale != glm::vec2(1.0f) || uvOffset != glm::vec2(0.0f) || uvRotation != 0.0f; }
    glm::mat4 transform() const; // unit cube centered at the origin -> world
    Aabb bounds() const;         // world-space, encloses the rotated box
};

// Image shared by all parts and materials that use it.
// Pixels come either as plain RGBA8 (`rgba`, mipmaps made on the GPU) or as a
// cooked mip chain (`cooked`, usually block-compressed, see TextureCook.h).
// Once the GPU has a texture made from a cache file, the renderer may drop the
// CPU copies (`cookedFile` is enough to stream the data in again); hence mutable.
struct Texture {
    std::uint64_t id = 0;            // GPU cache key; a new id makes the renderer upload again
    int width = 0;                   // full size (mip 0)
    int height = 0;
    mutable std::vector<std::uint8_t> rgba;
    mutable CookedTexture cooked;
    std::filesystem::path cookedFile; // disk cache with the same mip chain, empty = none
    TextureFormat format = TextureFormat::RGBA8;
    int mipCount = 1;                // of the cooked chain (1 for plain RGBA8)
    bool srgb = true;                // sampling settings (TextureSettings)
    bool nearest = false;
    bool mipmaps = true;
    int wrap = 0;                    // 0 repeat, 1 clamp, 2 mirror

    // Something to draw: pixels in memory or a cache file to read them from.
    bool valid() const { return width > 0 && (!rgba.empty() || !cooked.data.empty() || !cookedFile.empty()); }
    // Takes a cooked chain (sets size, format and mip count).
    void setCooked(CookedTexture c, std::filesystem::path file = {});
    std::size_t gpuBytes() const;    // full chain on the GPU
};

// Unique id for GPU caches (mesh parts, textures).
std::uint64_t newResourceId();

// One glTF primitive. Static models have node transforms baked in (model space);
// animated models keep mesh-local vertices that follow a skeleton node or skin.
struct MeshPart {
    std::uint64_t id = 0;            // unique per loaded part, used as GPU cache key
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;   // empty when the material has no texture
    std::shared_ptr<const Texture> texture; // base color texture, may be null
    // glTF metal/roughness material (textures may be null).
    std::shared_ptr<const Texture> normalTexture;
    std::shared_ptr<const Texture> metalRoughTexture; // G = roughness, B = metallic
    std::shared_ptr<const Texture> occlusionTexture;  // R
    std::shared_ptr<const Texture> emissiveTexture;
    float metallic = 0.0f;
    float roughness = 0.8f;
    float normalScale = 1.0f;
    glm::vec3 emissive{0.0f};        // sRGB
    std::vector<std::uint32_t> indices; // triangles
    glm::vec3 color{0.8f};           // sRGB
    // glTF alphaMode / alphaCutoff / baseColorFactor alpha / doubleSided.
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    float opacity = 1.0f;
    bool doubleSided = false;
    std::string materialName;        // glTF material name, used for material overrides
    int node = -1;                   // animated models: rigid part attached to this skeleton node
    int skin = -1;                   // animated models: skinned part (uses joints/weights)
    std::vector<glm::u16vec4> joints;   // indices into the skin's joint list
    std::vector<glm::vec4> weights;
};

// The mesh parts of a model, shared by every copy: placing the same .glb twenty
// times (or keeping it in the editor's undo history) stores its vertices once.
// Reads go to the shared list; push_back/clear make the list unique first.
class PartList {
public:
    using const_iterator = std::vector<MeshPart>::const_iterator;
    const_iterator begin() const { return list().begin(); }
    const_iterator end() const { return list().end(); }
    std::size_t size() const { return parts_ ? parts_->size() : 0; }
    bool empty() const { return size() == 0; }
    const MeshPart& operator[](std::size_t i) const { return (*parts_)[i]; }
    const MeshPart& front() const { return parts_->front(); }
    const MeshPart& back() const { return parts_->back(); }
    void push_back(MeshPart part) { unique().push_back(std::move(part)); }
    void clear() { parts_.reset(); }
    // True when both lists are the same shared data.
    bool sharedWith(const PartList& other) const { return parts_ && parts_ == other.parts_; }
    long useCount() const { return parts_.use_count(); }

private:
    const std::vector<MeshPart>& list() const
    {
        static const std::vector<MeshPart> none;
        return parts_ ? *parts_ : none;
    }
    std::vector<MeshPart>& unique()
    {
        if (!parts_)
            parts_ = std::make_shared<std::vector<MeshPart>>();
        else if (parts_.use_count() > 1)
            parts_ = std::make_shared<std::vector<MeshPart>>(*parts_);
        return *parts_;
    }
    std::shared_ptr<std::vector<MeshPart>> parts_;
};

// Static level geometry imported from glTF (Blender: File > Export > glTF 2.0).
struct Model {
    std::string name = "Model";
    ObjectId id = 0;                // persistent ID (level line `id`), see ObjectId
    std::string parent;               // hierarchy parent (object or group name), empty = root
    std::string prefabInstance, prefabObject; // runtime: prefab instance it was expanded from (engine/Prefab.h)
    std::filesystem::path sourcePath; // absolute path of the .gltf/.glb
    glm::vec3 position{0.0f};
    PartList parts;                   // shared between copies of the model
    Aabb localBounds;                 // animated models: bounds in the rest pose
    std::shared_ptr<const Skeleton> skeleton; // null for static models
    // Engine materials replacing glTF materials, by glTF material name (set in the editor).
    std::map<std::string, std::filesystem::path> materialOverrides;
    std::map<std::string, std::shared_ptr<Material>> overrideMaterials; // runtime

    const Material* materialFor(const MeshPart& part) const
    {
        const auto it = overrideMaterials.find(part.materialName);
        return it == overrideMaterials.end() ? nullptr : it->second.get();
    }
    std::vector<std::string> materialSlots() const; // distinct glTF material names

    glm::vec3 rotation{0.0f};         // degrees around X, Y, Z (applied Z, then X, then Y)
    float scale = 1.0f;               // uniform

    // Simpler copy drawn when the camera is farther than lodDistance (e.g. foo_lod1.glb).
    std::shared_ptr<const Model> lod;
    std::filesystem::path lodPath;    // absolute, empty = no LOD
    float lodDistance = 80.0f;        // metres from the camera to the model's center
    // Further, even simpler levels (level lines: more `lod` lines, increasing distance).
    struct FarLod {
        std::filesystem::path path;   // absolute
        std::shared_ptr<const Model> model;
        float distance = 200.0f;
    };
    std::vector<FarLod> farLods;
    // Level drawn at `distance` metres from the center (lodScale multiplies the distances).
    const Model& lodFor(float distance, float lodScale = 1.0f) const;
    // Performance: not drawn beyond cullDistance metres from the camera (0 = always),
    // and no physics body when collision is off (grass, small debris, leaves: walk and shoot through).
    float cullDistance = 0.0f;
    bool collision = true;
    bool castShadows = true;          // off for grass and small plants: the shadow pass is the costliest one
    float sway = 0.0f;                // bending in the level's wind: 0 rigid, ~0.3 trees, 1 grass and bushes
    // Beyond this many metres (times the quality's lodScale) the renderer draws a flat
    // stand-in baked from the model: two crossed textured quads (trees). 0 = never.
    float impostorDistance = 0.0f;
    // Physics prop (crates, barrels, cans): a rigid body the player, bullets and blasts push.
    // The collider is fitted to localBounds. Ignored for animated models.
    bool dynamic = false;
    float massKg = 10.0f;
    std::string collider = "box";     // "box", "cylinder" (upright) or "sphere"

    glm::mat4 transform() const;      // model space -> world
    Aabb bounds() const;              // world-space bounds of the rotated, scaled model
    // World streaming (runtime): the mesh is not in memory (far cell); localBounds still
    // hold its size and the coarsest LOD may stand in. WorldStreamer loads it when near.
    bool deferred = false;
};

// World streaming: the level is cut into square cells; static models of the cells
// near the player are loaded on worker threads, far ones released (their coarsest
// LOD can stay as a stand-in). Level line `streaming`.
struct StreamingSettings {
    bool enabled = false;
    float cellSize = 128.0f;     // metres
    float loadRadius = 256.0f;   // cells closer than this (to the cell's box) are loaded
    float unloadRadius = 352.0f; // and released beyond this (hysteresis)
    int budgetMB = 1024;         // mesh memory; the nearest cells win when it runs out
    bool keepFarLod = true;      // far cells show their models' coarsest LOD
    bool operator==(const StreamingSettings&) const = default;
};

// Gameplay object (enemy, pickup, trigger...). Behaviour comes from its Lua script.
// Entity I/O (as in Hammer): when the entity fires `output` (a trigger's
// "OnStartTouch", an enemy's "OnDeath"...), every entity named `target` receives
// `input` ("TurnOn", "Trigger", "Kill"...) with `parameter`, after `delay`
// seconds. engine/EntityIO.h lists the known outputs and inputs.
struct EntityOutput {
    std::string output;
    std::string target;    // entity name; "!self", "!activator" or "!player" also work
    std::string input;
    std::string parameter; // optional, e.g. a message text or "key=value" for SetProperty
    float delay = 0.0f;    // seconds
    bool once = false;     // fires only the first time
    bool operator==(const EntityOutput&) const = default;
};

struct Entity {
    std::string name = "Entity";
    ObjectId id = 0;                // persistent ID (level line `id`), see ObjectId
    std::string parent;                  // hierarchy parent (object or group name), empty = root
    std::string prefabInstance, prefabObject; // runtime: prefab instance it was expanded from (engine/Prefab.h)
    std::string type = "empty";          // gameplay role, readable from scripts
    glm::vec3 position{0.0f};            // base (feet) position
    float yawDegrees = 0.0f;             // facing, 0 = +X, 90 = +Z
    float health = 100.0f;               // runtime: changed by weapons and scripts
    bool alive = true;                   // runtime: dead entities are not drawn or simulated
    glm::vec3 size{0.6f, 1.8f, 0.6f};    // marker box / character capsule size
    glm::vec3 color{0.8f, 0.25f, 0.2f};  // sRGB
    std::filesystem::path scriptPath;    // absolute path to a .lua file, empty = none
    // Values for the script's `properties` (set in the editor); missing keys use the script defaults.
    std::map<std::string, std::string> properties;
    std::vector<EntityOutput> outputs;   // connections to other entities (level sub-line `output`)

    // Animated character model (glTF with skeleton). It is scaled to the entity's
    // height and stands on its feet; without a model the entity is drawn as a box.
    std::filesystem::path modelPath;     // absolute, empty = box
    float modelScale = 1.0f;             // multiplier on top of the fit to size.y
    float modelYaw = 0.0f;               // degrees, added when the model does not face +Z
    std::string animation;               // clip played when no script chooses one (editor preview)

    std::shared_ptr<const Model> model;  // runtime: loaded from modelPath
    Animator animator;                   // runtime
    float flash = 0.0f;                  // runtime: seconds of white hit flash left

    glm::mat4 modelTransform() const;    // model space -> world for `model`

    Aabb bounds() const
    {
        const glm::vec3 half(size.x * 0.5f, 0.0f, size.z * 0.5f);
        return {position - half, position + half + glm::vec3(0.0f, size.y, 0.0f)};
    }

    // Editor-only helpers (sound sources and zones, map markers): not drawn in the
    // game and not hit by weapons.
    bool helper() const { return type == "sound" || type == "map_marker" || type == "particle" || type == "anomaly" || type == "light" ||
                                 type == "sit_point" || type == "trigger" || type == "relay" || type == "reverb" || type == "ambience" ||
                                 type == "water" || type == "interior"; }
    // What the editor draws and picks: a sound zone can span the whole level, so
    // it is shown as a small speaker cube at its position; the zone is its size.
    Aabb markerBounds() const
    {
        if (type == "sound" || type == "particle")
            return {position, position + glm::vec3(type == "sound" ? 0.6f : 0.4f)};
        if (type == "relay" || type == "trigger" || type == "reverb" || type == "ambience" || type == "interior") // a node; a volume (its size) is drawn as lines
            return {position - glm::vec3(0.2f, 0.0f, 0.2f), position + glm::vec3(0.2f, 0.4f, 0.2f)};
        if (type == "water") // a node at the middle of the surface; the surface is drawn as water
            return {position + glm::vec3(-0.25f, size.y - 0.05f, -0.25f), position + glm::vec3(0.25f, size.y + 0.25f, 0.25f)};
        if (type == "light") // the light itself is at `position`
            return {position - glm::vec3(0.15f), position + glm::vec3(0.15f)};
        if (type == "anomaly") // the field (size) is drawn as rings by the editor
            return {position - glm::vec3(0.3f, 0.0f, 0.3f), position + glm::vec3(0.3f, 0.6f, 0.3f)};
        return bounds();
    }
};

// Player movement and health, stored per level and edited in the editor.
struct PlayerSettings {
    float walkSpeed = 5.0f;     // m/s
    float sprintSpeed = 8.5f;
    float jumpSpeed = 5.5f;
    float gravity = 18.0f;      // m/s^2, a bit above earth for snappy jumps
    float airControl = 0.15f;   // 0 = none, 1 = full
    float radius = 0.3f;
    float height = 1.8f;
    float eyeHeight = 1.7f;   // assets/SCALE.md: eye 1.7 m, character 1.8 m
    float stepHeight = 0.4f;    // stairs up to this height are climbed automatically
    float maxSlopeDegrees = 45.0f;
    float killY = -50.0f;       // respawn when falling below this
    float swimSpeed = 3.0f;     // m/s in deep water (wading slows walking by depth)
    float maxHealth = 100.0f;
    // Sounds (absolute paths of .sound or audio files, empty = silent).
    std::filesystem::path footstepSound; // one play per stride while walking
    std::filesystem::path landSound;     // landing after a fall
    std::filesystem::path hurtSound;     // taking damage
    // Radiation (anomalies of kind "radiation"): the dose drains health and wears off slowly.
    float radiationDamage = 1.0f; // hit points per second per 100 rad of dose
    float radiationDecay = 0.5f;  // rad per second the dose drops by itself
    // Flashlight (key F): a spot light at the eye that follows the view with a small lag.
    bool flashlight = true;              // the player has one in this level
    bool flashlightShadows = true;
    glm::vec3 flashlightColor{1.0f, 0.93f, 0.82f}; // sRGB, slightly warm
    float flashlightIntensity = 70.0f;
    float flashlightRange = 28.0f;       // m
    float flashlightAngle = 26.0f;       // degrees from the axis to the edge of the beam
    float flashlightLag = 0.07f;         // seconds the beam trails behind the view
    float flashlightBattery = 0.0f;      // seconds of light on a full battery, 0 = endless
    float flashlightRecharge = 90.0f;    // seconds to recharge an empty battery while off
};

// Look of the level: sun, sky, fog, ambient light and post-processing.
// Stored in the level file (`env key values...`) and edited in the editor.
struct Environment {
    glm::vec3 sunColor{1.0f, 0.93f, 0.82f};     // sRGB
    float sunIntensity = 3.0f;
    glm::vec3 skyZenith{0.32f, 0.42f, 0.58f};   // sRGB
    glm::vec3 skyHorizon{0.66f, 0.70f, 0.74f};  // sRGB
    glm::vec3 groundColor{0.30f, 0.28f, 0.25f}; // sRGB, below the horizon
    float cloudCover = 0.45f;                   // 0 clear .. 1 overcast
    float ambientIntensity = 1.0f;              // sky light (image based lighting)
    glm::vec3 fogColor{0.62f, 0.65f, 0.68f};    // sRGB
    float fogDensity = 0.012f;
    float fogHeight = 0.0f;                     // fog is densest below this height
    float fogHeightFalloff = 0.08f;
    float fogSunScatter = 1.0f;
    // Volumetric fog near the camera (when the quality allows it): light shafts through the
    // sun's shadows, lamps and the flashlight lighting up the fog.
    float fogVolumetricRange = 64.0f;           // m of view depth, 0 = analytic fog only
    float fogLightShafts = 1.0f;                // strength of the sun in the fog (0 = no shafts)
    float fogAnisotropy = 0.5f;                 // phase function g: 0 even, 0.9 bright toward the light
    float fogNoise = 0.4f;                      // 0 smooth .. 1 patchy fog drifting with the wind
    float fogLocalLights = 1.0f;                // strength of local lights in the fog
    float exposure = 1.0f;
    float bloomIntensity = 0.06f;
    float bloomThreshold = 1.2f;
    float ssaoRadius = 0.6f;
    float ssaoIntensity = 1.0f;
    float shadowDistance = 60.0f;               // m covered by the cascades
    float saturation = 1.0f;
    float contrast = 1.05f;
    glm::vec3 tint{1.0f};                       // multiplied after tone mapping
    float vignette = 0.25f;
    float grain = 0.02f;
    // Auto-exposure: the camera adapts to the brightness of the view like an eye.
    float autoExposure = 0.0f;                  // 0 manual (Exposure only) .. 1 fully automatic
    float exposureMinEv = -2.0f;                // darkest correction, stops (bright scenes)
    float exposureMaxEv = 2.0f;                 // brightest correction, stops: keep low so nights stay dark
    float adaptBrighten = 0.8f;                 // 1/s when the view gets darker (slow, like eyes)
    float adaptDarken = 2.5f;                   // 1/s when the view gets brighter
    // Grading after tone mapping.
    float temperature = 0.0f;                   // white balance: -1 cold (blue) .. 1 warm (orange)
    float greenMagenta = 0.0f;                  // white balance: -1 green .. 1 magenta
    glm::vec3 lift{0.0f};                       // shadows offset (about -0.2 .. 0.2)
    glm::vec3 gamma{1.0f};                      // midtones (> 1 brighter)
    glm::vec3 gain{1.0f};                       // highlights multiplier
    std::filesystem::path lut;                  // color LUT strip (N*N x N image), empty = none
    float lutStrength = 1.0f;
    // Weather (0..1): drives the rain and wind sound loops (and rain particles).
    float rain = 0.0f;
    float wind = 0.0f;
    // Weather loops (absolute .sound), empty = sounds/ambient/rain.sound and wind_storm.sound.
    std::filesystem::path rainSound;
    std::filesystem::path windSound;
    std::filesystem::path rainEffect;  // rain particles around the camera, empty = particles/rain.particle
    float storm = 0.0f;                // 0 none .. 1 frequent lightning (flash and thunder)
    // Wind: `wind` (0 calm .. 1 storm, ~20 m/s) blowing toward `windDirection` (degrees,
    // 0 = +X, 90 = +Z) drives vegetation sway, particles with "level wind" and the wind sound.
    float windDirection = 30.0f;
    float windGusts = 0.5f;            // 0 steady .. 1 strong gusts
    float puddles = 0.6f;              // 0..1 how much water collects on flat ground when wet
    std::filesystem::path thunderSound; // empty = sounds/ambient/thunder.sound
    // Room reverb of the whole level (engine/Audio.h reverbPresetNames()); reverb
    // zones (entities of type "reverb") override it where the listener is.
    std::string reverb = "none";
    float reverbAmount = 0.6f;
    // Ambience set of the whole level (.ambience: looping layers and random far sounds,
    // engine/Ambience.h); "ambience" zone entities replace it where the listener is.
    std::filesystem::path ambience;
    // Day/night cycle (engine/TimeOfDay.h); off = static sun (Scene::sunDirection).
    TimeOfDay time;
    // Weather changing over time (clear, fog, rain, storm...), engine/TimeOfDay.h.
    WeatherCycle weather;
    WeatherNow weatherNow; // runtime
};

// Folder in the scene hierarchy (no geometry), e.g. "Building A" or "Enemies".
struct Group {
    std::string name = "Group";
    std::string parent;                  // empty = root
    std::string prefabInstance;          // runtime: group of a prefab instance (engine/Prefab.h)
};

// Difference of one object of a prefab instance from the prefab (engine/Prefab.h).
struct PrefabOverride {
    enum class Kind { Transform, Property, Removed } kind = Kind::Transform;
    std::string object;           // object name in the prefab
    // Transform, in the prefab's space: position, rotation in degrees (entities: y =
    // yaw) and box size / model scale (x) / entity size.
    glm::vec3 position{0.0f}, rotation{0.0f}, scale{1.0f};
    std::string key, value;       // Property: entity script property
};

// A prefab placed in the level (level line `prefab`); its objects are in the scene
// as normal objects named "<name>/<object>" under a group called `name`.
struct PrefabInstance {
    std::string name;
    std::string parent;           // hierarchy parent of the instance group
    std::filesystem::path path;   // absolute .prefab
    glm::vec3 position{0.0f};     // the prefab's origin in the level
    float yawDegrees = 0.0f;      // turn around Y (as a model's rotation.y)
    ObjectId id = 0;              // object IDs of the instance are derived from it
    std::vector<PrefabOverride> overrides; // as loaded; saving computes them again
};

// What the game shows while the level loads (edited in the editor: Level > Loading screen).
struct LoadingScreen {
    std::filesystem::path image;  // absolute, empty = plain dark background
    std::string title;            // empty = level file name
    std::string hint;             // line under the progress bar
};

// In-game map (minimap and the full map on M). Without an image the game
// renders the level from above when it loads. Points of interest are entities
// of type "map_marker" (their name is the label).
struct MapSettings {
    std::filesystem::path image; // absolute, empty = rendered from the level; top of the image = -Z (north)
    glm::vec2 min{0.0f};         // world X, Z of the image's top-left corner
    float size = 0.0f;           // metres covered (square); 0 = around everything in the level
};

// Progress of loadScene(), readable from another thread (the loading screen).
struct LoadProgress {
    std::atomic<int> done{0};
    std::atomic<int> total{1};
    void setStep(const std::string& what)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = what;
    }
    std::string step() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_;
    }
    float fraction() const { return total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.0f; }

private:
    mutable std::mutex mutex_;
    std::string current_;
};

struct Scene {
    std::vector<Group> groups;
    std::vector<Box> boxes;
    std::vector<Model> models;
    std::vector<Entity> entities;
    std::vector<PrefabInstance> prefabs; // engine/Prefab.h; their objects are in the vectors above
    std::vector<std::filesystem::path> loadout;
    NavSettings nav;       // navmesh build settings (agent size, cell size)
    StreamingSettings streaming;
    MemoryBudget memoryBudget; // engine/Memory.h, level line `memory_budget`
    PlayerSettings player; // player's .weapon files (absolute), slot 1, 2, ...
    glm::vec3 playerStart{0.0f, 0.0f, 0.0f}; // feet position
    glm::vec3 sunDirection{-0.4f, -1.0f, -0.3f};
    Environment environment;
    LoadingScreen loading;
    MapSettings map;
    // Heightfield ground with painted layers and foliage, null = none. Shared between
    // copies of the scene (undo history); the editor copies it before each brush stroke.
    std::shared_ptr<Terrain> terrain;

    // Objects are addressed by one index: boxes first, then models, then entities.
    int objectCount() const { return static_cast<int>(boxes.size() + models.size() + entities.size()); }
    int firstModel() const { return static_cast<int>(boxes.size()); }
    int firstEntity() const { return static_cast<int>(boxes.size() + models.size()); }
    bool isBox(int object) const { return object >= 0 && object < firstModel(); }
    bool isModel(int object) const { return object >= firstModel() && object < firstEntity(); }
    bool isEntity(int object) const { return object >= firstEntity() && object < objectCount(); }
    Model& model(int object) { return models[static_cast<std::size_t>(object - firstModel())]; }
    const Model& model(int object) const { return models[static_cast<std::size_t>(object - firstModel())]; }
    Entity& entity(int object) { return entities[static_cast<std::size_t>(object - firstEntity())]; }
    const Entity& entity(int object) const { return entities[static_cast<std::size_t>(object - firstEntity())]; }
    Aabb objectBounds(int object) const;
};

Scene makeDefaultScene();

// Level files are text; model paths are stored relative to the level file.
bool saveScene(const Scene& scene, const std::filesystem::path& file, std::string* error = nullptr);
// One object as its level lines (paths relative to levelDir); used to compare objects.
void writeBox(std::ostream& out, const Box& b, const std::filesystem::path& levelDir);
void writeModel(std::ostream& out, const Model& m, const std::filesystem::path& levelDir, bool withBounds);
void writeEntity(std::ostream& out, const Entity& e, const std::filesystem::path& levelDir);
// Model files are parsed in parallel on the job system; `progress` (optional) counts them.
// streamFrom (game): with streaming on, static models in cells beyond the load radius
// of this point (NaN x: the level's player start) are not read (deferred); null = everything (the editor).
bool loadScene(Scene& scene, const std::filesystem::path& file, std::string* error = nullptr,
               LoadProgress* progress = nullptr, const glm::vec3* streamFrom = nullptr);
// Square area of the map: the level's settings, or everything in the level plus a margin.
void mapArea(const Scene& scene, glm::vec2& min, float& size);
// Only the loading screen settings of a level (fast: no models are read).
LoadingScreen readLoadingScreen(const std::filesystem::path& file);

// Loads every mesh of a glTF 2.0 file (.gltf or .glb) into model.parts.
bool loadModelFile(Model& model, const std::filesystem::path& file, std::string* error = nullptr);
// "foo.glb" -> "foo_lod1.glb" next to it when that file exists, else empty.
std::filesystem::path findLodFile(const std::filesystem::path& file);

// Loads a glTF character with its skeleton, skins and animation clips.
bool loadAnimatedModelFile(Model& model, const std::filesystem::path& file, std::string* error = nullptr);

// Shared, cached animated model (entities using the same file share it); null on error.
std::shared_ptr<const Model> loadCharacterModel(const std::filesystem::path& file, std::string* error = nullptr);

// Advances every entity's animation by dt (seconds). Entities that no script
// animates play their `animation` clip on a loop.
void updateAnimations(Scene& scene, float dt);

// A tunable declared by a script in its `properties` table (shown in the editor).
// Gives every box, model and entity without an ID (or with one already used by
// another object, e.g. after a copy) a new one. Returns how many changed.
int assignObjectIds(Scene& scene);
// Object index (Scene::objectCount order) with this ID, -1 if none.
int findObjectById(const Scene& scene, ObjectId id);

struct ScriptProperty {
    std::string name;
    std::string defaultValue;   // as text: number, true/false or string
    std::string description;
};

// Reads the `properties = { {"name", default, "description"}, ... }` table of a
// Lua script without running its callbacks.
bool readScriptProperties(const std::filesystem::path& script, std::vector<ScriptProperty>& out, std::string* error = nullptr);

// Object index (see Scene::objectCount) of the first box or model hit by the ray, or -1.
int pickObject(const Scene& scene, glm::vec3 origin, glm::vec3 dir);

} // namespace engine
