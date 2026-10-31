#include "engine/Renderer.h"

#include <glad/gl.h>
#include "engine/JobSystem.h"
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

namespace engine {

namespace {

// S3TC sRGB formats come from EXT_texture_sRGB (not in glad's core header).
constexpr GLenum kSrgbDxt1 = 0x8C4C;      // GL_COMPRESSED_SRGB_S3TC_DXT1_EXT
constexpr GLenum kSrgbAlphaDxt5 = 0x8C4F; // GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT

struct SurfaceAlpha {
    AlphaMode mode = AlphaMode::Opaque;
    float cutoff = 0.5f;
    float opacity = 1.0f;
    bool doubleSided = false;
};
// An engine material (override) decides; otherwise the glTF material of the part.
SurfaceAlpha surfaceAlpha(const Material* material, const MeshPart* part)
{
    if (material)
        return {material->alphaMode, material->alphaCutoff, material->opacity, material->doubleSided};
    if (part)
        return {part->alphaMode, part->alphaCutoff, part->opacity, part->doubleSided};
    return {};
}

// Metres from a point to a box (0 inside): texture streaming distance.
float distanceTo(glm::vec3 p, const Aabb& b)
{
    return glm::length(glm::max(glm::max(b.min - p, p - b.max), glm::vec3(0.0f)));
}

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec2 uv{0.0f};
    glm::u16vec4 joints{0};
    glm::vec4 weights{0.0f};
};

constexpr int kMaxJoints = 256; // size of the Joints uniform block

void setupVertexArray(unsigned vao, unsigned vbo)
{
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
    for (unsigned attr = 0; attr < 5; ++attr) {
        glEnableVertexArrayAttrib(vao, attr);
        glVertexArrayAttribBinding(vao, attr, 0);
    }
    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribFormat(vao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    glVertexArrayAttribIFormat(vao, 3, 4, GL_UNSIGNED_SHORT, offsetof(Vertex, joints));
    glVertexArrayAttribFormat(vao, 4, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, weights));
}

Vertex vtx(glm::vec3 position, glm::vec3 normal)
{
    Vertex v;
    v.position = position;
    v.normal = normal;
    return v;
}

std::vector<Vertex> unitCube()
{
    // 6 faces x 2 triangles, counter-clockwise when viewed from outside.
    const glm::vec3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    std::vector<Vertex> v;
    v.reserve(36);
    for (const glm::vec3& normal : n) {
        // Two axes spanning the face, oriented so u x w == normal.
        const glm::vec3 u = glm::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) * normal.y : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(normal, u);
        const glm::vec3 c = normal * 0.5f;
        const glm::vec3 a = c - u * 0.5f - w * 0.5f;
        const glm::vec3 b = c + u * 0.5f - w * 0.5f;
        const glm::vec3 d = c + u * 0.5f + w * 0.5f;
        const glm::vec3 e = c - u * 0.5f + w * 0.5f;
        // Pick winding so the triangle normal matches the face normal.
        if (glm::dot(glm::cross(b - a, d - a), normal) > 0.0f) {
            v.insert(v.end(), {vtx(a, normal), vtx(b, normal), vtx(d, normal), vtx(a, normal), vtx(d, normal), vtx(e, normal)});
        } else {
            v.insert(v.end(), {vtx(a, normal), vtx(d, normal), vtx(b, normal), vtx(a, normal), vtx(e, normal), vtx(d, normal)});
        }
    }
    return v;
}

} // namespace

namespace {

// Uniform block "Environment" (binding 1), std140 layout as in common.glsl.
struct EnvironmentBlock {
    glm::vec4 sunDir;
    glm::vec4 sunColor;
    glm::vec4 skyZenith;
    glm::vec4 skyHorizon;
    glm::vec4 groundColor;
    glm::vec4 fogColor;
    glm::vec4 fogParams;
    glm::vec4 cameraPos;
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 invViewProj;
    glm::mat4 shadowMatrices[3];
    glm::vec4 cascadeSplits;
    glm::vec4 viewport;
    glm::vec4 shadowParams;
};

constexpr int kEnvSize = 128;  // environment cubemap face size
constexpr int kEnvMips = 6;    // 128 .. 4, roughness 0 .. 1
constexpr int kBloomLevels = 6;

glm::vec3 linear(glm::vec3 srgb)
{
    return glm::pow(glm::max(srgb, glm::vec3(0.0f)), glm::vec3(2.2f));
}

std::string withIncludes(const std::string& source, const std::map<std::string, std::string>& files)
{
    // #include "file" for shared GLSL code. #line directives keep compiler messages
    // pointing at the right file: source string 0 is the shader itself, 1.. its
    // includes in order, with their own line numbers.
    std::string out;
    std::size_t pos = 0;
    int includeIndex = 0;
    while (true) {
        const std::size_t inc = source.find("#include \"", pos);
        if (inc == std::string::npos) {
            out += source.substr(pos);
            return out;
        }
        out += source.substr(pos, inc - pos);
        const std::size_t nameStart = inc + 10;
        const std::size_t nameEnd = source.find('"', nameStart);
        const std::string name = source.substr(nameStart, nameEnd - nameStart);
        const auto it = files.find(name);
        const int line = 1 + static_cast<int>(std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(inc), '\n'));
        out += "#line 1 " + std::to_string(++includeIndex) + "\n";
        out += it != files.end() ? it->second : "#error missing include " + name + "\n";
        out += "\n#line " + std::to_string(line + 1) + " 0\n";
        pos = source.find('\n', nameEnd);
        if (pos == std::string::npos)
            return out;
        ++pos;
    }
}

unsigned compileStage(GLenum type, const std::string& source, const std::string& name, std::string* error)
{
    const unsigned shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::array<char, 4096> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        if (error)
            *error = name + ": " + log.data();
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

unsigned linkProgram(const std::map<std::string, std::string>& files, const std::string& vertName,
                     const std::string& fragName, std::string* error)
{
    const auto vs = files.find(vertName), fs = files.find(fragName);
    if (vs == files.end() || fs == files.end()) {
        if (error)
            *error = "Missing shader " + (vs == files.end() ? vertName : fragName);
        return 0;
    }
    const unsigned v = compileStage(GL_VERTEX_SHADER, withIncludes(vs->second, files), vertName, error);
    const unsigned f = v ? compileStage(GL_FRAGMENT_SHADER, withIncludes(fs->second, files), fragName, error) : 0;
    if (!v || !f) {
        glDeleteShader(v);
        return 0;
    }
    const unsigned program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        std::array<char, 4096> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        if (error)
            *error = vertName + " + " + fragName + ": " + log.data();
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

unsigned makeTexture2D(GLenum format, int w, int h, GLint filter, int levels = 1)
{
    unsigned t = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &t);
    glTextureStorage2D(t, levels, format, w, h);
    glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, filter);
    glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, filter == GL_LINEAR_MIPMAP_LINEAR ? GL_LINEAR : filter);
    glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

unsigned makeFbo(unsigned color, unsigned depth = 0)
{
    unsigned f = 0;
    glCreateFramebuffers(1, &f);
    if (color)
        glNamedFramebufferTexture(f, GL_COLOR_ATTACHMENT0, color, 0);
    if (depth)
        glNamedFramebufferTexture(f, GL_DEPTH_ATTACHMENT, depth, 0);
    return f;
}

// Cubemap face orientation (right, up, forward) for GL's face order.
glm::mat3 cubeFace(int face)
{
    switch (face) {
    case 0: return {glm::vec3(0, 0, -1), glm::vec3(0, -1, 0), glm::vec3(1, 0, 0)};
    case 1: return {glm::vec3(0, 0, 1), glm::vec3(0, -1, 0), glm::vec3(-1, 0, 0)};
    case 2: return {glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0)};
    case 3: return {glm::vec3(1, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, -1, 0)};
    case 4: return {glm::vec3(1, 0, 0), glm::vec3(0, -1, 0), glm::vec3(0, 0, 1)};
    default: return {glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0), glm::vec3(0, 0, -1)};
    }
}

template <class T>
void hashCombine(std::size_t& h, const T& v)
{
    h ^= std::hash<T>{}(v) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
}

} // namespace

const std::map<std::string, std::string>& embeddedShaders(); // generated from engine/shaders/

RenderQuality RenderQuality::preset(int level)
{
    RenderQuality q;
    switch (level) {
    case 0:
        q.shadowResolution = 1024; q.shadowCascades = 2; q.ssao = false; q.bloom = false; q.fxaa = true;
        q.textureMaxSize = 1024; q.textureStreamDistance = 20.0f;
        break;
    case 1:
        q.shadowResolution = 1024; q.shadowCascades = 3; q.ssao = true; q.bloom = true;
        q.textureMaxSize = 2048; q.textureStreamDistance = 30.0f;
        break;
    case 3: q.shadowResolution = 4096; q.shadowCascades = 3; q.textureStreamDistance = 80.0f; break;
    default: break; // high
    }
    return q;
}

int Renderer::Program::loc(const char* name) const
{
    auto it = locations.find(name);
    if (it == locations.end())
        it = locations.emplace(name, glGetUniformLocation(id, name)).first;
    return it->second;
}

const std::map<std::string, std::string>& Renderer::builtinShaders()
{
    return embeddedShaders();
}

Renderer::~Renderer()
{
    // GL objects must be freed with a current context; owners call shutdown().
}

bool Renderer::buildPrograms(const std::map<std::string, std::string>& src, Programs& p, std::string* error)
{
    struct Entry {
        Program* program;
        const char* vert;
        const char* frag;
    };
    const Entry entries[] = {{&p.mesh, "mesh.vert", "pbr.frag"},          {&p.depth, "mesh.vert", "depth.frag"},
                             {&p.sky, "fullscreen.vert", "sky.frag"},     {&p.envCapture, "fullscreen.vert", "envcapture.frag"},
                             {&p.prefilter, "fullscreen.vert", "prefilter.frag"}, {&p.brdf, "fullscreen.vert", "brdf.frag"},
                             {&p.ssao, "fullscreen.vert", "ssao.frag"},   {&p.blur, "fullscreen.vert", "blur.frag"},
                             {&p.bloomDown, "fullscreen.vert", "bloom_down.frag"},
                             {&p.bloomUp, "fullscreen.vert", "bloom_up.frag"}, {&p.post, "fullscreen.vert", "post.frag"},
                             {&p.fxaa, "fullscreen.vert", "fxaa.frag"},   {&p.copy, "fullscreen.vert", "copy.frag"},
                             {&p.grid, "fullscreen.vert", "grid.frag"}};
    for (const Entry& e : entries) {
        e.program->id = linkProgram(src, e.vert, e.frag, error);
        e.program->locations.clear();
        if (!e.program->id) {
            destroyPrograms(p);
            return false;
        }
    }
    return true;
}

void Renderer::destroyPrograms(Programs& p)
{
    for (Program* program : {&p.mesh, &p.depth, &p.sky, &p.envCapture, &p.prefilter, &p.brdf, &p.ssao, &p.blur, &p.bloomDown,
                             &p.bloomUp, &p.post, &p.fxaa, &p.copy, &p.grid}) {
        if (program->id)
            glDeleteProgram(program->id);
        program->id = 0;
        program->locations.clear();
    }
}

bool Renderer::reloadShaders(const std::filesystem::path& directory, std::string* error)
{
    std::map<std::string, std::string> files = embeddedShaders();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file())
            continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        files[entry.path().filename().string()] = text.str();
    }
    Programs fresh;
    if (!buildPrograms(files, fresh, error))
        return false;
    destroyPrograms(programs_);
    programs_ = fresh;
    envHash_ = 0; // re-run the environment precomputation with the new shaders
    return true;
}

void Renderer::useMeshProgram(const Program& p)
{
    glUseProgram(p.id);
    loc_.model = p.loc("uModel");
    loc_.viewProj = p.loc("uViewProj");
    loc_.color = p.loc("uColor");
    loc_.highlight = p.loc("uHighlight");
    loc_.emissive = p.loc("uEmissive");
    loc_.skinned = p.loc("uSkinned");
    loc_.flash = p.loc("uFlash");
    loc_.tiling = p.loc("uTiling");
    loc_.worldUv = p.loc("uWorldUv");
    loc_.uvScale = p.loc("uUvScale");
    loc_.uvOffset = p.loc("uUvOffset");
    loc_.uvRotation = p.loc("uUvRotation");
    loc_.shading = p.loc("uShading");
    loc_.grid = p.loc("uGrid");
    loc_.emissiveColor = p.loc("uEmissiveColor");
    loc_.roughness = p.loc("uRoughness");
    loc_.metallic = p.loc("uMetallic");
    loc_.normalStrength = p.loc("uNormalStrength");
    loc_.roughnessChannel = p.loc("uRoughnessChannel");
    loc_.metallicChannel = p.loc("uMetallicChannel");
    loc_.overlay = p.loc("uOverlay");
    loc_.envRotation = p.loc("uEnvRotation");
    loc_.overlaySun = p.loc("uOverlaySunDir");
    loc_.alphaMode = p.loc("uAlphaMode");
    loc_.alphaCutoff = p.loc("uAlphaCutoff");
    loc_.opacity = p.loc("uOpacity");
    const char* names[6] = {"uHasTexture", "uHasNormalMap", "uHasRoughnessMap", "uHasMetallicMap", "uHasOcclusionMap", "uHasEmissiveMap"};
    for (int i = 0; i < 6; ++i)
        loc_.hasMap[i] = p.loc(names[i]);
}

bool Renderer::init(GlLoader loader, std::string* error)
{
    const int version = gladLoadGL(reinterpret_cast<GLADloadfunc>(loader));
    if (version == 0 || GLAD_VERSION_MAJOR(version) * 10 + GLAD_VERSION_MINOR(version) < 45) {
        if (error)
            *error = "OpenGL 4.5 or newer is required";
        return false;
    }
    // Block-compressed textures need S3TC (every desktop driver has it); without it the cooker keeps RGBA8.
    s3tc_ = GLAD_GL_EXT_texture_compression_s3tc != 0;
    if (s3tc_) {
        GLint ok = 0;
        glGetInternalformativ(GL_TEXTURE_2D, kSrgbDxt1, GL_INTERNALFORMAT_SUPPORTED, 1, &ok);
        s3tcSrgb_ = ok == GL_TRUE || GLAD_GL_EXT_texture_sRGB;
    }
    if (!s3tc_ || !s3tcSrgb_)
        textureCookConfig().compress = false;
    glInfo_ = std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))) + " / OpenGL "
            + reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (!buildPrograms(embeddedShaders(), programs_, error))
        return false;

    glCreateBuffers(1, &jointBuffer_);
    glNamedBufferData(jointBuffer_, kMaxJoints * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);
    glCreateBuffers(1, &envBuffer_);
    glNamedBufferData(envBuffer_, sizeof(EnvironmentBlock), nullptr, GL_DYNAMIC_DRAW);

    const std::vector<Vertex> cube = unitCube();
    vertexCount_ = static_cast<int>(cube.size());
    glCreateBuffers(1, &vbo_);
    glNamedBufferStorage(vbo_, static_cast<GLsizeiptr>(cube.size() * sizeof(Vertex)), cube.data(), 0);
    glCreateVertexArrays(1, &vao_);
    setupVertexArray(vao_, vbo_);
    glCreateVertexArrays(1, &emptyVao_);

    // 1x1 stand-ins so samplers are always valid.
    whiteTex_ = makeTexture2D(GL_R8, 1, 1, GL_NEAREST);
    const std::uint8_t white = 255;
    glTextureSubImage2D(whiteTex_, 0, 0, 0, 1, 1, GL_RED, GL_UNSIGNED_BYTE, &white);
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &shadowDummy_);
    glTextureStorage3D(shadowDummy_, 1, GL_DEPTH_COMPONENT32F, 1, 1, 1);
    glTextureParameteri(shadowDummy_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);

    // Split-sum BRDF table, computed once.
    brdfLut_ = makeTexture2D(GL_RG16F, 128, 128, GL_LINEAR);
    glCreateFramebuffers(1, &captureFbo_);
    glNamedFramebufferTexture(captureFbo_, GL_COLOR_ATTACHMENT0, brdfLut_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFbo_);
    glViewport(0, 0, 128, 128);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(programs_.brdf.id);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Environment cubemaps (filled when the sky changes).
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &envRaw_);
    glTextureStorage2D(envRaw_, kEnvMips, GL_RGBA16F, kEnvSize, kEnvSize);
    glTextureParameteri(envRaw_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(envRaw_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &envMap_);
    glTextureStorage2D(envMap_, kEnvMips, GL_RGBA16F, kEnvSize, kEnvSize);
    glTextureParameteri(envMap_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(envMap_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    return true;
}

void Renderer::shutdown()
{
    for (auto& [id, m] : meshes_) {
        glDeleteVertexArrays(1, &m.vao);
        glDeleteBuffers(1, &m.vbo);
        glDeleteBuffers(1, &m.ibo);
    }
    meshes_.clear();
    for (auto& [id, t] : textures_)
        glDeleteTextures(1, &t.handle);
    textures_.clear();
    destroyTargets();
    destroyPrograms(programs_);
    for (unsigned* t : {&shadowTex_, &envRaw_, &envMap_, &brdfLut_, &whiteTex_, &shadowDummy_})
        if (*t) {
            glDeleteTextures(1, t);
            *t = 0;
        }
    for (unsigned* f : {&shadowFbo_, &captureFbo_})
        if (*f) {
            glDeleteFramebuffers(1, f);
            *f = 0;
        }
    for (unsigned* b : {&vbo_, &jointBuffer_, &envBuffer_})
        if (*b) {
            glDeleteBuffers(1, b);
            *b = 0;
        }
    for (unsigned* a : {&vao_, &emptyVao_})
        if (*a) {
            glDeleteVertexArrays(1, a);
            *a = 0;
        }
    shadowRes_ = 0;
    envHash_ = 0;
}

void Renderer::destroyTargets()
{
    for (unsigned* t : {&hdrColor_, &depthTex_, &ssaoTex_, &ssaoBlurTex_, &ldrTex_})
        if (*t) {
            glDeleteTextures(1, t);
            *t = 0;
        }
    for (unsigned* f : {&hdrFbo_, &ssaoFbo_, &ssaoBlurFbo_, &ldrFbo_})
        if (*f) {
            glDeleteFramebuffers(1, f);
            *f = 0;
        }
    for (unsigned t : bloomTex_)
        glDeleteTextures(1, &t);
    for (unsigned f : bloomFbo_)
        glDeleteFramebuffers(1, &f);
    bloomTex_.clear();
    bloomFbo_.clear();
    bloomSize_.clear();
    width_ = height_ = 0;
}

void Renderer::ensureTargets(int width, int height)
{
    if (width == width_ && height == height_ && hdrFbo_)
        return;
    destroyTargets();
    width_ = width;
    height_ = height;
    hdrColor_ = makeTexture2D(GL_RGBA16F, width, height, GL_LINEAR);
    depthTex_ = makeTexture2D(GL_DEPTH_COMPONENT32F, width, height, GL_NEAREST);
    hdrFbo_ = makeFbo(hdrColor_, depthTex_);
    ssaoTex_ = makeTexture2D(GL_R8, width, height, GL_LINEAR);
    ssaoFbo_ = makeFbo(ssaoTex_);
    ssaoBlurTex_ = makeTexture2D(GL_R8, width, height, GL_LINEAR);
    ssaoBlurFbo_ = makeFbo(ssaoBlurTex_);
    ldrTex_ = makeTexture2D(GL_RGBA8, width, height, GL_LINEAR);
    ldrFbo_ = makeFbo(ldrTex_);
    glm::ivec2 size(width, height);
    for (int i = 0; i < kBloomLevels; ++i) {
        size = glm::max(size / 2, glm::ivec2(1));
        const unsigned t = makeTexture2D(GL_R11F_G11F_B10F, size.x, size.y, GL_LINEAR);
        bloomTex_.push_back(t);
        bloomFbo_.push_back(makeFbo(t));
        bloomSize_.push_back(size);
    }
}

void Renderer::ensureShadowMap()
{
    const int res = quality_.shadowResolution;
    if (res == shadowRes_)
        return;
    if (shadowTex_)
        glDeleteTextures(1, &shadowTex_);
    if (shadowFbo_)
        glDeleteFramebuffers(1, &shadowFbo_);
    shadowTex_ = shadowFbo_ = 0;
    shadowRes_ = res;
    if (res <= 0)
        return;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &shadowTex_);
    glTextureStorage3D(shadowTex_, 1, GL_DEPTH_COMPONENT32F, res, res, 3);
    glTextureParameteri(shadowTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(shadowTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(shadowTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(shadowTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTextureParameterfv(shadowTex_, GL_TEXTURE_BORDER_COLOR, border);
    glTextureParameteri(shadowTex_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(shadowTex_, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glCreateFramebuffers(1, &shadowFbo_);
}

void Renderer::updateEnvironment(const Scene& scene, const RenderView& view)
{
    const Environment& e = scene.environment;
    EnvironmentBlock b{};
    b.sunDir = glm::vec4(glm::normalize(scene.sunDirection), e.sunIntensity);
    b.sunColor = glm::vec4(linear(e.sunColor), quality_.shadowResolution > 0 ? 1.0f : 0.0f);
    b.skyZenith = glm::vec4(linear(e.skyZenith), e.cloudCover);
    b.skyHorizon = glm::vec4(linear(e.skyHorizon), time_);
    b.groundColor = glm::vec4(linear(e.groundColor), e.ambientIntensity);
    b.fogColor = glm::vec4(linear(e.fogColor), view.fog ? e.fogDensity : 0.0f);
    b.fogParams = glm::vec4(e.fogHeight, e.fogHeightFalloff, e.fogSunScatter, e.ssaoIntensity);
    b.cameraPos = glm::vec4(view.cameraPosition, quality_.ssao ? 1.0f : 0.0f);
    b.view = view.view;
    b.proj = view.projection;
    b.invViewProj = glm::inverse(view.projection * view.view);

    // Cascade splits: mix of uniform and logarithmic distribution.
    const int cascades = std::clamp(quality_.shadowCascades, 1, 3);
    const float nearPlane = 0.1f, farPlane = std::max(5.0f, e.shadowDistance);
    float splits[3] = {farPlane, farPlane, farPlane};
    for (int i = 0; i < cascades; ++i) {
        const float f = static_cast<float>(i + 1) / static_cast<float>(cascades);
        const float logSplit = nearPlane * std::pow(farPlane / nearPlane, f);
        const float uniformSplit = nearPlane + (farPlane - nearPlane) * f;
        splits[i] = glm::mix(uniformSplit, logSplit, 0.8f);
    }
    b.cascadeSplits = glm::vec4(splits[0], splits[1], splits[2], static_cast<float>(cascades));
    const glm::vec3 lightDir = glm::normalize(scene.sunDirection);
    const glm::mat4 invViewProj = b.invViewProj;
    float prev = nearPlane;
    for (int i = 0; i < cascades; ++i) {
        // Bounding sphere of the frustum slice: stable size while the camera turns.
        auto ndcDepth = [&](float d) {
            const glm::vec4 clip = view.projection * glm::vec4(0.0f, 0.0f, -d, 1.0f);
            return clip.z / clip.w;
        };
        const float z0 = ndcDepth(prev), z1 = ndcDepth(splits[i]);
        glm::vec3 corners[8];
        glm::vec3 center(0.0f);
        for (int c = 0; c < 8; ++c) {
            const glm::vec4 p = invViewProj * glm::vec4((c & 1) ? 1.0f : -1.0f, (c & 2) ? 1.0f : -1.0f, (c & 4) ? z1 : z0, 1.0f);
            corners[c] = glm::vec3(p) / p.w;
            center += corners[c] / 8.0f;
        }
        float radius = 0.0f;
        for (const glm::vec3& c : corners)
            radius = std::max(radius, glm::length(c - center));
        radius = std::ceil(radius * 16.0f) / 16.0f;
        const glm::vec3 up = std::abs(lightDir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::mat4 lightView = glm::lookAt(center - lightDir * (radius + 80.0f), center, up);
        glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + 160.0f);
        // Snap to shadow map texels so edges do not shimmer when the camera moves.
        if (quality_.shadowResolution > 0) {
            const glm::mat4 m = lightProj * lightView;
            glm::vec4 origin = m * glm::vec4(0, 0, 0, 1) * (static_cast<float>(quality_.shadowResolution) * 0.5f);
            const glm::vec4 rounded = glm::round(origin);
            glm::vec4 offset = (rounded - origin) * (2.0f / static_cast<float>(quality_.shadowResolution));
            lightProj[3][0] += offset.x;
            lightProj[3][1] += offset.y;
        }
        b.shadowMatrices[i] = lightProj * lightView;
        if (i == 0) // world size of one texel of the first cascade drives the biases
            b.shadowParams.z = 1.0f / static_cast<float>(std::max(1, quality_.shadowResolution));
        b.shadowParams.y = 2.0f * radius / static_cast<float>(std::max(1, quality_.shadowResolution)) * (i == 0 ? 1.5f : b.shadowParams.y);
        prev = splits[i];
    }
    b.shadowParams.x = 0.0005f;
    b.shadowParams.y = 2.0f * (splits[0]) / static_cast<float>(std::max(1, quality_.shadowResolution)) * 1.5f;
    b.shadowParams.w = static_cast<float>(kEnvMips - 1);
    b.viewport = glm::vec4(view.viewportWidth, view.viewportHeight, 1.0f / view.viewportWidth, 1.0f / view.viewportHeight);
    glNamedBufferSubData(envBuffer_, 0, sizeof(b), &b);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, envBuffer_);
}

void Renderer::updateEnvironmentMap(const Scene& scene)
{
    // Re-render the sky into the cubemap and prefilter it when the sky changed.
    const Environment& e = scene.environment;
    std::size_t h = 1;
    for (float v : {e.sunColor.r, e.sunColor.g, e.sunColor.b, e.sunIntensity, e.skyZenith.r, e.skyZenith.g, e.skyZenith.b,
                    e.skyHorizon.r, e.skyHorizon.g, e.skyHorizon.b, e.groundColor.r, e.groundColor.g, e.groundColor.b,
                    e.cloudCover, scene.sunDirection.x, scene.sunDirection.y, scene.sunDirection.z})
        hashCombine(h, v);
    if (h == envHash_)
        return;
    envHash_ = h;
    // Capture without cloud drift (time 0) so reflections do not need updates every frame.
    EnvironmentBlock captureBlock{};
    glGetNamedBufferSubData(envBuffer_, 0, sizeof(captureBlock), &captureBlock);
    captureBlock.skyHorizon.w = 0.0f;
    glNamedBufferSubData(envBuffer_, 0, sizeof(captureBlock), &captureBlock);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBindVertexArray(emptyVao_);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFbo_);
    glUseProgram(programs_.envCapture.id);
    glViewport(0, 0, kEnvSize, kEnvSize);
    for (int face = 0; face < 6; ++face) {
        glNamedFramebufferTextureLayer(captureFbo_, GL_COLOR_ATTACHMENT0, envRaw_, 0, face);
        const glm::mat3 m = cubeFace(face);
        glUniformMatrix3fv(programs_.envCapture.loc("uFace"), 1, GL_FALSE, glm::value_ptr(m));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glGenerateTextureMipmap(envRaw_);
    glUseProgram(programs_.prefilter.id);
    glBindTextureUnit(0, envRaw_);
    for (int mip = 0; mip < kEnvMips; ++mip) {
        const int size = std::max(1, kEnvSize >> mip);
        glViewport(0, 0, size, size);
        glUniform1f(programs_.prefilter.loc("uRoughness"), static_cast<float>(mip) / static_cast<float>(kEnvMips - 1));
        for (int face = 0; face < 6; ++face) {
            glNamedFramebufferTextureLayer(captureFbo_, GL_COLOR_ATTACHMENT0, envMap_, mip, face);
            const glm::mat3 m = cubeFace(face);
            glUniformMatrix3fv(programs_.prefilter.loc("uFace"), 1, GL_FALSE, glm::value_ptr(m));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }
    captureBlock.skyHorizon.w = time_;
    glNamedBufferSubData(envBuffer_, 0, sizeof(captureBlock), &captureBlock);
}

bool Renderer::visible(const Aabb& box) const
{
    for (int i = 0; i < 6; ++i) {
        if (frustumSkipNear_ && i == 4)
            continue;
        const glm::vec4& p = frustum_[static_cast<std::size_t>(i)];
        // Corner of the box furthest along the plane normal.
        const glm::vec3 corner(p.x >= 0.0f ? box.max.x : box.min.x, p.y >= 0.0f ? box.max.y : box.min.y,
                               p.z >= 0.0f ? box.max.z : box.min.z);
        if (glm::dot(glm::vec3(p), corner) + p.w < 0.0f)
            return false;
    }
    return true;
}

void Renderer::drawGeometry(const Scene& scene, const RenderView& view, bool depthOnly, const glm::mat4& cull, bool skipNear)
{
    // Frustum planes (Gribb/Hartmann): left, right, bottom, top, near, far.
    const glm::mat4 t = glm::transpose(cull);
    frustum_ = {t[3] + t[0], t[3] - t[0], t[3] + t[1], t[3] - t[1], t[3] + t[2], t[3] - t[2]};
    frustumSkipNear_ = skipNear;
    depthOnly_ = depthOnly;
    glUniform1i(loc_.emissive, 0);
    glUniform1i(loc_.skinned, 0);
    glUniform1f(loc_.flash, 0.0f);
    glUniform1i(loc_.overlay, 0);
    auto drawBox = [&](glm::vec3 center, glm::vec3 size, glm::vec3 color, float highlight, float yawDegrees = 0.0f,
                       const Material* material = nullptr, const Box* uvBox = nullptr) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
        if (yawDegrees != 0.0f)
            model = glm::rotate(model, -glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, size);
        glUniformMatrix4fv(loc_.model, 1, GL_FALSE, glm::value_ptr(model));
        applySurface(material, nullptr, color, true); // boxes have no UVs: world-space mapping
        if (uvBox)
            applyUv(*uvBox);
        glUniform1f(loc_.highlight, highlight);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    };
    for (std::size_t i = 0; i < scene.boxes.size(); ++i) {
        const Box& b = scene.boxes[i];
        if (!visible(b.bounds())) {
            if (b.material) { // keep its maps on the GPU while out of view
                drawDistance_ = distanceTo(view.cameraPosition, b.bounds());
                for (const auto& asset : b.material->maps)
                    if (asset)
                        keepTextureAlive(asset->texture.get());
            }
            ++culled_;
            continue;
        }
        drawDistance_ = distanceTo(view.cameraPosition, b.bounds());
        if (b.material && b.material->alphaMode == AlphaMode::Blend) {
            if (!depthOnly && collectTransparent_)
                transparent_.push_back({nullptr, b.material.get(), &b, b.transform(), drawDistance_,
                                        static_cast<int>(i) == view.selectedObject ? 1.0f : 0.0f});
            if (depthOnly || collectTransparent_)
                continue;
        }
        const bool boxTwoSided = b.material && b.material->doubleSided && glIsEnabled(GL_CULL_FACE);
        if (boxTwoSided)
            glDisable(GL_CULL_FACE);
        struct RestoreCull {
            bool on;
            ~RestoreCull() { if (on) glEnable(GL_CULL_FACE); }
        } restoreCull{boxTwoSided};
        if (b.rotated()) {
            glUniformMatrix4fv(loc_.model, 1, GL_FALSE, glm::value_ptr(b.transform()));
            applySurface(b.material.get(), nullptr, b.color, true);
            applyUv(b);
            glUniform1f(loc_.highlight, static_cast<int>(i) == view.selectedObject ? 1.0f : 0.0f);
            glBindVertexArray(vao_);
            glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
            continue;
        }
        drawBox(b.position, b.size, b.color, static_cast<int>(i) == view.selectedObject ? 1.0f : 0.0f, 0.0f, b.material.get(), &b);
    }
    for (std::size_t i = 0; i < scene.models.size(); ++i) {
        const Model& model = scene.models[i];
        drawDistance_ = distanceTo(view.cameraPosition, model.bounds());
        const bool tooFar = model.cullDistance > 0.0f && drawDistance_ > model.cullDistance;
        if (tooFar || !visible(model.bounds())) {
            keepAlive(model); // stays uploaded while out of view
            if (model.lod)
                keepAlive(*model.lod);
            ++culled_;
            continue;
        }
        const bool selected = static_cast<int>(scene.boxes.size() + i) == view.selectedObject;
        // Distance LOD: the simpler copy beyond lodDistance (the other one stays uploaded).
        const Model* draw = &model;
        if (model.lod) {
            const Aabb b = model.bounds();
            const bool far = glm::distance(view.cameraPosition, (b.min + b.max) * 0.5f) > model.lodDistance;
            draw = far ? model.lod.get() : &model;
            keepAlive(far ? model : *model.lod);
        }
        drawModel(*draw, model.transform(), selected ? 1.0f : 0.0f);
    }
    drawDistance_ = 0.0f; // weapon in hand, previews
    for (const auto& [extra, transform] : view.extraModels)
        if (extra)
            drawModel(*extra, transform, 0.0f);
    if (!depthOnly)
        for (const OverlayBox& b : view.worldBoxes)
            drawCube(glm::scale(glm::translate(glm::mat4(1.0f), b.center), b.size), b.color, 0.0f, b.emissive);
    glBindVertexArray(vao_);
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const Entity& e = scene.entities[i];
        if (!view.editorHelpers && e.type == "map_marker")
            continue; // only a point on the map in the game
        const float highlight = static_cast<int>(scene.boxes.size() + scene.models.size() + i) == view.selectedObject ? 1.0f : 0.0f;
        // Generous box: animations reach outside the rest pose.
        const float reach = std::max({e.size.x, e.size.y, e.size.z}) * 1.5f;
        drawDistance_ = std::max(0.0f, glm::distance(view.cameraPosition, e.position) - reach);
        if (!visible(Aabb::fromCenterSize(e.position + glm::vec3(0.0f, e.size.y * 0.5f, 0.0f), glm::vec3(reach)))) {
            if (e.model)
                keepAlive(*e.model);
            ++culled_;
            continue;
        }
        if (e.model) {
            // Animated character; stays as a corpse after death.
            glUniform1f(loc_.flash, std::min(1.0f, e.flash * 8.0f));
            drawModel(*e.model, e.modelTransform(), highlight, &e.animator);
            glUniform1f(loc_.flash, 0.0f);
            glBindVertexArray(vao_);
            continue;
        }
        if (!e.alive)
            continue;
        glUniform1f(loc_.flash, std::min(1.0f, e.flash * 8.0f));
        drawBox(e.position + glm::vec3(0.0f, e.size.y * 0.5f, 0.0f), e.size, e.color, highlight, e.yawDegrees);
        glUniform1f(loc_.flash, 0.0f);
        // A small "nose" shows which way the entity faces.
        const float yaw = glm::radians(e.yawDegrees);
        const glm::vec3 facing(std::cos(yaw), 0.0f, std::sin(yaw));
        const glm::vec3 nose = e.position + glm::vec3(0.0f, e.size.y * 0.8f, 0.0f) + facing * (e.size.x * 0.5f + 0.1f);
        drawBox(nose, glm::vec3(0.2f, 0.12f, 0.2f), glm::vec3(0.95f, 0.9f, 0.3f), highlight, e.yawDegrees);
    }
    if (view.drawPlayerStart && !depthOnly) {
        // Player-sized marker so level designers see the spawn and its scale.
        drawBox(scene.playerStart + glm::vec3(0.0f, 0.9f, 0.0f), glm::vec3(0.6f, 1.8f, 0.6f), glm::vec3(0.2f, 0.8f, 0.3f), 0.0f);
    }
    depthOnly_ = false;
}

void Renderer::renderShadows(const Scene& scene, const RenderView& view)
{
    ensureShadowMap();
    if (!shadowTex_)
        return;
    EnvironmentBlock b{};
    glGetNamedBufferSubData(envBuffer_, 0, sizeof(b), &b);
    useMeshProgram(programs_.depth);
    glViewport(0, 0, shadowRes_, shadowRes_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE); // open meshes (foliage, planes) cast shadows too
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    const int cascades = static_cast<int>(b.cascadeSplits.w);
    for (int i = 0; i < cascades; ++i) {
        glNamedFramebufferTextureLayer(shadowFbo_, GL_DEPTH_ATTACHMENT, shadowTex_, 0, i);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(b.shadowMatrices[i]));
        RenderView shadowView = view;
        shadowView.drawPlayerStart = false;
        drawGeometry(scene, shadowView, true, b.shadowMatrices[i], true);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_CULL_FACE);
}

void Renderer::renderSsao()
{
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(emptyVao_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFbo_);
    glViewport(0, 0, width_, height_);
    glUseProgram(programs_.ssao.id);
    glUniform1f(programs_.ssao.loc("uRadius"), env_ ? env_->ssaoRadius : 0.6f);
    glUniformMatrix4fv(programs_.ssao.loc("uInvProj"), 1, GL_FALSE, glm::value_ptr(glm::inverse(projection_)));
    glBindTextureUnit(0, depthTex_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFbo_);
    glUseProgram(programs_.blur.id);
    glBindTextureUnit(0, ssaoTex_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

namespace {
// Host toolkits (Qt's QPainter over the editor viewport) leave blending, scissor
// and masks enabled between frames; every pass here assumes the defaults.
void resetGlState()
{
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glFrontFace(GL_CCW);
}
} // namespace

void Renderer::render(const Scene& scene, const RenderView& view)
{
    resetGlState();
    // Free GPU data nothing drew since the previous render() (removed models).
    updateStreaming(); // before releaseUnused(): uses last frame's distances
    releaseUnused();
    texturesUploaded_ = 0;
    culled_ = 0;
    GLint target = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
    targetFbo_ = target;
    targetX_ = view.viewportX;
    targetY_ = view.viewportY;
    env_ = &scene.environment;
    time_ = view.time;
    view_ = view.view;
    projection_ = view.projection;
    ensureTargets(std::max(1, view.viewportWidth), std::max(1, view.viewportHeight));

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, jointBuffer_);
    updateEnvironment(scene, view);
    updateEnvironmentMap(scene);
    renderShadows(scene, view);

    // Depth pre-pass: SSAO needs depth, and the main pass then shades each pixel once.
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo_);
    glViewport(0, 0, width_, height_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClear(GL_DEPTH_BUFFER_BIT);
    const glm::mat4 viewProj = view.projection * view.view;
    useMeshProgram(programs_.depth);
    glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    drawGeometry(scene, view, true, viewProj);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    if (quality_.ssao)
        renderSsao();

    // Main pass.
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo_);
    glViewport(0, 0, width_, height_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    glClearBufferfv(GL_COLOR, 0, clear);
    useMeshProgram(programs_.mesh);
    glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniformMatrix3fv(loc_.envRotation, 1, GL_FALSE, glm::value_ptr(glm::mat3(1.0f)));
    glBindTextureUnit(6, shadowTex_ ? shadowTex_ : shadowDummy_);
    glBindTextureUnit(7, quality_.ssao ? ssaoBlurTex_ : whiteTex_);
    glBindTextureUnit(8, envMap_);
    glBindTextureUnit(9, brdfLut_);
    glUniform1i(loc_.shading, static_cast<int>(view.shading));
    const bool wireframe = view.shading == Shading::Wireframe;
    wireframe_ = wireframe;
    if (wireframe) {
        // Every edge, hidden ones included, on a dark background.
        const float dark[4] = {0.012f, 0.013f, 0.016f, 1.0f};
        glClearBufferfv(GL_COLOR, 0, dark);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
    }
    transparent_.clear();
    collectTransparent_ = !wireframe; // wireframe draws glass edges like everything else
    drawGeometry(scene, view, false, viewProj);
    collectTransparent_ = false;
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glUniform1i(loc_.shading, 0);

    // Sky where nothing was drawn.
    glDepthMask(GL_FALSE);
    if (!wireframe) {
        glUseProgram(programs_.sky.id);
        glBindVertexArray(emptyVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    if (!transparent_.empty()) {
        useMeshProgram(programs_.mesh);
        glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniformMatrix3fv(loc_.envRotation, 1, GL_FALSE, glm::value_ptr(glm::mat3(1.0f)));
        glUniform1i(loc_.shading, static_cast<int>(view.shading));
        drawTransparent(view);
        glUniform1i(loc_.shading, 0);
    }

    if (view.grid) {
        // Editor grid, blended over the scene and hidden by what stands above it.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        if (wireframe)
            glDisable(GL_DEPTH_TEST);
        const Program& grid = programs_.grid;
        glUseProgram(grid.id);
        glUniform1i(grid.loc("uGridAxis"), std::clamp(view.gridAxis, 0, 2));
        glUniform1f(grid.loc("uGridFade"), view.gridFade);
        glUniformMatrix4fv(grid.loc("uGridViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
        glBindVertexArray(emptyVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_BLEND);
    }

    if (!view.gizmoBoxes.empty()) {
        glClear(GL_DEPTH_BUFFER_BIT); // handles stay visible through walls
        useMeshProgram(programs_.mesh);
        for (const OverlayBox& b : view.gizmoBoxes)
            drawCube(glm::scale(glm::translate(glm::mat4(1.0f), b.center), b.size), b.color, 0.0f, b.emissive);
    }
    glBindVertexArray(0);
    pendingPresent_ = true;
    if (!view.deferPresent)
        present();
}

void Renderer::drawTransparent(const RenderView& view)
{
    // Back to front, blended over the scene; they test depth but do not write it.
    std::sort(transparent_.begin(), transparent_.end(),
              [](const TransparentDraw& a, const TransparentDraw& b) { return a.distance > b.distance; });
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glUniform1i(loc_.skinned, 0);
    glUniform1i(loc_.emissive, 0);
    glUniform1i(loc_.overlay, 0);
    glUniform1f(loc_.flash, 0.0f);
    for (const TransparentDraw& d : transparent_) {
        drawDistance_ = d.distance;
        glUniformMatrix4fv(loc_.model, 1, GL_FALSE, glm::value_ptr(d.transform));
        glUniform1f(loc_.highlight, d.highlight);
        const SurfaceAlpha alpha = surfaceAlpha(d.material, d.part);
        // Two-sided glass: back faces first, then front faces, so both blend in order.
        const int passes = alpha.doubleSided ? 2 : 1;
        for (int pass = 0; pass < passes; ++pass) {
            if (passes == 2)
                glCullFace(pass == 0 ? GL_FRONT : GL_BACK);
            if (d.box) {
                applySurface(d.material, nullptr, d.box->color, true);
                applyUv(*d.box);
                glBindVertexArray(vao_);
                glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
            } else {
                const GpuMesh& mesh = upload(*d.part);
                applySurface(d.material, d.part, d.part->color, false);
                glBindVertexArray(mesh.vao);
                glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
            }
        }
        glCullFace(GL_BACK);
    }
    applySurface(nullptr, nullptr, glm::vec3(1.0f), false);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    (void)view;
}

void Renderer::renderOverlay(const RenderView& view, const OverlayPass& pass)
{
    // First-person layer: own depth range and projection, so the weapon never clips into walls.
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo_);
    glViewport(0, 0, width_, height_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glClear(GL_DEPTH_BUFFER_BIT);
    useMeshProgram(programs_.mesh);
    glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(pass.projection));
    glUniform1i(loc_.overlay, 1);
    const glm::mat3 viewToWorld = glm::transpose(glm::mat3(view.view));
    glUniformMatrix3fv(loc_.envRotation, 1, GL_FALSE, glm::value_ptr(viewToWorld));
    glUniform3fv(loc_.overlaySun, 1, glm::value_ptr(glm::normalize(pass.sunDirectionViewSpace)));
    glUniform1f(loc_.flash, 0.0f);
    glUniform1i(loc_.skinned, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, jointBuffer_);
    glBindTextureUnit(6, shadowDummy_);
    glBindTextureUnit(7, whiteTex_);
    glBindTextureUnit(8, envMap_);
    glBindTextureUnit(9, brdfLut_);
    if (pass.model)
        drawModel(*pass.model, pass.modelTransform, 0.0f, pass.animator);
    for (const OverlayBox& b : pass.boxes)
        drawCube(glm::scale(glm::translate(glm::mat4(1.0f), b.center), b.size), b.color, 0.0f, b.emissive);
    glUniform1i(loc_.overlay, 0);
    glBindVertexArray(0);
}

void Renderer::renderBloom()
{
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(emptyVao_);
    glUseProgram(programs_.bloomDown.id);
    glUniform1f(programs_.bloomDown.loc("uThreshold"), env_ ? env_->bloomThreshold : 1.2f);
    for (std::size_t i = 0; i < bloomTex_.size(); ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
        glViewport(0, 0, bloomSize_[i].x, bloomSize_[i].y);
        glUniform1i(programs_.bloomDown.loc("uFirst"), i == 0 ? 1 : 0);
        glBindTextureUnit(0, i == 0 ? hdrColor_ : bloomTex_[i - 1]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glUseProgram(programs_.bloomUp.id);
    glUniform1f(programs_.bloomUp.loc("uRadius"), 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (std::size_t i = bloomTex_.size() - 1; i > 0; --i) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i - 1]);
        glViewport(0, 0, bloomSize_[i - 1].x, bloomSize_[i - 1].y);
        glBindTextureUnit(0, bloomTex_[i]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);
}

void Renderer::present()
{
    if (!pendingPresent_)
        return;
    pendingPresent_ = false;
    resetGlState();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    if (quality_.bloom)
        renderBloom();
    const Environment defaults;
    const Environment& e = env_ ? *env_ : defaults;
    glBindVertexArray(emptyVao_);
    const bool fxaa = quality_.fxaa && !wireframe_; // FXAA breaks one-pixel lines into dots
    glBindFramebuffer(GL_FRAMEBUFFER, fxaa ? ldrFbo_ : static_cast<unsigned>(targetFbo_));
    if (fxaa)
        glViewport(0, 0, width_, height_);
    else
        glViewport(targetX_, targetY_, width_, height_);
    const Program& post = programs_.post;
    glUseProgram(post.id);
    glUniform1f(post.loc("uExposure"), e.exposure);
    glUniform1f(post.loc("uBloomIntensity"), quality_.bloom ? e.bloomIntensity : 0.0f);
    glUniform1f(post.loc("uSaturation"), e.saturation);
    glUniform1f(post.loc("uContrast"), e.contrast);
    glUniform3fv(post.loc("uTint"), 1, glm::value_ptr(e.tint));
    glUniform1f(post.loc("uVignette"), e.vignette);
    glUniform1f(post.loc("uGrain"), e.grain);
    glUniform1f(post.loc("uTime"), time_);
    glUniform1i(post.loc("uLumaAlpha"), fxaa ? 1 : 0);
    glBindTextureUnit(0, hdrColor_);
    glBindTextureUnit(1, bloomTex_.empty() ? whiteTex_ : bloomTex_[0]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (fxaa) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(targetFbo_));
        glViewport(targetX_, targetY_, width_, height_);
        glUseProgram(programs_.fxaa.id);
        glBindTextureUnit(0, ldrTex_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    // Leave depth of the scene in the target too, for tools that draw on top.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(targetFbo_));
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
}

const Renderer::GpuMesh& Renderer::upload(const MeshPart& part)
{
    auto it = meshes_.find(part.id);
    if (it != meshes_.end()) {
        it->second.used = true;
        return it->second;
    }
    std::vector<Vertex> vertices(part.positions.size());
    const bool hasUv = part.texcoords.size() == part.positions.size();
    const bool skinned = part.joints.size() == part.positions.size() && part.weights.size() == part.positions.size();
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        vertices[i] = {part.positions[i], part.normals[i], hasUv ? part.texcoords[i] : glm::vec2(0.0f)};
        if (skinned) {
            vertices[i].joints = part.joints[i];
            vertices[i].weights = part.weights[i];
        }
    }

    GpuMesh m;
    glCreateBuffers(1, &m.vbo);
    glNamedBufferStorage(m.vbo, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), 0);
    glCreateBuffers(1, &m.ibo);
    glNamedBufferStorage(m.ibo, static_cast<GLsizeiptr>(part.indices.size() * sizeof(std::uint32_t)), part.indices.data(), 0);
    glCreateVertexArrays(1, &m.vao);
    setupVertexArray(m.vao, m.vbo);
    glVertexArrayElementBuffer(m.vao, m.ibo);
    m.indexCount = static_cast<int>(part.indices.size());
    m.bytes = vertices.size() * sizeof(Vertex) + part.indices.size() * sizeof(std::uint32_t);
    m.used = true;
    return meshes_.emplace(part.id, m).first->second;
}

namespace {
GLenum internalFormat(TextureFormat f, bool srgb)
{
    switch (f) {
    case TextureFormat::BC1: return srgb ? kSrgbDxt1 : GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    case TextureFormat::BC3: return srgb ? kSrgbAlphaDxt5 : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    case TextureFormat::BC5: return GL_COMPRESSED_RG_RGTC2;
    default: return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    }
}

// First level of a chain that is at most `size` pixels on its longest side.
int mipForSize(int width, int height, int mipCount, int size)
{
    if (size <= 0)
        return 0;
    int mip = 0;
    while (mip + 1 < mipCount && std::max(width >> mip, height >> mip) > size)
        ++mip;
    return mip;
}

constexpr int kStreamLowSize = 256; // far-away textures keep at most this many pixels
constexpr double kStreamOutDelay = 5.0; // seconds far away before the big mips are freed

double secondsNow()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

unsigned Renderer::createTexture(GpuTexture& g, const CookedTexture& c, int skip)
{
    skip = std::clamp(skip, 0, std::max(0, static_cast<int>(c.mips.size()) - 1));
    const int levels = static_cast<int>(c.mips.size()) - skip;
    if (levels <= 0)
        return 0;
    if (c.format != TextureFormat::RGBA8 && (!s3tc_ || (g.srgb && c.format != TextureFormat::BC5 && !s3tcSrgb_)))
        return 0; // GPU without S3TC: textureCookConfig().compress is off, so this is a stale cache
    unsigned handle = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &handle);
    const GLenum fmt = internalFormat(c.format, g.srgb && c.format != TextureFormat::BC5);
    const TextureMip& top = c.mips[static_cast<std::size_t>(skip)];
    glTextureStorage2D(handle, levels, fmt, top.width, top.height);
    std::size_t bytes = 0;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < levels; ++i) {
        const TextureMip& m = c.mips[static_cast<std::size_t>(skip + i)];
        if (c.format == TextureFormat::RGBA8)
            glTextureSubImage2D(handle, i, 0, 0, m.width, m.height, GL_RGBA, GL_UNSIGNED_BYTE, c.data.data() + m.offset);
        else
            glCompressedTextureSubImage2D(handle, i, 0, 0, m.width, m.height, fmt, static_cast<GLsizei>(m.size),
                                          c.data.data() + m.offset);
        bytes += m.size;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    const bool mips = g.mipmaps && levels > 1;
    const GLint minFilter = g.nearest ? (mips ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST) : (mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTextureParameteri(handle, GL_TEXTURE_MIN_FILTER, minFilter);
    glTextureParameteri(handle, GL_TEXTURE_MAG_FILTER, g.nearest ? GL_NEAREST : GL_LINEAR);
    const GLint wrap = g.wrap == 1 ? GL_CLAMP_TO_EDGE : (g.wrap == 2 ? GL_MIRRORED_REPEAT : GL_REPEAT);
    glTextureParameteri(handle, GL_TEXTURE_WRAP_S, wrap);
    glTextureParameteri(handle, GL_TEXTURE_WRAP_T, wrap);
    if (GLAD_GL_VERSION_4_6 || GLAD_GL_EXT_texture_filter_anisotropic)
        glTextureParameterf(handle, GL_TEXTURE_MAX_ANISOTROPY, 8.0f); // sharp floors at grazing angles
    g.bytes = bytes;
    return handle;
}

unsigned Renderer::texture(const Texture& tex)
{
    auto it = textures_.find(tex.id);
    if (it != textures_.end()) {
        it->second.used = true;
        it->second.nearest_m = std::min(it->second.nearest_m, drawDistance_);
        return it->second.handle;
    }
    // Uploading many 2K textures at once would stall a frame: spread them out
    // (surfaces draw without the map until it is on the GPU).
    if (!ignoreUploadBudget_ && quality_.textureUploadsPerFrame > 0 && ++texturesUploaded_ > quality_.textureUploadsPerFrame)
        return 0;
    GpuTexture g;
    g.srgb = tex.srgb;
    g.nearest = tex.nearest;
    g.mipmaps = tex.mipmaps;
    g.wrap = tex.wrap;
    g.format = tex.format;
    g.file = tex.cookedFile;
    g.used = true;
    g.nearest_m = drawDistance_;
    g.fullBytes = static_cast<std::size_t>(tex.width) * static_cast<std::size_t>(tex.height) * 4 * 4 / 3;
    const bool cooked = !tex.cooked.mips.empty() || !tex.cookedFile.empty();
    if (cooked) {
        const int count = tex.cooked.mips.empty() ? tex.mipCount : static_cast<int>(tex.cooked.mips.size());
        g.width = tex.width;
        g.height = tex.height;
        g.mipCount = count;
        g.maxMip = mipForSize(tex.width, tex.height, count, quality_.textureMaxSize);
        const bool streams = !g.file.empty() && quality_.textureStreamDistance > 0.0f && count > 1;
        g.lowMip = streams ? std::max(g.maxMip, mipForSize(tex.width, tex.height, count, kStreamLowSize)) : g.maxMip;
        const int want = drawDistance_ > quality_.textureStreamDistance ? g.lowMip : g.maxMip;
        if (!tex.cooked.data.empty()) {
            g.handle = createTexture(g, tex.cooked, want);
            g.firstMip = want;
        } else {
            // Pixels already released: read them back from the cook cache on a worker.
            const std::filesystem::path file = g.file;
            g.pending = std::make_shared<std::future<CookedTexture>>(JobSystem::global().submit([file, want] {
                CookedTexture c;
                readCookedTexture(file, c, want);
                return c;
            }));
            g.pendingMip = want;
            g.firstMip = want;
        }
        if (want != g.maxMip)
            g.farSince = secondsNow();
    } else if (!tex.rgba.empty()) {
        glCreateTextures(GL_TEXTURE_2D, 1, &g.handle);
        const int levels = tex.mipmaps ? 1 + static_cast<int>(std::floor(std::log2(std::max(tex.width, tex.height)))) : 1;
        glTextureStorage2D(g.handle, levels, tex.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, tex.width, tex.height);
        glTextureSubImage2D(g.handle, 0, 0, 0, tex.width, tex.height, GL_RGBA, GL_UNSIGNED_BYTE, tex.rgba.data());
        if (tex.mipmaps)
            glGenerateTextureMipmap(g.handle);
        const GLint minFilter = tex.nearest ? (tex.mipmaps ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST)
                                            : (tex.mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTextureParameteri(g.handle, GL_TEXTURE_MIN_FILTER, minFilter);
        glTextureParameteri(g.handle, GL_TEXTURE_MAG_FILTER, tex.nearest ? GL_NEAREST : GL_LINEAR);
        const GLint wrap = tex.wrap == 1 ? GL_CLAMP_TO_EDGE : (tex.wrap == 2 ? GL_MIRRORED_REPEAT : GL_REPEAT);
        glTextureParameteri(g.handle, GL_TEXTURE_WRAP_S, wrap);
        glTextureParameteri(g.handle, GL_TEXTURE_WRAP_T, wrap);
        if (GLAD_GL_VERSION_4_6 || GLAD_GL_EXT_texture_filter_anisotropic)
            glTextureParameterf(g.handle, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
        g.bytes = tex.gpuBytes();
    } else {
        return 0;
    }
    // The GPU has it and the cook cache can give it back: free the CPU copy.
    if (quality_.releaseCpuTextures && !tex.cookedFile.empty() && g.handle) {
        std::vector<std::uint8_t>().swap(tex.rgba);
        tex.cooked = CookedTexture{};
    }
    const unsigned handle = g.handle;
    textures_.emplace(tex.id, std::move(g));
    return handle;
}

void Renderer::updateStreaming()
{
    const double now = secondsNow();
    int uploads = 0;
    for (auto& [id, g] : textures_) {
        // Finished reads: swap in the new texture.
        if (g.pending && g.pending->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            if (quality_.textureUploadsPerFrame > 0 && uploads >= quality_.textureUploadsPerFrame)
                continue;
            const CookedTexture c = g.pending->get();
            g.pending.reset();
            if (!c.mips.empty()) {
                if (const unsigned handle = createTexture(g, c, 0)) {
                    if (g.handle)
                        glDeleteTextures(1, &g.handle);
                    g.handle = handle;
                    g.firstMip = g.pendingMip;
                    ++uploads;
                }
            }
        }
        const float distance = g.nearest_m;
        g.nearest_m = 1e30f; // gathered again by this frame's draws
        if (g.file.empty() || g.pending || (g.lowMip == g.maxMip && g.firstMip == g.maxMip))
            continue;
        const bool near = distance <= quality_.textureStreamDistance;
        int want = g.firstMip;
        if (near) {
            g.farSince = -1.0;
            want = g.maxMip;
        } else {
            if (g.farSince < 0.0)
                g.farSince = now;
            if (now - g.farSince > kStreamOutDelay)
                want = g.lowMip;
        }
        if (want == g.firstMip)
            continue;
        const std::filesystem::path file = g.file;
        g.pending = std::make_shared<std::future<CookedTexture>>(JobSystem::global().submit([file, want] {
            CookedTexture c;
            readCookedTexture(file, c, want);
            return c;
        }));
        g.pendingMip = want;
    }
}

void Renderer::setQuality(const RenderQuality& quality)
{
    quality_ = quality;
    for (auto& [id, g] : textures_) {
        if (g.file.empty())
            continue; // plain uploads keep their size until they are uploaded again
        g.maxMip = mipForSize(g.width, g.height, g.mipCount, quality_.textureMaxSize);
        const bool streams = quality_.textureStreamDistance > 0.0f && g.mipCount > 1;
        g.lowMip = streams ? std::max(g.maxMip, mipForSize(g.width, g.height, g.mipCount, kStreamLowSize)) : g.maxMip;
        g.farSince = -1.0;
        if (!g.pending && g.firstMip != g.maxMip) { // updateStreaming() then picks low or full by distance
            const std::filesystem::path file = g.file;
            const int want = g.maxMip;
            g.pending = std::make_shared<std::future<CookedTexture>>(JobSystem::global().submit([file, want] {
                CookedTexture c;
                readCookedTexture(file, c, want);
                return c;
            }));
            g.pendingMip = want;
        }
    }
}

Renderer::MemoryStats Renderer::memoryStats() const
{
    MemoryStats m;
    for (const auto& [id, g] : textures_) {
        m.textureBytes += g.bytes;
        m.savedBytes += g.fullBytes > g.bytes ? g.fullBytes - g.bytes : 0;
        ++m.textures;
        if (!g.file.empty() && g.firstMip > g.maxMip)
            ++m.streamedLow;
        if (g.pending)
            ++m.streaming;
    }
    for (const auto& [id, mesh] : meshes_) {
        m.meshBytes += mesh.bytes;
        ++m.meshes;
    }
    const std::size_t pixels = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    // HDR color + depth + SSAO (two) + LDR + bloom chain (about a third of HDR).
    m.targetBytes = pixels * (8 + 4 + 2 + 4 + 3) + static_cast<std::size_t>(shadowRes_) * shadowRes_ * 4 * 3;
    return m;
}

void Renderer::drawModel(const Model& model, const glm::mat4& transform, float highlight, const Animator* animator)
{
    glUniform1f(loc_.highlight, highlight);
    const Skeleton* skeleton = model.skeleton.get();
    if (skeleton)
        evaluatePose(*skeleton, animator, pose_);
    int uploadedSkin = -1;
    for (const MeshPart& part : model.parts) {
        const GpuMesh& mesh = upload(part);
        glm::mat4 partTransform = transform;
        const bool skinned = skeleton && part.skin >= 0;
        if (skinned) {
            if (part.skin != uploadedSkin) {
                skinMatrices(*skeleton, part.skin, pose_, joints_);
                const std::size_t count = std::min<std::size_t>(joints_.size(), kMaxJoints);
                glNamedBufferSubData(jointBuffer_, 0, static_cast<GLsizeiptr>(count * sizeof(glm::mat4)), joints_.data());
                uploadedSkin = part.skin;
            }
        } else if (skeleton && part.node >= 0) {
            partTransform = transform * pose_[static_cast<std::size_t>(part.node)];
        }
        const Material* material = model.materialFor(part);
        const SurfaceAlpha alpha = surfaceAlpha(material, &part);
        if (alpha.mode == AlphaMode::Blend && !skinned) {
            if (depthOnly_)
                continue; // glass neither occludes nor casts shadows
            if (collectTransparent_) {
                transparent_.push_back({&part, material, nullptr, partTransform, drawDistance_, highlight});
                continue;
            }
        }
        glUniform1i(loc_.skinned, skinned ? 1 : 0);
        glUniformMatrix4fv(loc_.model, 1, GL_FALSE, glm::value_ptr(partTransform));
        applySurface(material, &part, part.color, false);
        glBindVertexArray(mesh.vao);
        const bool twoSided = alpha.doubleSided && glIsEnabled(GL_CULL_FACE);
        if (twoSided)
            glDisable(GL_CULL_FACE);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        if (twoSided)
            glEnable(GL_CULL_FACE);
    }
    applySurface(nullptr, nullptr, glm::vec3(1.0f), false);
    glUniform1i(loc_.skinned, 0);
}

void Renderer::applyUv(const Box& box)
{
    if (depthOnly_ || !box.uvModified())
        return;
    glUniform2fv(loc_.uvScale, 1, glm::value_ptr(box.uvScale));
    glUniform2fv(loc_.uvOffset, 1, glm::value_ptr(box.uvOffset));
    glUniform1f(loc_.uvRotation, glm::radians(box.uvRotation));
}

void Renderer::applySurface(const Material* material, const MeshPart* part, glm::vec3 color, bool worldUv)
{
    const SurfaceAlpha alpha = surfaceAlpha(material, part);
    glUniform1i(loc_.alphaMode, static_cast<int>(alpha.mode));
    glUniform1f(loc_.alphaCutoff, alpha.cutoff);
    glUniform1f(loc_.opacity, alpha.opacity);
    if (depthOnly_) {
        // Depth and shadows only need the albedo alpha of masked surfaces.
        if (alpha.mode == AlphaMode::Mask) {
            const Texture* albedo = nullptr;
            if (material && material->maps[0])
                albedo = material->maps[0]->texture.get();
            else if (!material && part)
                albedo = part->texture.get();
            const unsigned handle = albedo && albedo->valid() ? texture(*albedo) : 0u;
            if (handle)
                glBindTextureUnit(0, handle);
            glUniform1i(loc_.hasMap[0], handle ? 1 : 0);
            const glm::vec2 tiling = material ? material->tiling : glm::vec2(1.0f);
            glUniform2fv(loc_.tiling, 1, glm::value_ptr(tiling));
        }
        return;
    }
    // Texture units: 0 albedo, 1 normal, 2 roughness, 3 metallic, 4 occlusion, 5 emissive.
    const Texture* maps[6] = {};
    glm::vec2 tiling(1.0f);
    float roughness = 0.8f, metallic = 0.0f, normalStrength = 1.0f;
    glm::vec3 emissive(0.0f);
    int roughnessChannel = 0, metallicChannel = 0;
    bool grid = true;
    if (material) {
        color = material->color;
        for (int i = 0; i < kMapCount; ++i)
            if (const auto& asset = material->maps[static_cast<std::size_t>(i)])
                maps[i] = asset->texture.get();
        tiling = material->tiling;
        roughness = material->roughness;
        metallic = material->metallic;
        normalStrength = material->normalStrength;
        emissive = material->emissive;
        worldUv = worldUv || material->worldUv;
        grid = false;
    } else if (part) {
        maps[0] = part->texture.get();
        maps[1] = part->normalTexture.get();
        maps[2] = maps[3] = part->metalRoughTexture.get();
        maps[4] = part->occlusionTexture.get();
        maps[5] = part->emissiveTexture.get();
        roughnessChannel = 1; // glTF packs roughness in G and metallic in B
        metallicChannel = 2;
        roughness = part->roughness;
        metallic = part->metallic;
        normalStrength = part->normalScale;
        emissive = part->emissive;
        grid = maps[0] == nullptr;
    }
    glUniform3fv(loc_.color, 1, glm::value_ptr(color));
    glUniform2fv(loc_.tiling, 1, glm::value_ptr(tiling));
    glUniform1f(loc_.roughness, roughness);
    glUniform1f(loc_.metallic, metallic);
    glUniform1f(loc_.normalStrength, normalStrength);
    glUniform1i(loc_.roughnessChannel, roughnessChannel);
    glUniform1i(loc_.metallicChannel, metallicChannel);
    glUniform3fv(loc_.emissiveColor, 1, glm::value_ptr(emissive));
    glUniform1i(loc_.worldUv, worldUv ? 1 : 0);
    glUniform1i(loc_.grid, grid ? 1 : 0);
    glUniform2f(loc_.uvScale, 1.0f, 1.0f);
    glUniform2f(loc_.uvOffset, 0.0f, 0.0f);
    glUniform1f(loc_.uvRotation, 0.0f);
    for (int i = 0; i < 6; ++i) {
        const unsigned handle = maps[i] && maps[i]->valid() ? texture(*maps[i]) : 0u;
        const bool has = handle != 0;
        if (has)
            glBindTextureUnit(static_cast<unsigned>(i), handle);
        glUniform1i(loc_.hasMap[i], has ? 1 : 0);
    }
}

void Renderer::drawCube(const glm::mat4& transform, glm::vec3 color, float highlight, bool emissive)
{
    glBindVertexArray(vao_);
    glUniformMatrix4fv(loc_.model, 1, GL_FALSE, glm::value_ptr(transform));
    applySurface(nullptr, nullptr, color, false);
    glUniform1f(loc_.highlight, highlight);
    glUniform1i(loc_.emissive, emissive ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    glUniform1i(loc_.emissive, 0);
}

void Renderer::keepTextureAlive(const Texture* tex)
{
    if (!tex)
        return;
    if (auto t = textures_.find(tex->id); t != textures_.end()) {
        t->second.used = true;
        t->second.nearest_m = std::min(t->second.nearest_m, drawDistance_); // streaming goes by distance, not visibility
    }
}

void Renderer::keepAlive(const Model& model)
{
    // Culled models keep their GPU meshes and textures (no re-upload when they come back into view).
    for (const MeshPart& part : model.parts) {
        if (auto it = meshes_.find(part.id); it != meshes_.end())
            it->second.used = true;
        if (const Material* m = model.materialFor(part))
            for (const auto& asset : m->maps)
                if (asset)
                    keepTextureAlive(asset->texture.get());
        // Every glTF map, not only the base color: a culled model must not lose its
        // normal/roughness maps and upload them again when it comes back into view.
        for (const Texture* tex : {part.texture.get(), part.normalTexture.get(), part.metalRoughTexture.get(),
                                   part.occlusionTexture.get(), part.emissiveTexture.get()})
            keepTextureAlive(tex);
    }
}

unsigned Renderer::renderTopDown(const Scene& scene, glm::vec2 minXZ, float size, int pixels)
{
    unsigned tex = 0, fbo = 0, depth = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    const int levels = 1 + static_cast<int>(std::floor(std::log2(pixels)));
    glTextureStorage2D(tex, levels, GL_RGBA8, pixels, pixels);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {0.05f, 0.05f, 0.045f, 1.0f};
    glTextureParameterfv(tex, GL_TEXTURE_BORDER_COLOR, border);
    glCreateRenderbuffers(1, &depth);
    glNamedRenderbufferStorage(depth, GL_DEPTH24_STENCIL8, pixels, pixels);
    glCreateFramebuffers(1, &fbo);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, tex, 0);
    glNamedFramebufferRenderbuffer(fbo, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
    GLint previous = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    const glm::vec3 center(minXZ.x + size * 0.5f, 0.0f, minXZ.y + size * 0.5f);
    RenderView view;
    view.viewportWidth = view.viewportHeight = pixels;
    view.cameraPosition = center + glm::vec3(0.0f, 1500.0f, 0.0f);
    view.view = glm::lookAt(view.cameraPosition, center, glm::vec3(0.0f, 0.0f, -1.0f));
    const float h = size * 0.5f;
    view.projection = glm::ortho(-h, h, -h, h, 1.0f, 3000.0f);
    view.fog = false;
    const RenderQuality saved = quality_;
    quality_.ssao = true;
    quality_.fxaa = true;
    render(scene, view);
    quality_ = saved;
    glGenerateTextureMipmap(tex);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(previous));
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &depth);
    return tex;
}

void Renderer::deleteTexture(unsigned texture)
{
    if (texture)
        glDeleteTextures(1, &texture);
}

float Renderer::preload(const Scene& scene, std::size_t& cursor, double budgetMs, glm::vec3 viewer)
{
    // Distance from the viewer decides small (streamed) or full textures.
    std::vector<std::pair<const Model*, float>> models;
    for (const Model& m : scene.models) {
        const float d = distanceTo(viewer, m.bounds());
        models.emplace_back(&m, d);
        if (m.lod)
            models.emplace_back(m.lod.get(), d);
    }
    for (const Entity& e : scene.entities)
        if (e.model)
            models.emplace_back(e.model.get(), glm::distance(viewer, e.position));
    std::vector<std::pair<const Material*, float>> materials;
    for (const Box& b : scene.boxes)
        if (b.material)
            materials.emplace_back(b.material.get(), distanceTo(viewer, b.bounds()));
    const std::size_t total = models.size() + materials.size();
    const auto start = std::chrono::steady_clock::now();
    ignoreUploadBudget_ = true;
    auto upMaterial = [this](const Material* material) {
        for (const auto& asset : material->maps)
            if (asset && asset->texture && asset->texture->valid())
                texture(*asset->texture);
    };
    while (cursor < total) {
        if (cursor < models.size()) {
            const Model& model = *models[cursor].first;
            drawDistance_ = models[cursor].second;
            for (const MeshPart& part : model.parts) {
                upload(part);
                if (const Material* m = model.materialFor(part))
                    upMaterial(m);
                for (const Texture* t : {part.texture.get(), part.normalTexture.get(), part.metalRoughTexture.get(),
                                         part.occlusionTexture.get(), part.emissiveTexture.get()})
                    if (t && t->valid())
                        texture(*t);
            }
        } else {
            drawDistance_ = materials[cursor - models.size()].second;
            upMaterial(materials[cursor - models.size()].first);
        }
        ++cursor;
        if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() > budgetMs)
            break;
    }
    // Textures read from the cook cache on workers: wait for them on the loading screen.
    int waiting = 0;
    for (auto& [id, g] : textures_) {
        if (!g.pending)
            continue;
        if (g.pending->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++waiting;
            continue;
        }
        const CookedTexture c = g.pending->get();
        g.pending.reset();
        if (const unsigned handle = c.mips.empty() ? 0u : createTexture(g, c, 0)) {
            if (g.handle)
                glDeleteTextures(1, &g.handle);
            g.handle = handle;
            g.firstMip = g.pendingMip;
        }
    }
    ignoreUploadBudget_ = false;
    if (cursor >= total)
        return waiting ? 0.99f : 1.0f;
    return total ? static_cast<float>(cursor) / static_cast<float>(total) : 1.0f;
}

void Renderer::releaseUnused()
{
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (!it->second.used) {
            glDeleteTextures(1, &it->second.handle);
            it = textures_.erase(it);
        } else {
            it->second.used = false;
            ++it;
        }
    }
    // Meshes of models removed from the scene are freed after one frame.
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (!it->second.used) {
            glDeleteVertexArrays(1, &it->second.vao);
            glDeleteBuffers(1, &it->second.vbo);
            glDeleteBuffers(1, &it->second.ibo);
            it = meshes_.erase(it);
        } else {
            it->second.used = false;
            ++it;
        }
    }
}

} // namespace engine
