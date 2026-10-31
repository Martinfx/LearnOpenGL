#pragma once
#include "engine/Scene.h"
#include <glm/mat4x4.hpp>
#include <cstdint>
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

struct RenderView {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 cameraPosition{0.0f};
    int viewportWidth = 1;
    int viewportHeight = 1;
    int selectedObject = -1;  // editor highlight (Scene object index), -1 = none
    bool drawPlayerStart = false;
    std::vector<OverlayBox> worldBoxes; // e.g. bullet impacts
};

// First-person layer drawn on top of the world (weapon, muzzle flash, crosshair).
// Everything is in view space: camera at the origin looking down -Z.
struct OverlayPass {
    glm::mat4 projection{1.0f};
    glm::vec3 sunDirectionViewSpace{0.0f, -1.0f, 0.0f};
    const Model* model = nullptr;
    glm::mat4 modelTransform{1.0f};
    std::vector<OverlayBox> boxes;
};

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
    void renderOverlay(const RenderView& view, const OverlayPass& pass); // after render()

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
    const GpuMesh& upload(const MeshPart& part);
    unsigned texture(const Texture& tex);
    void drawModel(const Model& model, const glm::mat4& transform, float highlight);
    void drawCube(const glm::mat4& transform, glm::vec3 color, float highlight, bool emissive);
    void releaseUnused();

    std::unordered_map<std::uint64_t, GpuTexture> textures_;

    std::unordered_map<std::uint64_t, GpuMesh> meshes_; // keyed by MeshPart::id
    unsigned program_ = 0;
    unsigned vao_ = 0;
    unsigned vbo_ = 0;
    int vertexCount_ = 0;
    int locModel_ = -1, locViewProj_ = -1, locColor_ = -1, locSun_ = -1, locCamera_ = -1, locHighlight_ = -1;
    int locHasTexture_ = -1, locEmissive_ = -1, locFog_ = -1;
    std::string glInfo_;
};

} // namespace engine
