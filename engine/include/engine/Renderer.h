#pragma once
#include "engine/Scene.h"
#include <glm/mat4x4.hpp>
#include <array>
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

// How the main pass shades the scene (editor view modes).
enum class Shading { Lit, Unlit, LightingOnly, Wireframe };

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
    // The game draws the first-person layer before post-processing: it sets this,
    // calls renderOverlay() and then present().
    bool deferPresent = false;
    std::vector<OverlayBox> worldBoxes; // e.g. bullet impacts
    std::vector<OverlayBox> gizmoBoxes; // editor handles, drawn on top of everything (world space)
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
};

// Graphics options of the machine (not of the level): set from the editor's
// View menu or the game's settings.
struct RenderQuality {
    int shadowResolution = 2048;  // per cascade, 0 = no shadows
    int shadowCascades = 3;       // 1..3
    bool ssao = true;
    bool bloom = true;
    bool fxaa = true;
    int textureUploadsPerFrame = 6; // new textures sent to the GPU per frame, 0 = all at once
    // Memory. Textures larger than textureMaxSize drop their top mip levels (0 = full size).
    // Texture streaming: objects farther than textureStreamDistance get a small copy
    // (at most 256 px) on the GPU; the full one is read from the cook cache on a worker
    // thread when you come closer (0 = always full size).
    int textureMaxSize = 0;
    float textureStreamDistance = 40.0f;
    bool releaseCpuTextures = true; // drop CPU pixels once on the GPU (re-read from the cache if needed)
    bool instancing = true;         // copies of one model in one draw call per mesh part
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
    void renderOverlay(const RenderView& view, const OverlayPass& pass); // after render(), before present()
    void present(); // post-processing into the framebuffer that was bound when render() was called

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
        std::size_t savedBytes = 0; // vs. uncompressed full-size RGBA8 with mipmaps
    };
    MemoryStats memoryStats() const;

    // Frame profile: CPU time to record each pass and GPU time to execute it
    // (timer queries, a couple of frames behind), plus what was drawn.
    enum Pass { PassShadows, PassDepth, PassSsao, PassOpaque, PassTransparent, PassPost, PassOverlay, PassCount };
    static const char* passName(int pass);
    struct FrameStats {
        double cpuMs[PassCount] = {};
        double gpuMs[PassCount] = {};
        double cpuTotalMs = 0.0, gpuTotalMs = 0.0;
        int drawCalls = 0, instanced = 0, culled = 0;
        long long triangles = 0;
        int uploads = 0;
        int occluded = 0;        // hidden behind nearer geometry (main pass)         // meshes and textures sent to the GPU (should be ~0 when standing still)
        bool gpuValid = false;   // timer queries available on this driver
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

private:
    struct GpuMesh {
        unsigned vao = 0, vbo = 0, ibo = 0;
        int indexCount = 0;
        std::size_t bytes = 0;
        bool used = false;
    };
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
        Program mesh, depth, sky, envCapture, prefilter, brdf, ssao, blur, bloomDown, bloomUp, post, fxaa, copy, grid, hiz;
    };
    // Uniform locations of the program that draws geometry right now.
    struct MeshLocations {
        int model = -1, viewProj = -1, color = -1, highlight = -1, emissive = -1, skinned = -1, flash = -1;
        int tiling = -1, worldUv = -1, grid = -1, emissiveColor = -1, roughness = -1, metallic = -1;
        int normalStrength = -1, roughnessChannel = -1, metallicChannel = -1, overlay = -1, envRotation = -1;
        int overlaySun = -1, uvScale = -1, uvOffset = -1, uvRotation = -1, shading = -1;
        int alphaMode = -1, alphaCutoff = -1, opacity = -1, instanced = -1, instanceBase = -1;
        int hasMap[6] = {-1, -1, -1, -1, -1, -1};
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
    // Draws what intersects the frustum of `cull` (view-projection); skipNear keeps
    // shadow casters between the light and the cascade.
    void drawGeometry(const Scene& scene, const RenderView& view, bool depthOnly, const glm::mat4& cull, bool skipNear = false);
    void renderSsao();
    void renderBloom();

    const GpuMesh& upload(const MeshPart& part);
    unsigned texture(const Texture& tex);
    void drawModel(const Model& model, const glm::mat4& transform, float highlight, const Animator* animator = nullptr);
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
        std::vector<Aabb> bounds;         // world bounds per model
        std::vector<float> diagonal;
        std::vector<float> random;        // 0..1 per model (detail thinning)
        std::vector<std::uint8_t> detail; // grass-like model
        int rebuilds = 0;
    };
    ModelGrid grid_;
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
    };
    std::vector<InstanceBatch> batches_;
    std::unordered_map<std::uint64_t, std::size_t> batchIndex_;
    std::vector<glm::mat4> instanceData_;
    unsigned instanceBuffer_ = 0;
    std::size_t instanceCapacity_ = 0;
    int drawCalls_ = 0, lastDrawCalls_ = 0, instanced_ = 0, lastInstanced_ = 0;
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
    struct GpuTerrain {
        unsigned vao = 0, vbo = 0, ibo = 0, splat = 0;
        int indexCount = 0, resolution = 0, splatResolution = 0;
        std::uint64_t meshVersion = 0, splatVersion = 0;
    };
    GpuTerrain terrain_;
    void drawTerrain(const Terrain& terrain, const RenderView& view);
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
    void drawModelInstanced(const Model& model, int base, int count);
    bool collectTransparent_ = false;   // main pass: Blend parts go to transparent_
    void drawTransparent(const RenderView& view);
    bool s3tc_ = true, s3tcSrgb_ = true;
    unsigned vao_ = 0, vbo_ = 0, emptyVao_ = 0;
    int vertexCount_ = 0;
    unsigned jointBuffer_ = 0;          // uniform block with skinning matrices (binding 0)
    unsigned envBuffer_ = 0;            // Environment uniform block (binding 1)
    std::vector<glm::mat4> pose_;       // scratch: node matrices of the model being drawn
    std::vector<glm::mat4> joints_;     // scratch: skinning matrices

    // Render targets.
    int width_ = 0, height_ = 0;
    unsigned hdrFbo_ = 0, hdrColor_ = 0, depthTex_ = 0;
    unsigned ssaoFbo_ = 0, ssaoTex_ = 0, ssaoBlurFbo_ = 0, ssaoBlurTex_ = 0;
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
    unsigned whiteTex_ = 0;             // stand-in when SSAO is off
    unsigned shadowDummy_ = 0;          // stand-in when shadows are off

    RenderQuality quality_;
    const Environment* env_ = nullptr;  // of the scene being rendered
    float time_ = 0.0f;
    int targetFbo_ = 0;                 // framebuffer to present into
    int targetX_ = 0, targetY_ = 0;     // where in it
    bool pendingPresent_ = false;
    bool wireframe_ = false;
    bool ignoreUploadBudget_ = false;  // preload(): everything at once            // last render() was a wireframe view
    glm::mat4 view_{1.0f}, projection_{1.0f};
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
