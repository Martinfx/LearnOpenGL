#pragma once
#include "engine/FrameArena.h"
#include "engine/RangeAllocator.h"
#include "engine/Particles.h"
#include "engine/Lights.h"
#include "engine/Scene.h"
#include <glm/mat4x4.hpp>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include "engine/TextureCook.h"
#include <chrono>
#include <future>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine {

// Matches glad's GLADloadfunc, so GLFW and Qt can both pass their loaders.
using GlProc = void (*)();
using GlLoader = GlProc (*)(const char* name);

// Untextured cube for effects and markers. Emissive = unlit, full color.
struct OverlayBox {
    glm::vec3 center{0.0f};
    glm::vec3 size{0.1f};
    glm::vec3 color{1.0f};
    bool emissive = false;
};

// Flat-colored helper geometry (editor navmesh, AI paths), blended over the scene
// and hidden by what stands in front of it.
struct DebugGeometry {
    std::vector<glm::vec3> triangles; // 3 vertices each
    std::vector<glm::vec3> lines;     // 2 vertices each
    std::vector<glm::vec4> lineColors; // optional: one per line vertex (else lineColor)
    glm::vec4 fill{0.2f, 0.9f, 0.4f, 0.25f};
    glm::vec4 edges{0.2f, 1.0f, 0.5f, 0.7f}; // triangle outlines, a = 0: none
    glm::vec4 lineColor{1.0f, 0.8f, 0.2f, 1.0f};
    bool xray = false; // drawn over everything (not hidden by walls): editor connection arrows
};

// How the main pass shades the scene (editor view modes).
enum class Shading { Lit, Unlit, LightingOnly, Wireframe };

// Screen-space warp around a point in the world (anomalies): heat haze rising over
// a vent, or a lens that bends the image toward its middle. The renderer keeps up
// to 8 per frame, applied in post-processing (not to the first-person weapon).
struct ScreenDistortion {
    glm::vec3 position{0.0f};
    float radius = 1.0f;      // m
    float strength = 1.0f;    // 0..1 (scaled by the mode's own maximum)
    bool lens = false;        // false = heat shimmer, true = lens / gravity bend
};

struct RenderView {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 cameraPosition{0.0f};
    int viewportWidth = 1;
    int viewportHeight = 1;
    int viewportX = 0, viewportY = 0; // lower-left corner in the target framebuffer (split views)
    bool fog = true;                  // off for the editor's orthographic views
    Shading shading = Shading::Lit;
    bool grid = false;        // editor: metre grid (1 m minor, 10 m major lines)
    int gridAxis = 1;         // normal of the grid plane: 0 X (side), 1 Y (ground), 2 Z (front)
    float gridFade = 150.0f;  // m from the camera
    // Extra models drawn with the scene (editor reference mannequin), not part of the level.
    std::vector<std::pair<const Model*, glm::mat4>> extraModels;
    int selectedObject = -1;  // editor highlight (Scene object index), -1 = none
    bool drawPlayerStart = false;
    bool editorHelpers = false; // editor: draw helper entities (map markers) the game hides
    float time = 0.0f;        // seconds, animates clouds and film grain
    // Weather (the game drives these; the editor leaves the defaults).
    float wetness = -1.0f;    // 0 dry .. 1 soaked surfaces; < 0 = follow the environment's rain
    float lightning = 0.0f;   // 0..1 flash of a lightning strike this frame
    // The game draws the first-person layer before post-processing: it sets this,
    // calls renderOverlay() and then present().
    bool deferPresent = false;
    std::vector<OverlayBox> worldBoxes; // e.g. bullet impacts
    std::vector<SpriteBatch> sprites;   // particles and decals (ParticleSystem::appendSprites, decal pools)
    std::vector<const DebugGeometry*> debug; // editor helpers (navmesh), not owned
    std::vector<OverlayBox> gizmoBoxes; // editor handles, drawn on top of everything (world space)
    std::vector<ScreenDistortion> distortions; // anomalies: heat haze, lens bends
    // Local lights besides the level's "light" entities (flashlight, muzzle flashes, fires).
    std::vector<LocalLight> lights;
    // Probe bake (captureCube): HDR only (no post-processing), no volumetric fog, no probes,
    // no occlusion culling, full resolution.
    bool capture = false;
    // A continuous view (the game): temporal anti-aliasing may build on earlier frames.
    // Views drawn on demand or several per frame (editor panes) use FXAA instead.
    bool temporal = false;
};

// First-person layer drawn on top of the world (weapon, muzzle flash, crosshair).
// Everything is in view space: camera at the origin looking down -Z.
struct OverlayPass {
    glm::mat4 projection{1.0f};
    glm::vec3 sunDirectionViewSpace{0.0f, -1.0f, 0.0f};
    const Model* model = nullptr;
    const Animator* animator = nullptr; // pose of an animated view model (arms)
    glm::mat4 modelTransform{1.0f};
    std::vector<OverlayBox> boxes;
    std::vector<SpriteBatch> sprites; // muzzle flashes (view space)
};

// Graphics options of the machine (not of the level): set from the editor's
// View menu or the game's settings.
struct RenderQuality {
    int shadowResolution = 2048;  // per cascade, 0 = no shadows
    int shadowCascades = 3;       // 1..3
    bool ssao = true;
    bool bloom = true;
    bool fxaa = true;
    // Temporal anti-aliasing in continuous views (RenderView::temporal); elsewhere FXAA
    // takes over when this is on.
    bool taa = true;
    bool ssr = true; // screen-space reflections (wet ground, glass, polished floors)
    bool parallax = true; // height maps shift textures with the view angle (Material::heightDepth)
    // Sun shadows sharp at the contact and softer far from the caster (PCSS, Environment::sunSize).
    bool softShadows = true;
    // Screen-space indirect light: walls and ground seen on screen light their neighbours
    // (one bounce, gathered with the ambient occlusion; needs ssao). Environment::bounceLight.
    bool ssgi = true;
    // Contact shadows: a short screen-space ray toward the sun adds the small shadows the
    // shadow map misses (grass, kerbs, rubble). Environment::contactShadowLength.
    bool contactShadows = true;
    bool ssaoFullRes = false; // ambient occlusion + bounce light at full resolution (Ultra), else half
    float motionBlur = 0.0f; // camera motion blur in continuous views, shutter fraction 0..1 (0 = off)
    int textureUploadsPerFrame = 6; // new textures sent to the GPU per frame, 0 = all at once
    // Memory. Textures larger than textureMaxSize drop their top mip levels (0 = full size).
    // Texture streaming: objects farther than textureStreamDistance get a small copy
    // (at most 256 px) on the GPU; the full one is read from the cook cache on a worker
    // thread when you come closer (0 = always full size).
    int textureMaxSize = 0;
    float textureStreamDistance = 40.0f;
    bool releaseCpuTextures = true; // drop CPU pixels once on the GPU (re-read from the cache if needed)
    bool instancing = true;         // copies of one model in one draw call per mesh part
    bool impostors = true;          // far trees as baked crossed quads (Model/FoliageType impostorDistance)
    // Shadows: the far cascades are re-rendered every 2nd / 4th frame (sooner when the
    // camera leaves their margin or the sun moves); small casters are left out of them.
    bool shadowCache = true;
    // Detail objects: low models (under 2 m) with a cull distance and no collision (grass, weeds,
    // debris). They thin out with distance (detailDensityFar of them are left at the
    // cull distance) and cast shadows only within detailShadowDistance metres.
    float detailDensityFar = 0.2f;
    float detailShadowDistance = 15.0f;
    float lodScale = 1.0f;          // multiplies every model's LOD distance
    // Occlusion culling: objects hidden behind nearer geometry in a recent frame's depth
    // (read back asynchronously, small screen copy) are skipped in the depth and main passes.
    bool occlusionCulling = true;
    // Frame preparation on workers: model culling of the main view and the sun's shadow
    // cascades starts as jobs at the beginning of the frame and runs while the main thread
    // submits the rain map, environment, probes and lights; off = culled when drawn.
    bool asyncCull = true;
    float renderScale = 1.0f; // scene resolution relative to the window (0.5 = half), HUD stays sharp
    // Dynamic resolution: in the game view the scene resolution drops (down to dynamicMinScale of
    // renderScale) while the GPU needs more than 1/dynamicTargetFps per frame, and climbs back when
    // there is room again (GPU timer queries; 0 = off). The image is sharpened when smaller.
    float dynamicTargetFps = 0.0f;
    float dynamicMinScale = 0.5f;
    // Sharpening of a scene drawn below the window resolution (0 off .. 1 strong).
    float upscaleSharpness = 0.5f;
    // Shadows of local lights: the nearest shadow-casting lights get layers of one map
    // (a spot light 1, a point light 6), 0 = no local shadows.
    int localShadowResolution = 1024;
    int localShadowLayers = 12;
    // Volumetric fog (light shafts, lamps and the flashlight in the fog) near the camera:
    // 0 off (analytic height fog only), 1 medium froxel grid, 2 fine grid.
    int volumetricFog = 2;

    static RenderQuality preset(int level); // 0 low, 1 medium, 2 high, 3 ultra
};

// Forward renderer with an HDR pipeline: cascaded sun shadows, depth pre-pass,
// SSAO, physically based shading with image based lighting from a procedural
// sky, height fog, bloom, ACES tone mapping, color grading and FXAA. Shaders live
// in engine/shaders/ (embedded at build time, reloadable from disk).
class Renderer {
public:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    ~Renderer();

    // Call with a current OpenGL 4.5+ core context.
    bool init(GlLoader loader, std::string* error = nullptr);
    void shutdown();
    void render(const Scene& scene, const RenderView& view);
    // Probe bake: the scene around `position` as 6 HDR faces (GL cubemap order, rows from t = 0),
    // size * size RGB floats each, appended to `faces`. Several full renders; context current.
    void captureCube(const Scene& scene, glm::vec3 position, int size, std::vector<float>& faces, float time = 0.0f);
    // Probe bake bounces: captures are lit by these probes (earlier bounce) instead of none.
    void setCaptureProbes(std::shared_ptr<const ProbeSet> probes) { captureProbes_ = std::move(probes); }
    // Tests (golden images): texture streaming by frames, not wall time, and waits for its reads.
    void setDeterministic(bool on) { deterministic_ = on; }
    void renderOverlay(const RenderView& view, const OverlayPass& pass); // after render(), before present()
    void present(); // post-processing into the framebuffer that was bound when render() was called
    // Reads the bound framebuffer (after present()) as RGB8 rows from the top; for screenshots and tests.
    std::vector<std::uint8_t> readPixels(int width, int height) const;

    // Sends the meshes and textures of the scene to the GPU before the first frame
    // (loading screen), so walking toward new models does not stall on uploads.
    // Call once per loading-screen frame; works for about `budgetMs` and returns
    // the fraction done (1 = finished). `cursor` starts at 0.
    // `viewer` (e.g. the player start) decides which textures start at full size (streaming).
    float preload(const Scene& scene, std::size_t& cursor, double budgetMs, glm::vec3 viewer = glm::vec3(0.0f));

    // Renders the level from straight above (orthographic, no fog) into a new
    // texture of `pixels` x `pixels` covering [minXZ, minXZ + size] in X and Z.
    // Row 0 (bottom of the GL texture) is +Z, the top row is -Z (north up).
    // The caller owns the texture (deleteTexture()).
    unsigned renderTopDown(const Scene& scene, glm::vec2 minXZ, float size, int pixels);
    static void deleteTexture(unsigned texture);

    // GPU memory held by this renderer (bytes), for the editor's memory panel.
    struct MemoryStats {
        std::size_t textureBytes = 0, meshBytes = 0, targetBytes = 0;
        int textures = 0, meshes = 0;
        int streamedLow = 0;   // textures with only their small mips resident
        int streaming = 0;     // reads from the cook cache in flight
        std::size_t frameArenaBytes = 0, frameArenaPeak = 0; // CPU scratch memory of a frame
        std::size_t savedBytes = 0; // vs. uncompressed full-size RGBA8 with mipmaps
    };
    MemoryStats memoryStats() const;

    // Frame profile: CPU time to record each pass and GPU time to execute it
    // (timer queries, a couple of frames behind), plus what was drawn.
    enum Pass { PassShadows, PassVolumetric, PassDepth, PassSsao, PassOpaque, PassTransparent, PassPost, PassOverlay, PassCount };
    static const char* passName(int pass);
    struct FrameStats {
        double cpuMs[PassCount] = {};
        double gpuMs[PassCount] = {};
        double cpuTotalMs = 0.0, gpuTotalMs = 0.0;
        double cullMs = 0.0;     // model culling, all views, on any thread (CPU time)
        double cullWaitMs = 0.0; // of that, the main thread waited for culling jobs
        int drawCalls = 0, instanced = 0, culled = 0;
        int meshDraws = 0;       // meshes drawn by multi-draw-indirect calls (each call counts once in drawCalls)
        long long triangles = 0;
        int uploads = 0;
        int occluded = 0;        // hidden behind nearer geometry (main pass)
        int lights = 0;          // local lights in view
        int lightShadows = 0;    // of those, with shadows
        bool gpuValid = false;   // timer queries available on this driver
        float renderScale = 1.0f; // scene resolution used (renderScale x dynamic resolution)
    };
    const FrameStats& frameStats() const { return stats_; }

    // Texture size / streaming changes apply to loaded textures too (streamed from the cook cache).
    void setQuality(const RenderQuality& quality);
    const RenderQuality& quality() const { return quality_; }

    // Compiles the shaders from `directory` (files named as in engine/shaders/).
    // On failure the previous shaders stay active and `error` has the compiler log.
    bool reloadShaders(const std::filesystem::path& directory, std::string* error = nullptr);
    static const std::map<std::string, std::string>& builtinShaders();

    std::string glInfo() const { return glInfo_; }

    // Auto-exposure jumps to the new view's brightness on the next frame (level start, teleport).
    void resetExposure() { exposureReset_ = true; }

private:
    FrameArena frameArena_{1024 * 1024}; // per-frame scratch arrays (light clusters, sprites)
    void collectDistortions(const RenderView& view); // anomaly warps of this frame as quads
    void renderDistortion(const RenderView& view, const glm::mat4& viewProj); // particles + warps -> distortTex_
    // Every mesh lives in one shared vertex and index buffer (the mesh pool), so
    // meshes with the same material can be drawn by one glMultiDrawElementsIndirect.
    struct GpuMesh {
        unsigned vao = 0;           // the pool's
        int indexCount = 0, vertexCount = 0;
        int firstIndex = 0, baseVertex = 0; // place in the pool
        std::size_t bytes = 0;
        bool used = false;
    };
    unsigned poolVao_ = 0, poolVbo_ = 0, poolIbo_ = 0, iotaBuffer_ = 0, indirectBuffer_ = 0;
    RangeAllocator poolVertices_, poolIndices_;
    std::size_t iotaCount_ = 0, indirectCapacity_ = 0;
    void growMeshPool(std::size_t vertices, std::size_t indices); // room for at least this many more
    void ensureIota(std::size_t count); // per-instance index attribute 0..count-1
    void releaseMesh(const GpuMesh& mesh);
    // Impostors: flat stand-ins of models, baked on the GPU at the start of a frame.
    // Keyed by the id of the model's first mesh part (copies share it).
    std::unordered_map<std::uint64_t, std::shared_ptr<const Model>> impostors_;
    std::unordered_map<std::uint64_t, Model> impostorQueue_;
    std::unordered_map<std::uint64_t, int> impostorWait_; // frames waited for textures
    const Model* impostorFor(const Model& model); // null until baked
    void bakePendingImpostors();
public:
    // Bakes the impostor of a model now (needs the GL context; tests use it directly).
    std::shared_ptr<const Model> bakeImpostor(const Model& model, int viewSize = 256);
private:
    struct GpuTexture {
        unsigned handle = 0;
        bool used = false;
        std::size_t bytes = 0;
        std::size_t fullBytes = 0;     // uncompressed RGBA8 with mips, for the "saved" figure
        // Streaming (textures with a cook cache file).
        std::filesystem::path file;
        TextureFormat format = TextureFormat::RGBA8;
        bool srgb = true, nearest = false, mipmaps = true;
        int wrap = 0;
        int firstMip = 0;              // top resident level of the full chain
        int lowMip = 0;                // level used while far away
        int maxMip = 0;                // level for textureMaxSize
        int width = 0, height = 0, mipCount = 1; // full chain
        float nearest_m = 1e30f;       // closest use this frame (metres)
        double farSince = -1.0;        // time the texture became far, -1 = near
        std::shared_ptr<std::future<CookedTexture>> pending;
        int pendingMip = 0;
    };
    struct Program {
        unsigned id = 0;
        mutable std::unordered_map<std::string, int> locations;
        int loc(const char* name) const;
    };
    struct Programs {
        Program mesh, depth, sky, envCapture, prefilter, brdf, ssao, blur, bloomDown, bloomUp, post, fxaa, taa, ssrHistory, copy, grid, hiz, sprite, debug,
            luminance, exposure, water, underwater, distort, volumeInject, volumeIntegrate;
    };
    // Uniform locations of the program that draws geometry right now.
    struct MeshLocations {
        int model = -1, viewProj = -1, color = -1, highlight = -1, emissive = -1, skinned = -1, flash = -1;
        int tiling = -1, worldUv = -1, grid = -1, emissiveColor = -1, roughness = -1, metallic = -1;
        int normalStrength = -1, roughnessChannel = -1, metallicChannel = -1, overlay = -1, envRotation = -1;
        int overlaySun = -1, uvScale = -1, uvOffset = -1, uvRotation = -1, shading = -1;
        int alphaMode = -1, alphaCutoff = -1, opacity = -1, instanced = -1, instanceBase = -1, sway = -1, fade = -1;
        int hasMap[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1}; // MapKind order
        int heightDepth = -1;
        int detail = -1;
        int paintBase = -1, paintBaseVertex = -1;
        int blendLayers = -1, blendColor = -1, blendTiling = -1, blendSurface = -1, blendNormalStrength = -1, blendSharpness = -1;
    };

    bool buildPrograms(const std::map<std::string, std::string>& sources, Programs& out, std::string* error);
    void destroyPrograms(Programs& p);
    void useMeshProgram(const Program& program);
    void ensureTargets(int width, int height);
    void destroyTargets();
    void ensureShadowMap();
    void updateEnvironment(const Scene& scene, const RenderView& view);
    void updateEnvironmentMap(const Scene& scene);
    void renderShadows(const Scene& scene, const RenderView& view);
    // Local lights: visible ones into frameLights_, shadow layers chosen, clusters built and uploaded.
    void collectLights(const Scene& scene, const RenderView& view);
    void renderLocalShadows(const Scene& scene, const RenderView& view);
    // Rain occlusion: depth seen from above around the camera (roofs keep the rain off).
    void renderRainMap(const Scene& scene, const RenderView& view);
    void uploadLightClusters(const RenderView& view);
    void uploadInteriors(const Scene& scene);
    void syncProbes(const Scene& scene, const std::shared_ptr<const ProbeSet>& probes); // -> cube array, irradiance atlas
    bool volumetricActive(const RenderView& view) const;
    void renderVolumetricFog(const RenderView& view);
    // Draws what intersects the frustum of `cull` (view-projection); skipNear keeps
    // shadow casters between the light and the cascade.
    void drawGeometry(const Scene& scene, const RenderView& view, bool depthOnly, const glm::mat4& cull, bool skipNear = false);
    void renderSsao();
    void renderBloom();

    const GpuMesh& upload(const MeshPart& part);
    unsigned texture(const Texture& tex);
    void drawModel(const Model& model, const glm::mat4& transform, float highlight, const Animator* animator = nullptr,
                   const VertexPaint* paint = nullptr);
    // Vertex paint on the GPU: one buffer per painted model, uploaded again when its version changes.
    struct GpuPaint {
        unsigned buffer = 0;
        std::size_t capacity = 0;      // bytes
        std::uint64_t version = 0;
        std::uint64_t lastFrame = 0;
        std::vector<int> partOffsets;  // first entry of each part, -1 = does not fit the mesh
    };
    std::unordered_map<const VertexPaint*, GpuPaint> paints_;
    const GpuPaint* uploadPaint(const VertexPaint& paint, const Model& model);
    void releaseUnusedPaint();
    void applyUv(const Box& box); // texture placement of a box (after applySurface)
    void applySurface(const Material* material, const MeshPart* part, glm::vec3 color, bool worldUv);
    void drawCube(const glm::mat4& transform, glm::vec3 color, float highlight, bool emissive);
    void releaseUnused();
    void keepAlive(const Model& model);
    void keepAliveOnce(const Model& model); // once per shared mesh per frame
    void keepAliveLods(const Model& model, const Model* drawn); // every LOD level except the drawn one
    std::unordered_set<const void*> keptAlive_;
    // Spatial grid of the level's models (32 m cells, bounds of their contents) so
    // culling tests a few hundred cells instead of every model in every pass.
    struct ModelCell {
        Aabb bounds{glm::vec3(1e30f), glm::vec3(-1e30f)};
        float maxCull = 0.0f;      // largest cull distance inside, 0 = something is never culled
        float maxDiagonal = 0.0f;  // largest model inside
        std::vector<std::uint32_t> models;
    };
    struct ModelGrid {
        const Scene* scene = nullptr;
        std::uint64_t hash = 0;
        std::vector<ModelCell> cells;
        // Per model, packed (structure of arrays) for the culling loop on the job system.
        static constexpr std::uint8_t kCastShadows = 1, kAnimated = 2;
        std::vector<Aabb> bounds;         // world bounds per model
        std::vector<glm::mat4> transforms; // Model::transform()
        std::vector<float> cullDistance;
        std::vector<std::uint8_t> flags;  // kCastShadows, kAnimated
        std::vector<float> diagonal;
        std::vector<float> random;        // 0..1 per model (detail thinning)
        std::vector<std::uint8_t> detail; // grass-like model
        int rebuilds = 0;
    };
    ModelGrid grid_;
    // Model culling of one view (grid cells, then frustum, distance and occlusion per model),
    // apart from drawing so it can run on a worker (RenderQuality::asyncCull). Reads only the
    // packed grid arrays, quality_ and occ_, no GL. A result is kept for the whole frame, so
    // the depth and the main pass of one view cull once.
    struct CullRequest {
        glm::mat4 cull{1.0f};
        glm::vec3 eye{0.0f};
        bool skipNear = false;
        int cascade = -1;        // shadow cascade or -1
        float minCaster = 0.0f;  // smallest shadow caster (far cascades)
        bool occlusion = false;  // test against the occlusion depth
        bool same(const CullRequest& o) const;
    };
    struct CullResult {
        std::vector<std::uint32_t> candidates; // models of the cells that passed
        std::vector<float> distances;
        std::vector<std::uint8_t> drawn;       // per candidate: 0 culled, 1 drawn, 2 hidden (occlusion)
        std::vector<std::uint32_t> culledCells;
        int hiddenCells = 0;
    };
    struct CullJob {
        CullRequest request;
        CullResult result;
        std::future<void> done; // invalid when computed on the main thread
    };
    std::vector<std::unique_ptr<CullJob>> cullJobs_; // this frame's results
    double cullWaitMs_ = 0.0;                         // main thread waiting for cull jobs (this frame)
    std::atomic<std::int64_t> cullNs_{0};             // culling time, all threads (this frame)
    void cullModels(const CullRequest& request, CullResult& out) const;
    void startCull(const CullRequest& request);       // on a worker; no-op if already there
    const CullResult& cullFor(const CullRequest& request); // started earlier (waits) or culled now
    void finishCullJobs();                            // waits for every job (end of frame, shutdown)
    bool hiddenByOcclusion(const Aabb& box) const;   // occluded() without counting
    // Occlusion culling (CPU test against a small max-depth copy read back without stalls).
    struct OcclusionReadback {
        unsigned pbo = 0;
        void* fence = nullptr;     // GLsync
        glm::mat4 viewProj{1.0f};
        glm::vec3 eye{0.0f};
    };
    struct Occlusion {
        unsigned fbo = 0, tex = 0;
        int width = 0, height = 0;
        std::array<OcclusionReadback, 3> ring{};
        int next = 0;
        std::vector<float> depth;  // latest finished copy, row 0 = bottom
        glm::mat4 viewProj{1.0f};
        glm::vec3 eye{0.0f};
        bool valid = false;
        bool testing = false;      // this pass uses it
        float inflate = 0.0f;      // box growth for camera movement since the copy
        int hidden = 0;            // objects skipped this frame
    } occ_;
    int lastOccluded_ = 0;
    void captureOcclusion(const RenderView& view);
    void pollOcclusion();
    void releaseOcclusion();
    bool occluded(const Aabb& box) const;
    void updateModelGrid(const Scene& scene);
    void keepTextureAlive(const Texture* tex);
    void updateStreaming();
    unsigned createTexture(GpuTexture& g, const CookedTexture& cooked, int firstMipOffset);

    std::unordered_map<std::uint64_t, GpuTexture> textures_;
    std::unordered_map<std::uint64_t, GpuMesh> meshes_; // keyed by MeshPart::id
    Programs programs_;
    MeshLocations loc_;
    bool depthOnly_ = false;
    int texturesUploaded_ = 0;          // this frame; large uploads are spread over frames
    float drawDistance_ = 0.0f;         // camera to the object being drawn (texture streaming)
    // See-through surfaces (AlphaMode::Blend), drawn after the opaque scene back to front.
    struct TransparentDraw {
        const MeshPart* part = nullptr;  // or a box (unit cube with world-space mapping)
        const Material* material = nullptr;
        const Box* box = nullptr;
        glm::mat4 transform{1.0f};
        float distance = 0.0f;
        float highlight = 0.0f;
    };
    std::vector<TransparentDraw> transparent_;
    // Instancing: visible copies of the same model (same shared mesh and materials).
    struct InstanceBatch {
        const Model* model = nullptr;
        std::vector<glm::mat4> transforms;
        float distance = 1e30f;
        float sway = 0.0f; // bending in the wind (Model::sway, FoliageType::sway)
        glm::vec2 fade{0.0f}; // dithered fade from x to y metres (cull distance); y 0 = none
    };
    std::vector<InstanceBatch> batches_;
    std::unordered_map<std::uint64_t, std::size_t> batchIndex_;
    std::vector<glm::mat4> instanceData_;
    unsigned instanceBuffer_ = 0;
    std::size_t instanceCapacity_ = 0;
    int drawCalls_ = 0, lastDrawCalls_ = 0, instanced_ = 0, lastInstanced_ = 0, meshDraws_ = 0;
    long long triangles_ = 0;
    int uploads_ = 0;
    // Profiler: timer queries per pass, in a ring of frames.
    static constexpr int kQueryFrames = 4;
    unsigned queries_[kQueryFrames][PassCount] = {};
    bool queryIssued_[kQueryFrames][PassCount] = {};
    int queryFrame_ = 0;
    int activePass_ = -1;
    std::chrono::steady_clock::time_point passStart_;
    FrameStats stats_, building_;
    void beginPass(int pass);
    void endPass();
    void collectQueries();
    void flushInstances();
    // Terrain mesh and splat map on the GPU, rebuilt when the terrain's versions change.
    // Terrain: one vertex buffer (grid + lowered skirt copies), drawn in 64-quad chunks
    // with geomipmapping LOD (every 2nd, 4th... vertex); skirts hide the cracks between levels.
    struct GpuTerrain {
        static constexpr int kChunk = 64, kLevels = 5;
        struct Chunk {
            Aabb bounds;
            std::uint32_t first[kLevels] = {}, count[kLevels] = {}; // index ranges per level
        };
        unsigned vao = 0, vbo = 0, ibo = 0, splat = 0; // splat: 2D array, slice 0 layers 0-3, slice 1 layers 4-7
        int resolution = 0, splatResolution = 0;
        glm::vec2 origin{0.0f};
        float size = 0.0f;
        // Layer maps copied into arrays (slice = layer), rebuilt per slice when its source texture changes.
        static constexpr int kLayerMapSize = 1024;
        unsigned albedoArray = 0, normalArray = 0, copyFbo = 0;
        unsigned albedoSource[8] = {}, normalSource[8] = {};
        std::vector<std::uint8_t> splatPlanes; // upload scratch
        std::uint64_t meshVersion = 0, splatVersion = 0;
        std::vector<Chunk> chunks;
        int chunksX = 0;
    };
    GpuTerrain terrain_;
    void drawTerrain(const Terrain& terrain, const RenderView& view);
    int updateTerrainLayers(const Terrain& terrain); // returns the uLayerMaps bits
    // Foliage instances sorted into 32 m cells per type, for culling.
    struct FoliageCell {
        Aabb bounds;
        std::vector<std::uint32_t> instances;
    };
    struct FoliageCache {
        const Terrain* terrain = nullptr;
        std::uint64_t version = 0;
        std::vector<std::vector<FoliageCell>> types;
        std::vector<glm::mat4> transforms;
    };
    FoliageCache foliageCache_;
    void addFoliage(const Terrain& terrain, const RenderView& view);
    bool collectTransparent_ = false;   // main pass: Blend parts go to transparent_
    void drawTransparent(const RenderView& view);
    void drawDebug(const std::vector<const DebugGeometry*>& items, const glm::mat4& viewProj);
    void drawSprites(const std::vector<SpriteBatch>& batches, const glm::mat4& viewProj, Shading shading, bool overlay);
    bool s3tc_ = true, s3tcSrgb_ = true;
    unsigned vao_ = 0, vbo_ = 0, emptyVao_ = 0;
    int vertexCount_ = 0;
    unsigned spriteBuffer_ = 0;          // shader storage with sprite instances (binding 3)
    std::size_t spriteCapacity_ = 0;
    unsigned debugBuffer_ = 0;           // shader storage with debug vertices (binding 4)
    std::size_t debugCapacity_ = 0;
    unsigned jointBuffer_ = 0;          // uniform block with skinning matrices (binding 0)
    unsigned envBuffer_ = 0;            // Environment uniform block (binding 1)
    std::vector<glm::mat4> pose_;       // scratch: node matrices of the model being drawn
    std::vector<glm::mat4> joints_;     // scratch: skinning matrices

    // Render targets.
    int width_ = 0, height_ = 0;          // render targets (output size x render scale)
    int outWidth_ = 0, outHeight_ = 0;    // the view's size in the target framebuffer
    unsigned hdrFbo_ = 0, hdrColor_ = 0, depthTex_ = 0;
    unsigned sceneColorCopy_ = 0, sceneDepthCopy_ = 0; // water: what lies behind the surface
    void copySceneForWater();
    void drawWater(const Scene& scene, const RenderView& view, const glm::mat4& viewProj);
    void drawUnderwater(const Scene& scene, const RenderView& view);
    unsigned ssaoFbo_ = 0, ssaoTex_ = 0, ssaoBlurFbo_ = 0, ssaoBlurTex_ = 0;
    int ssaoWidth_ = 0, ssaoHeight_ = 0; // ambient occlusion targets (half resolution unless ssaoFullRes)
    unsigned ssgiTex_ = 0;           // screen-space bounce light, second target of the SSAO pass
    bool ssgiActive_ = false;        // this frame's SSAO pass gathers bounce light
    bool contactActive_ = false;     // the lit pass traces contact shadows in the depth copy
    glm::mat4 ssgiViewToPrev_{1.0f}; // view space -> clip space of the frame in ssrColor_
    std::vector<unsigned> bloomTex_, bloomFbo_;
    std::vector<glm::ivec2> bloomSize_;
    unsigned ldrFbo_ = 0, ldrTex_ = 0;
    unsigned shadowFbo_ = 0, shadowTex_ = 0;
    int shadowRes_ = 0;
    // Cascade cache: what each layer of the shadow map currently holds.
    struct ShadowCascade {
        glm::mat4 matrix{1.0f};
        glm::vec3 center{0.0f};
        float radius = 0.0f;         // covered sphere (with the margin)
        bool valid = false;
    };
    std::array<ShadowCascade, 3> cascades_{};
    glm::vec3 cascadeLight_{0.0f};
    int cascadeCount_ = 0, cascadeRes_ = 0;
    unsigned cascadeUpdate_ = 0;     // bit per cascade rendered this frame
    std::uint64_t frameNumber_ = 0;
    int shadowCascade_ = -1;         // cascade being drawn (-1 = not the shadow pass)
    float shadowMinCaster_ = 0.0f;   // casters with a smaller bounding box diagonal are skipped
    unsigned envRaw_ = 0, envMap_ = 0, brdfLut_ = 0, captureFbo_ = 0;
    std::size_t envHash_ = 0;
    // Physical atmosphere: sky-view table (engine/Atmosphere.h) on texture unit 31, rebuilt when the sun or the air changes.
    float rainTop_ = 0.0f;
    bool skipPlants_ = false; // drawGeometry leaves out swaying models and foliage (top-down map) // world height of the top-down map's camera
    unsigned int skyLut_ = 0;
    std::size_t skyLutHash_ = 0;
    glm::vec3 skyLutZenith_{0.0f}, skyLutHorizon_{0.0f}, skyLutSunTint_{1.0f};
    unsigned whiteTex_ = 0;             // stand-in when SSAO is off
    unsigned shadowDummy_ = 0;          // stand-in when shadows are off
    std::vector<unsigned char> envBlockCopy_; // last environment uniform block written (CPU side)
    unsigned shadowRawSampler_ = 0;     // sun shadow map read as depth (no compare) by the lit pass (PCSS)
    // Volumetric fog: froxel light + density (ping-pong with the previous frame) and the
    // integrated result read by applyFog (unit 15).
    unsigned interiorBuffer_ = 0;       // interior volumes (binding 9)
    // Baked probes (engine/Probes.h): reflection cubemaps (array, unit 19), irradiance atlas
    // (r, g, b SH textures, units 16-18), shapes in SSBOs 10 and 11.
    std::shared_ptr<const ProbeSet> probesHeld_, captureProbes_;
    unsigned probeCubes_ = 0, probeIrradiance_[3] = {0, 0, 0}, probeBuffer_ = 0, probeVolumeBuffer_ = 0, probeDummyCube_ = 0,
             probeDummy3d_ = 0;
    int probeReflections_ = 0, probeVolumes_ = 0;
    unsigned volumeScatter_[2] = {0, 0}, volumeIntegrated_ = 0, volumeDummy_ = 0;
    glm::ivec3 volumeDims_{0};
    int volumeCurrent_ = 0;
    unsigned volumeFrame_ = 0;
    bool volumeHistory_ = false;
    glm::mat4 volumePrevViewProj_{1.0f};
    unsigned rainTex_ = 0, rainFbo_ = 0;   // rain occlusion depth (top-down)
    glm::mat4 rainMatrix_{1.0f};
    glm::vec3 rainCenter_{1e9f};
    std::uint64_t rainFrame_ = 0;          // frame of the last rain map update
    bool rainActive_ = false;              // wet or raining: map used this frame
    float wetness_ = 0.0f, lightning_ = 0.0f;

    // Clustered local lights (shader storage: 5 lights, 6 cluster grid, 7 light indices, 8 shadow matrices).
    unsigned lightBuffer_ = 0, lightGridBuffer_ = 0, lightIndexBuffer_ = 0, lightShadowBuffer_ = 0;
    std::vector<LocalLight> frameLights_;       // in view this frame
    struct LocalShadow {
        int light = -1;                         // index in frameLights_
        int layer = 0, faces = 1;               // first layer of localShadowTex_
        glm::mat4 matrices[6];
        float texelScale = 0.0f;                // normal offset per metre of distance
    };
    std::vector<LocalShadow> localShadows_;
    unsigned localShadowTex_ = 0, localShadowFbo_ = 0;
    int localShadowRes_ = 0, localShadowLayers_ = 0;
    void ensureLocalShadowMap();

    // Auto-exposure: centre-weighted log luminance (mipmapped to 1 texel) and the adapted
    // exposure in two 1x1 targets (ping-pong), all on the GPU.
    void updateExposure(const Environment& e);
    unsigned lumTex_ = 0, lumFbo_ = 0;
    unsigned adaptTex_[2] = {0, 0}, adaptFbo_[2] = {0, 0};
    int adaptIndex_ = 0;
    bool exposureReset_ = true;
    float lastExposureTime_ = -1.0f;
    // Color LUT (3D texture from a strip image), reloaded when the level's file changes.
    void updateLut(const Environment& e);
    unsigned lutTex_ = 0;
    int lutSize_ = 0;
    std::filesystem::path lutPath_;

    RenderQuality quality_;
    const Environment* env_ = nullptr;  // of the scene being rendered (resolvedEnv_ during render())
    Environment resolvedEnv_;           // the scene's environment at its time of day
    SkyState sky_;                      // light direction, moon, stars of resolvedEnv_
    float time_ = 0.0f;
    // Dynamic resolution (RenderQuality::dynamicTargetFps): current factor, smoothed GPU time.
    float dynScale_ = 1.0f;
    double dynGpuMs_ = 0.0;
    double dynLastChange_ = 0.0;
    void updateDynamicResolution();
    std::vector<SpriteInstance> distortionSprites_; // this frame: anomaly warps as camera-facing quads
    unsigned distortTex_ = 0, distortFbo_ = 0;      // screen offsets in pixels (rg), scene depth for the test
    unsigned distortBuffer_ = 0;
    std::size_t distortCapacity_ = 0;
    bool distortDrawn_ = false;
    bool overlayDrawn_ = false;           // first-person layer in the depth buffer (masks the distortion)
    int targetFbo_ = 0;                 // framebuffer to present into
    int targetX_ = 0, targetY_ = 0;     // where in it
    bool pendingPresent_ = false;
    bool wireframe_ = false;
    bool ignoreUploadBudget_ = false;  // preload(): everything at once            // last render() was a wireframe view
    glm::mat4 view_{1.0f}, projection_{1.0f}; // projection_ carries the TAA jitter
    // Temporal AA (taa.frag): two history targets swapped every frame.
    unsigned taaHistory_[2] = {0, 0}, taaFbo_[2] = {0, 0}, taaDepth_ = 0;
    int taaIndex_ = 0;
    bool taaActive_ = false, taaValid_ = false;
    std::uint64_t taaFrame_ = 0;
    glm::vec2 taaJitterUV_{0.0f};
    glm::mat4 taaViewProj_{1.0f}, taaPrevViewProj_{1.0f}, taaJitteredViewProj_{1.0f};
    unsigned sceneTex_ = 0; // what post-processing reads: hdrColor_ or the TAA result
    // Motion blur: cameras of this and the previous continuous frame.
    bool motionActive_ = false, motionValid_ = false;
    glm::mat4 motionViewProj_{1.0f}, motionPrevViewProj_{1.0f}, motionJitteredViewProj_{1.0f};
    // Screen-space reflections: the previous frame's HDR image (mipmapped) they read from.
    unsigned ssrColor_ = 0, ssrFbo_ = 0;
    bool ssrRecord_ = false, ssrValid_ = false;
    glm::mat4 ssrViewProj_{1.0f};     // camera of the frame in ssrColor_ (no jitter)
    glm::mat4 ssrPrevViewProj_{1.0f}; // this frame's, until it is recorded
    void recordSsrColor();
    void resolveTaa();
    bool deterministic_ = false;
#ifdef TRACY_ENABLE
    static const Renderer*& tracyGpuOwner(); // the renderer with Tracy's GPU context
#endif
    std::string glInfo_;
    std::array<glm::vec4, 6> frustum_{}; // planes of the pass being drawn
    bool frustumSkipNear_ = false;
    bool visible(const Aabb& box) const;
    int culled_ = 0;

public:
    int lastCulled() const { return culled_; } // objects skipped by frustum culling in the last render()
    int lastDrawCalls() const { return lastDrawCalls_; } // geometry draw calls of the last render(), all passes
    int lastInstanced() const { return lastInstanced_; } // model copies drawn through instancing (main pass)
    int lastOccluded() const { return lastOccluded_; }   // objects skipped by occlusion culling (main pass)
};

} // namespace engine
