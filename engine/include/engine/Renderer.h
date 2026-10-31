#pragma once
#include "engine/Scene.h"
#include <glm/mat4x4.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
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
    float preload(const Scene& scene, std::size_t& cursor, double budgetMs);

    void setQuality(const RenderQuality& quality) { quality_ = quality; }
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
        bool used = false;
    };
    struct GpuTexture {
        unsigned handle = 0;
        bool used = false;
    };
    struct Program {
        unsigned id = 0;
        mutable std::unordered_map<std::string, int> locations;
        int loc(const char* name) const;
    };
    struct Programs {
        Program mesh, depth, sky, envCapture, prefilter, brdf, ssao, blur, bloomDown, bloomUp, post, fxaa, copy, grid;
    };
    // Uniform locations of the program that draws geometry right now.
    struct MeshLocations {
        int model = -1, viewProj = -1, color = -1, highlight = -1, emissive = -1, skinned = -1, flash = -1;
        int tiling = -1, worldUv = -1, grid = -1, emissiveColor = -1, roughness = -1, metallic = -1;
        int normalStrength = -1, roughnessChannel = -1, metallicChannel = -1, overlay = -1, envRotation = -1;
        int overlaySun = -1, uvScale = -1, uvOffset = -1, uvRotation = -1, shading = -1;
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

    std::unordered_map<std::uint64_t, GpuTexture> textures_;
    std::unordered_map<std::uint64_t, GpuMesh> meshes_; // keyed by MeshPart::id
    Programs programs_;
    MeshLocations loc_;
    bool depthOnly_ = false;
    int texturesUploaded_ = 0;          // this frame; large uploads are spread over frames
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
};

} // namespace engine
