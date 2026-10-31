#pragma once
#include "engine/Scene.h"
#include <glm/mat4x4.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine {

// Matches glad's GLADloadfunc, so GLFW and Qt can both pass their loaders.
using GlProc = void (*)();
using GlLoader = GlProc (*)(const char* name);

struct RenderView {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 cameraPosition{0.0f};
    int viewportWidth = 1;
    int viewportHeight = 1;
    int selectedObject = -1;  // editor highlight (Scene object index), -1 = none
    bool drawPlayerStart = false;
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

    std::string glInfo() const { return glInfo_; }

private:
    struct GpuMesh {
        unsigned vao = 0, vbo = 0, ibo = 0;
        int indexCount = 0;
        bool used = false;
    };
    const GpuMesh& upload(const MeshPart& part);
    void releaseUnused();

    std::unordered_map<std::uint64_t, GpuMesh> meshes_; // keyed by MeshPart::id
    unsigned program_ = 0;
    unsigned vao_ = 0;
    unsigned vbo_ = 0;
    int vertexCount_ = 0;
    int locModel_ = -1, locViewProj_ = -1, locColor_ = -1, locSun_ = -1, locCamera_ = -1, locHighlight_ = -1;
    std::string glInfo_;
};

} // namespace engine
