#include "engine/Renderer.h"

#include <glad/gl.h>
#include "engine/JobSystem.h"
#include "engine/Log.h"
#include "engine/Paths.h"
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <stb_image.h>
#include <glm/gtc/packing.hpp>
#include <glm/packing.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <unordered_map>
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
    glm::vec4 moonDir;    // xyz toward the moon, w: moon brightness 0..1
    glm::vec4 skyParams;  // x: stars, y: twilight glow, z: cloud shadows, w: 1 = time of day on
    glm::vec4 sunPos;     // xyz toward the sun (the main light may be the moon)
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

// ---- GL debugging: driver messages to the log, object labels, program binary cache ----

void GLAD_API_PTR glDebugToLog(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* message, const void*)
{
    // The same message again and again (every frame) is logged a few times, then muted.
    static std::mutex mutex;
    static std::unordered_map<GLuint, int> seen;
    {
        std::lock_guard lock(mutex);
        const int n = ++seen[id];
        if (n > 5)
            return;
        if (n == 5) {
            LOG_WARN("gl", "message %u repeats, muted from now on", id);
            return;
        }
    }
    LogLevel level = LogLevel::Debug;
    if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH)
        level = LogLevel::Error;
    else if (severity == GL_DEBUG_SEVERITY_MEDIUM || type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR)
        level = LogLevel::Warning;
    else if (type == GL_DEBUG_TYPE_PERFORMANCE || type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR)
        level = LogLevel::Info;
    const char* kind = type == GL_DEBUG_TYPE_ERROR ? "error"
                     : type == GL_DEBUG_TYPE_PERFORMANCE ? "performance"
                     : type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR ? "undefined behaviour"
                     : type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR ? "deprecated"
                     : type == GL_DEBUG_TYPE_PORTABILITY ? "portability" : "other";
    const char* from = source == GL_DEBUG_SOURCE_SHADER_COMPILER ? "shader compiler"
                     : source == GL_DEBUG_SOURCE_WINDOW_SYSTEM ? "window system"
                     : source == GL_DEBUG_SOURCE_APPLICATION ? "application" : "driver";
    logf(level, "gl", "%s (%s, id %u): %s", kind, from, id, message);
}

// Names GL objects for RenderDoc / Nsight / apitrace captures.
void label(GLenum kind, unsigned object, const std::string& name)
{
    if (object)
        glObjectLabel(kind, object, static_cast<GLsizei>(name.size()), name.c_str());
}

bool g_programCacheEnabled = true;

std::filesystem::path programCacheFile(const std::string& vert, const std::string& frag)
{
    // Key: final sources plus the driver identity (a new driver gets fresh binaries).
    std::uint64_t h = 1469598103934665603ull; // FNV-1a, stable across runs and standard libraries
    auto mix = [&h](const std::string& text) {
        for (unsigned char c : text)
            h = (h ^ c) * 1099511628211ull;
        h = (h ^ 0xff) * 1099511628211ull;
    };
    mix(vert);
    mix(frag);
    for (GLenum e : {GL_VENDOR, GL_RENDERER, GL_VERSION})
        if (const GLubyte* str = glGetString(e))
            mix(reinterpret_cast<const char*>(str));
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.bin", static_cast<unsigned long long>(h));
    return userDirectory("cache") / "shaders" / name;
}

unsigned loadCachedProgram(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return 0;
    std::uint32_t format = 0;
    if (!in.read(reinterpret_cast<char*>(&format), sizeof format))
        return 0;
    const std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.empty())
        return 0;
    const unsigned program = glCreateProgram();
    glProgramBinary(program, format, data.data(), static_cast<GLsizei>(data.size()));
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) { // a driver update can refuse old binaries: compile again
        glDeleteProgram(program);
        std::error_code ec;
        std::filesystem::remove(file, ec);
        return 0;
    }
    return program;
}

void storeCachedProgram(unsigned program, const std::filesystem::path& file)
{
    GLint length = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
    if (length <= 0)
        return;
    std::vector<char> data(static_cast<std::size_t>(length));
    GLenum format = 0;
    glGetProgramBinary(program, length, nullptr, &format, data.data());
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const std::filesystem::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary);
        const auto f = static_cast<std::uint32_t>(format);
        out.write(reinterpret_cast<const char*>(&f), sizeof f);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out)
            return;
    }
    std::filesystem::rename(temp, file, ec); // whole files only, even if two programs start at once
}

unsigned linkProgram(const std::map<std::string, std::string>& files, const std::string& vertName,
                     const std::string& fragName, std::string* error, bool* fromCache = nullptr)
{
    const auto vs = files.find(vertName), fs = files.find(fragName);
    if (vs == files.end() || fs == files.end()) {
        if (error)
            *error = "Missing shader " + (vs == files.end() ? vertName : fragName);
        return 0;
    }
    const std::string vertSource = withIncludes(vs->second, files), fragSource = withIncludes(fs->second, files);
    std::filesystem::path cacheFile;
    if (g_programCacheEnabled) {
        cacheFile = programCacheFile(vertSource, fragSource);
        if (const unsigned cached = loadCachedProgram(cacheFile)) {
            if (fromCache)
                *fromCache = true;
            label(GL_PROGRAM, cached, vertName + " + " + fragName);
            return cached;
        }
    }
    const unsigned v = compileStage(GL_VERTEX_SHADER, vertSource, vertName, error);
    const unsigned f = v ? compileStage(GL_FRAGMENT_SHADER, fragSource, fragName, error) : 0;
    if (!v || !f) {
        glDeleteShader(v);
        return 0;
    }
    const unsigned program = glCreateProgram();
    if (g_programCacheEnabled)
        glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
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
    label(GL_PROGRAM, program, vertName + " + " + fragName);
    if (g_programCacheEnabled)
        storeCachedProgram(program, cacheFile);
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
        q.lodScale = 0.5f; q.detailDensityFar = 0.1f; q.detailShadowDistance = 0.0f;
        q.localShadowResolution = 512; q.localShadowLayers = 6;
        break;
    case 1:
        q.shadowResolution = 1024; q.shadowCascades = 3; q.ssao = true; q.bloom = true;
        q.textureMaxSize = 2048; q.textureStreamDistance = 30.0f;
        q.lodScale = 0.75f; q.detailDensityFar = 0.15f; q.detailShadowDistance = 10.0f;
        q.localShadowResolution = 512;
        break;
    case 3:
        q.shadowResolution = 4096; q.shadowCascades = 3; q.textureStreamDistance = 80.0f;
        q.lodScale = 1.5f; q.detailDensityFar = 0.35f; q.detailShadowDistance = 25.0f;
        q.localShadowLayers = 18;
        break;
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
                             {&p.grid, "fullscreen.vert", "grid.frag"}, {&p.hiz, "fullscreen.vert", "hiz.frag"},
                             {&p.sprite, "sprite.vert", "sprite.frag"},
                             {&p.debug, "debug.vert", "debug.frag"},
                             {&p.luminance, "fullscreen.vert", "luminance.frag"},
                             {&p.exposure, "fullscreen.vert", "exposure.frag"}};
    const auto start = std::chrono::steady_clock::now();
    int cached = 0;
    for (const Entry& e : entries) {
        bool hit = false;
        e.program->id = linkProgram(src, e.vert, e.frag, error, &hit);
        cached += hit ? 1 : 0;
        e.program->locations.clear();
        if (!e.program->id) {
            destroyPrograms(p);
            return false;
        }
    }
    LOG_INFO("render", "%zu shader programs in %.0f ms (%d from the binary cache)", std::size(entries),
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), cached);
    return true;
}

void Renderer::destroyPrograms(Programs& p)
{
    for (Program* program : {&p.mesh, &p.depth, &p.sky, &p.envCapture, &p.prefilter, &p.brdf, &p.ssao, &p.blur, &p.bloomDown,
                             &p.bloomUp, &p.post, &p.fxaa, &p.copy, &p.grid, &p.hiz, &p.sprite, &p.debug, &p.luminance, &p.exposure}) {
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
    loc_.instanced = p.loc("uInstanced");
    loc_.instanceBase = p.loc("uInstanceBase");
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
    // Driver messages (errors, undefined behaviour, performance warnings) go to the log, category "gl".
    // With a debug context (FPS_ENGINE_GL_DEBUG=1) they arrive synchronously, so a breakpoint in
    // glDebugToLog stops at the offending call.
    {
        GLint flags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        glEnable(GL_DEBUG_OUTPUT);
        if (flags & GL_CONTEXT_FLAG_DEBUG_BIT)
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugToLog, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        if (flags & GL_CONTEXT_FLAG_DEBUG_BIT)
            LOG_INFO("render", "OpenGL debug context: driver messages are synchronous");
    }
    {
        GLint formats = 0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
        const char* off = std::getenv("FPS_ENGINE_NO_SHADER_CACHE");
        g_programCacheEnabled = formats > 0 && !(off && *off && *off != '0');
    }
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
    releaseOcclusion();
    for (auto& [id, m] : meshes_) {
        glDeleteVertexArrays(1, &m.vao);
        glDeleteBuffers(1, &m.vbo);
        glDeleteBuffers(1, &m.ibo);
    }
    meshes_.clear();
    if (terrain_.vao) {
        glDeleteVertexArrays(1, &terrain_.vao);
        glDeleteBuffers(1, &terrain_.vbo);
        glDeleteBuffers(1, &terrain_.ibo);
    }
    if (terrain_.splat)
        glDeleteTextures(1, &terrain_.splat);
    terrain_ = GpuTerrain{};
    for (auto& frame : queries_)
        for (unsigned& q : frame)
            if (q) {
                glDeleteQueries(1, &q);
                q = 0;
            }
    foliageCache_ = FoliageCache{};
    if (instanceBuffer_)
        glDeleteBuffers(1, &instanceBuffer_);
    instanceBuffer_ = 0;
    instanceCapacity_ = 0;
    for (auto& [id, t] : textures_)
        glDeleteTextures(1, &t.handle);
    textures_.clear();
    destroyTargets();
    destroyPrograms(programs_);
    for (unsigned* t : {&shadowTex_, &envRaw_, &envMap_, &brdfLut_, &whiteTex_, &shadowDummy_, &localShadowTex_, &lumTex_, &adaptTex_[0],
                        &adaptTex_[1], &lutTex_})
        if (*t) {
            glDeleteTextures(1, t);
            *t = 0;
        }
    for (unsigned* f : {&shadowFbo_, &captureFbo_, &localShadowFbo_, &lumFbo_, &adaptFbo_[0], &adaptFbo_[1]})
        if (*f) {
            glDeleteFramebuffers(1, f);
            *f = 0;
        }
    if (spriteBuffer_)
        glDeleteBuffers(1, &spriteBuffer_);
    spriteBuffer_ = 0;
    if (debugBuffer_)
        glDeleteBuffers(1, &debugBuffer_);
    debugBuffer_ = 0;
    debugCapacity_ = 0;
    spriteCapacity_ = 0;
    for (unsigned* b : {&vbo_, &jointBuffer_, &envBuffer_, &lightBuffer_, &lightGridBuffer_, &lightIndexBuffer_, &lightShadowBuffer_})
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
    localShadowRes_ = 0;
    lutSize_ = 0;
    lutPath_.clear();
    exposureReset_ = true;
    cascadeCount_ = 0; // cached cascades are gone with the texture
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
    label(GL_TEXTURE, hdrColor_, "HDR colour");
    label(GL_TEXTURE, depthTex_, "Scene depth");
    label(GL_FRAMEBUFFER, hdrFbo_, "HDR");
    label(GL_TEXTURE, ssaoTex_, "SSAO");
    label(GL_TEXTURE, ssaoBlurTex_, "SSAO blurred");
    label(GL_FRAMEBUFFER, ssaoFbo_, "SSAO");
    label(GL_FRAMEBUFFER, ssaoBlurFbo_, "SSAO blur");
    label(GL_TEXTURE, ldrTex_, "LDR colour");
    label(GL_FRAMEBUFFER, ldrFbo_, "LDR");
    glm::ivec2 size(width, height);
    for (int i = 0; i < kBloomLevels; ++i) {
        size = glm::max(size / 2, glm::ivec2(1));
        const unsigned t = makeTexture2D(GL_R11F_G11F_B10F, size.x, size.y, GL_LINEAR);
        bloomTex_.push_back(t);
        bloomFbo_.push_back(makeFbo(t));
        label(GL_TEXTURE, t, "Bloom " + std::to_string(i));
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
    (void)scene;
    const Environment& e = *env_;
    EnvironmentBlock b{};
    b.sunDir = glm::vec4(sky_.lightDirection, e.sunIntensity);
    b.moonDir = glm::vec4(sky_.moon, sky_.moonLight * std::max(0.0f, e.time.nightBrightness));
    b.skyParams = glm::vec4(sky_.stars, sky_.twilight, sky_.cloudShadows, e.time.enabled ? 1.0f : 0.0f);
    b.sunPos = glm::vec4(e.time.enabled ? sunPosition(e.time, e.time.hour) : -sky_.lightDirection, 0.0f);
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
    const glm::vec3 lightDir = sky_.lightDirection;
    const glm::mat4 invViewProj = b.invViewProj;
    float prev = nearPlane;
    // A new sun direction, resolution or cascade count invalidates every cached cascade.
    const bool sameSetup = cascadeCount_ == cascades && cascadeRes_ == quality_.shadowResolution &&
                           glm::dot(cascadeLight_, lightDir) > 0.99999f;
    cascadeCount_ = cascades;
    cascadeRes_ = quality_.shadowResolution;
    cascadeLight_ = lightDir;
    cascadeUpdate_ = 0;
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
        // Far cascades are cached: drawn with a margin and re-rendered every 2nd (cascade 1)
        // or 4th (cascade 2) frame, or as soon as the slice leaves the covered sphere.
        ShadowCascade& cache = cascades_[i];
        const bool cached = quality_.shadowCache && i > 0;
        const bool scheduled = !cached || (i == 1 ? (frameNumber_ % 2) == 1 : (frameNumber_ % 4) == 2);
        const bool inside = cache.valid && glm::distance(center, cache.center) + radius <= cache.radius;
        if (!sameSetup || scheduled || !inside) {
            cascadeUpdate_ |= 1u << i;
            cache.center = center;
            cache.radius = cached ? radius * 1.12f : radius;
        } else {
            b.shadowMatrices[i] = cache.matrix;
            prev = splits[i];
            continue;
        }
        radius = cache.radius;
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
        cache.matrix = b.shadowMatrices[i];
        cache.valid = true;
        if (i == 0) // world size of one texel of the first cascade drives the biases
            b.shadowParams.z = 1.0f / static_cast<float>(std::max(1, quality_.shadowResolution));
        b.shadowParams.y = 2.0f * radius / static_cast<float>(std::max(1, quality_.shadowResolution)) * (i == 0 ? 1.5f : b.shadowParams.y);
        prev = splits[i];
    }
    b.shadowParams.x = 0.0005f;
    b.shadowParams.y = 2.0f * (splits[0]) / static_cast<float>(std::max(1, quality_.shadowResolution)) * 1.5f;
    b.shadowParams.w = static_cast<float>(kEnvMips - 1);
    b.viewport = glm::vec4(width_, height_, 1.0f / static_cast<float>(width_), 1.0f / static_cast<float>(height_)); // render targets
    glNamedBufferSubData(envBuffer_, 0, sizeof(b), &b);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, envBuffer_);
}

void Renderer::updateEnvironmentMap(const Scene& scene)
{
    // Re-render the sky into the cubemap and prefilter it when the sky changed.
    (void)scene;
    const Environment& e = *env_;
    std::size_t h = 1;
    // A running day/night cycle changes the sky every frame: quantize so the cubemap
    // is captured again only when the change becomes visible (every few game minutes).
    const float q = e.time.enabled ? 48.0f : 0.0f;
    auto quant = [q](float v) { return q > 0.0f ? std::round(v * q) / q : v; };
    for (float v : {e.sunColor.r, e.sunColor.g, e.sunColor.b, e.sunIntensity, e.skyZenith.r, e.skyZenith.g, e.skyZenith.b,
                    e.skyHorizon.r, e.skyHorizon.g, e.skyHorizon.b, e.groundColor.r, e.groundColor.g, e.groundColor.b,
                    e.cloudCover, sky_.lightDirection.x, sky_.lightDirection.y, sky_.lightDirection.z, sky_.stars, sky_.twilight,
                    sky_.moon.x, sky_.moon.y, sky_.moon.z})
        hashCombine(h, quant(v));
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
    if (scene.terrain && scene.terrain->valid())
        drawTerrain(*scene.terrain, view);
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
    const bool mainPass = !depthOnly && shadowCascade_ < 0;
    const bool shadowPass = shadowCascade_ >= 0;
    const ModelGrid& grid = grid_;
    for (const ModelCell& cell : grid.cells) {
        // Whole cells first: beyond every cull distance inside, out of the frustum, or
        // (far shadow cascades) only small casters.
        const float cellDistance = distanceTo(view.cameraPosition, cell.bounds);
        const bool cellFar = cell.maxCull > 0.0f && cellDistance > cell.maxCull;
        const bool cellTiny = shadowMinCaster_ > 0.0f && cell.maxDiagonal < shadowMinCaster_;
        if (cellFar || cellTiny || !visible(cell.bounds) || occluded(cell.bounds)) {
            if (mainPass)
                for (std::uint32_t i : cell.models)
                    keepAliveLods(scene.models[i], nullptr); // stays uploaded while out of view
            culled_ += static_cast<int>(cell.models.size());
            continue;
        }
        for (std::uint32_t i : cell.models) {
        const Model& model = scene.models[i];
        const Aabb& modelBounds = grid.bounds[i];
        drawDistance_ = distanceTo(view.cameraPosition, modelBounds);
        const bool tooFar = model.cullDistance > 0.0f && drawDistance_ > model.cullDistance;
        bool skip = tooFar || (shadowPass && !model.castShadows) ||
                    (shadowMinCaster_ > 0.0f && grid.diagonal[i] < shadowMinCaster_);
        if (!skip && grid.detail[i]) {
            // Grass and weeds: fewer of them with distance, shadows only up close.
            const float d = drawDistance_ / model.cullDistance;
            const float keep = 1.0f - (1.0f - quality_.detailDensityFar) * glm::smoothstep(0.3f, 1.0f, d);
            skip = grid.random[i] > keep || (shadowPass && (shadowCascade_ > 0 || drawDistance_ > quality_.detailShadowDistance));
        }
        if (skip || !visible(modelBounds) || occluded(modelBounds)) {
            if (mainPass)
                keepAliveLods(model, nullptr);
            ++culled_;
            continue;
        }
        const bool selected = static_cast<int>(scene.boxes.size() + i) == view.selectedObject;
        // Distance LOD: the simpler copy beyond lodDistance (the other one stays uploaded).
        const Model* draw = &model;
        if (model.lod) {
            draw = &model.lodFor(glm::distance(view.cameraPosition, (modelBounds.min + modelBounds.max) * 0.5f), quality_.lodScale);
            if (draw == &model && (shadowCascade_ > 0 || (shadowPass && grid.detail[i])))
                draw = model.lod.get(); // shadows: the simple copy is enough
            if (mainPass)
                keepAliveLods(model, draw);
        }
        if (!quality_.instancing || selected || draw->skeleton || draw->parts.empty()) {
            drawModel(*draw, model.transform(), selected ? 1.0f : 0.0f);
            continue;
        }
        // Batch with other copies: same shared mesh list and the same material overrides.
        std::uint64_t key = reinterpret_cast<std::uintptr_t>(&draw->parts[0]);
        for (const auto& [slot, material] : draw->overrideMaterials)
            key = key * 1099511628211ull ^ reinterpret_cast<std::uintptr_t>(material.get());
        auto [it, added] = batchIndex_.emplace(key, batches_.size());
        if (added)
            batches_.push_back({draw, {}, drawDistance_});
        InstanceBatch& batch = batches_[it->second];
        batch.transforms.push_back(model.transform());
        batch.distance = std::min(batch.distance, drawDistance_);
        }
    }
    if (scene.terrain && !scene.terrain->foliage.empty())
        addFoliage(*scene.terrain, view);
    flushInstances();
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
        if ((!view.editorHelpers || shadowCascade_ >= 0) && e.helper())
            continue; // map markers, sound sources, lights: editor only, never cast shadows
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
        if (e.type == "sound" || e.type == "particle" || e.type == "anomaly" || e.type == "light" || e.type == "trigger" ||
            e.type == "relay" || e.type == "reverb" || e.type == "ambience") { // marker cube; a sound zone itself is the entity size
            const Aabb m = e.markerBounds();
            const bool glow = e.type == "light" && !depthOnly; // a bulb: unlit, in the light's color
            if (glow)
                glUniform1i(loc_.emissive, 1);
            drawBox((m.min + m.max) * 0.5f, m.max - m.min, glow ? glm::mix(e.color, glm::vec3(1.0f, 0.75f, 0.2f), highlight) : e.color,
                    highlight, 0.0f);
            if (glow)
                glUniform1i(loc_.emissive, 0);
            continue;
        }
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
    if (cascadeUpdate_ == 0)
        return; // every cascade is cached this frame
    useMeshProgram(programs_.depth);
    glViewport(0, 0, shadowRes_, shadowRes_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE); // open meshes (foliage, planes) cast shadows too
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    for (int i = 0; i < cascadeCount_; ++i) {
        if (!(cascadeUpdate_ & (1u << i)))
            continue;
        const glm::mat4& matrix = cascades_[i].matrix;
        glNamedFramebufferTextureLayer(shadowFbo_, GL_DEPTH_ATTACHMENT, shadowTex_, 0, i);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(matrix));
        RenderView shadowView = view;
        shadowView.drawPlayerStart = false;
        shadowCascade_ = i;
        // Far cascades: casters below ~1.5 % of the cascade's diameter cover less than a
        // few texels there, so their shadows are left out (grass, debris, small props).
        shadowMinCaster_ = quality_.shadowCache && i > 0 ? cascades_[i].radius * 0.03f : 0.0f;
        drawGeometry(scene, shadowView, true, matrix, true);
    }
    shadowCascade_ = -1;
    shadowMinCaster_ = 0.0f;
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_CULL_FACE);
}

namespace {
constexpr int kClusterX = 16, kClusterY = 9, kClusterZ = 24;
constexpr std::size_t kMaxLights = 512;

// std430 layout of one light in shader storage (pbr.frag `LocalLight`).
struct GpuLight {
    glm::vec4 posRange;  // xyz, w: range
    glm::vec4 colorType; // rgb linear * intensity, w: 0 point, 1 spot
    glm::vec4 dirShadow; // xyz spot axis, w: first shadow layer (-1 = none)
    glm::vec4 cone;      // x: cos outer, y: 1 / (cos inner - cos outer), z: shadow normal offset per m, w: 1 = flashlight cookie
};
} // namespace

void Renderer::ensureLocalShadowMap()
{
    const int res = std::max(0, quality_.localShadowResolution);
    const int layers = std::clamp(quality_.localShadowLayers, 1, 64);
    if (res == localShadowRes_ && (res == 0 || layers == localShadowLayers_))
        return;
    if (localShadowTex_)
        glDeleteTextures(1, &localShadowTex_);
    if (localShadowFbo_)
        glDeleteFramebuffers(1, &localShadowFbo_);
    localShadowTex_ = localShadowFbo_ = 0;
    localShadowRes_ = res;
    localShadowLayers_ = layers;
    if (res <= 0)
        return;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &localShadowTex_);
    glTextureStorage3D(localShadowTex_, 1, GL_DEPTH_COMPONENT24, res, res, layers);
    glTextureParameteri(localShadowTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(localShadowTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(localShadowTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(localShadowTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(localShadowTex_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(localShadowTex_, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    label(GL_TEXTURE, localShadowTex_, "local light shadows");
    glCreateFramebuffers(1, &localShadowFbo_);
}

void Renderer::collectLights(const Scene& scene, const RenderView& view)
{
    frameLights_.clear();
    localShadows_.clear();
    if (view.shading == Shading::Unlit || view.shading == Shading::Wireframe)
        return;
    const glm::mat4 viewProj = view.projection * view.view;
    const glm::mat4 t = glm::transpose(viewProj);
    const glm::vec4 planes[6] = {t[3] + t[0], t[3] - t[0], t[3] + t[1], t[3] - t[1], t[3] + t[2], t[3] - t[2]};
    auto inView = [&](const LocalLight& l) {
        for (const glm::vec4& p : planes)
            if (glm::dot(glm::vec3(p), l.position) + p.w < -l.range * glm::length(glm::vec3(p)))
                return false;
        return true;
    };
    auto add = [&](const LocalLight& l) {
        if (l.range > 0.0f && glm::dot(l.color, glm::vec3(1.0f)) > 0.0f && inView(l))
            frameLights_.push_back(l);
    };
    for (const Entity& e : scene.entities) {
        if (e.type != "light")
            continue;
        LocalLight l;
        if (evaluateLight(e, static_cast<double>(view.time), l))
            add(l);
    }
    for (const LocalLight& l : view.lights)
        add(l);
    auto distance = [&](const LocalLight& l) { return std::max(0.0f, glm::distance(l.position, view.cameraPosition) - l.range); };
    if (frameLights_.size() > kMaxLights) {
        std::nth_element(frameLights_.begin(), frameLights_.begin() + kMaxLights, frameLights_.end(),
                         [&](const LocalLight& a, const LocalLight& b) { return distance(a) < distance(b); });
        frameLights_.resize(kMaxLights);
    }

    // Shadows: the nearest casters get layers while they last (point lights take six).
    ensureLocalShadowMap();
    if (!localShadowTex_)
        return;
    std::vector<int> order;
    for (std::size_t i = 0; i < frameLights_.size(); ++i)
        if (frameLights_[i].castShadows)
            order.push_back(static_cast<int>(i));
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return glm::distance(frameLights_[a].position, view.cameraPosition) < glm::distance(frameLights_[b].position, view.cameraPosition);
    });
    int layer = 0;
    for (int index : order) {
        const LocalLight& l = frameLights_[static_cast<std::size_t>(index)];
        const int faces = l.type == LightType::Spot ? 1 : 6;
        if (layer + faces > localShadowLayers_)
            continue;
        LocalShadow s;
        s.light = index;
        s.layer = layer;
        s.faces = faces;
        const float nearPlane = std::clamp(l.range * 0.005f, 0.02f, 0.1f);
        if (l.type == LightType::Spot) {
            const float fov = std::min(glm::radians(170.0f), 2.0f * glm::radians(l.outerAngle) + glm::radians(4.0f));
            const glm::vec3 up = std::abs(l.direction.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            s.matrices[0] = glm::perspective(fov, 1.0f, nearPlane, l.range) * glm::lookAt(l.position, l.position + l.direction, up);
            s.texelScale = 2.0f * std::tan(fov * 0.5f) / static_cast<float>(localShadowRes_);
        } else {
            // Cube faces +X -X +Y -Y +Z -Z, a little wider than 90 degrees so filtering does not seam.
            const float fov = 2.0f * std::atan(1.0f + 4.0f / static_cast<float>(localShadowRes_));
            const glm::mat4 proj = glm::perspective(fov, 1.0f, nearPlane, l.range);
            const glm::vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            const glm::vec3 ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
            for (int f = 0; f < 6; ++f)
                s.matrices[f] = proj * glm::lookAt(l.position, l.position + dirs[f], ups[f]);
            s.texelScale = 2.0f * std::tan(fov * 0.5f) / static_cast<float>(localShadowRes_);
        }
        localShadows_.push_back(s);
        layer += faces;
    }
}

void Renderer::renderLocalShadows(const Scene& scene, const RenderView& view)
{
    if (localShadows_.empty())
        return;
    useMeshProgram(programs_.depth);
    glViewport(0, 0, localShadowRes_, localShadowRes_);
    glBindFramebuffer(GL_FRAMEBUFFER, localShadowFbo_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);
    RenderView shadowView = view;
    shadowView.drawPlayerStart = false;
    shadowCascade_ = 0; // level of detail and detail objects as in the nearest sun cascade
    shadowMinCaster_ = 0.0f;
    for (const LocalShadow& s : localShadows_)
        for (int f = 0; f < s.faces; ++f) {
            glNamedFramebufferTextureLayer(localShadowFbo_, GL_DEPTH_ATTACHMENT, localShadowTex_, 0, s.layer + f);
            glClear(GL_DEPTH_BUFFER_BIT);
            glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(s.matrices[f]));
            drawGeometry(scene, shadowView, true, s.matrices[f], false);
        }
    shadowCascade_ = -1;
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_CULL_FACE);
}

void Renderer::uploadLightClusters(const RenderView& view)
{
    if (!lightBuffer_) {
        glCreateBuffers(1, &lightBuffer_);
        glCreateBuffers(1, &lightGridBuffer_);
        glCreateBuffers(1, &lightIndexBuffer_);
        glCreateBuffers(1, &lightShadowBuffer_);
        label(GL_BUFFER, lightBuffer_, "local lights");
        label(GL_BUFFER, lightGridBuffer_, "light clusters");
    }
    std::vector<GpuLight> lights;
    lights.reserve(std::max<std::size_t>(1, frameLights_.size()));
    std::vector<glm::mat4> shadowMatrices;
    std::vector<int> shadowLayer(frameLights_.size(), -1);
    std::vector<float> shadowTexel(frameLights_.size(), 0.0f);
    for (const LocalShadow& s : localShadows_) {
        shadowLayer[static_cast<std::size_t>(s.light)] = s.layer;
        shadowTexel[static_cast<std::size_t>(s.light)] = s.texelScale;
        if (shadowMatrices.size() < static_cast<std::size_t>(s.layer + s.faces))
            shadowMatrices.resize(static_cast<std::size_t>(s.layer + s.faces));
        for (int f = 0; f < s.faces; ++f)
            shadowMatrices[static_cast<std::size_t>(s.layer + f)] = s.matrices[f];
    }
    for (std::size_t i = 0; i < frameLights_.size(); ++i) {
        const LocalLight& l = frameLights_[i];
        GpuLight g;
        g.posRange = glm::vec4(l.position, l.range);
        g.colorType = glm::vec4(l.color, l.type == LightType::Spot ? 1.0f : 0.0f);
        g.dirShadow = glm::vec4(glm::normalize(l.direction), static_cast<float>(shadowLayer[i]));
        const float cosOuter = std::cos(glm::radians(l.outerAngle));
        const float cosInner = std::cos(glm::radians(std::min(l.innerAngle, l.outerAngle)));
        g.cone = glm::vec4(cosOuter, 1.0f / std::max(cosInner - cosOuter, 1e-4f), shadowTexel[i],
                           l.cookie && l.type == LightType::Spot ? 1.0f : 0.0f);
        lights.push_back(g);
    }
    if (lights.empty())
        lights.push_back(GpuLight{});
    if (shadowMatrices.empty())
        shadowMatrices.emplace_back(1.0f);

    // Clusters: a screen grid of tiles, sliced in depth logarithmically between the
    // near plane and the farthest light. Orthographic views use one cluster.
    const glm::mat4& p = view.projection;
    const bool ortho = p[3][3] == 1.0f;
    const int cx = ortho ? 1 : kClusterX, cy = ortho ? 1 : kClusterY, cz = ortho ? 1 : kClusterZ;
    const float nearPlane = ortho ? 0.1f : std::max(0.01f, p[3][2] / (p[2][2] - 1.0f));
    float farPlane = nearPlane * 2.0f;
    std::vector<glm::vec4> viewSpace(frameLights_.size());
    for (std::size_t i = 0; i < frameLights_.size(); ++i) {
        viewSpace[i] = glm::vec4(glm::vec3(view.view * glm::vec4(frameLights_[i].position, 1.0f)), frameLights_[i].range);
        farPlane = std::max(farPlane, -viewSpace[i].z + viewSpace[i].w);
    }
    const float sliceScale = static_cast<float>(cz) / std::log(farPlane / nearPlane);
    const float sliceBias = -std::log(nearPlane) * sliceScale;
    const int clusterCount = cx * cy * cz;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs; // (cluster, light)
    for (std::size_t i = 0; i < frameLights_.size(); ++i) {
        int x0 = 0, x1 = cx - 1, y0 = 0, y1 = cy - 1, z0 = 0, z1 = cz - 1;
        if (!ortho) {
            const glm::vec4 c = viewSpace[i];
            const float r = c.w;
            const float d0 = -c.z - r, d1 = -c.z + r; // depth range (positive in front)
            if (d1 <= nearPlane)
                continue;
            auto slice = [&](float d) {
                return std::clamp(static_cast<int>(std::floor(std::log(std::max(d, nearPlane)) * sliceScale + sliceBias)), 0, cz - 1);
            };
            z0 = slice(d0);
            z1 = slice(d1);
            if (d0 > nearPlane) {
                // Screen rectangle of the sphere's view-space box (conservative).
                auto range = [&](float lo, float hi, float scale, int cells, int& a, int& b) {
                    const float mn = scale * (lo < 0.0f ? lo / d0 : lo / d1);
                    const float mx = scale * (hi > 0.0f ? hi / d0 : hi / d1);
                    a = std::clamp(static_cast<int>(std::floor((mn * 0.5f + 0.5f) * static_cast<float>(cells))), 0, cells - 1);
                    b = std::clamp(static_cast<int>(std::floor((mx * 0.5f + 0.5f) * static_cast<float>(cells))), 0, cells - 1);
                    return mx >= -1.0f && mn <= 1.0f;
                };
                if (!range(c.x - r, c.x + r, p[0][0], cx, x0, x1) || !range(c.y - r, c.y + r, p[1][1], cy, y0, y1))
                    continue;
            }
        }
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    pairs.emplace_back(static_cast<std::uint32_t>(x + y * cx + z * cx * cy), static_cast<std::uint32_t>(i));
    }
    std::vector<std::uint32_t> counts(static_cast<std::size_t>(clusterCount), 0);
    for (const auto& pr : pairs)
        ++counts[pr.first];
    struct Header {
        glm::vec4 params; // x: slice scale, y: slice bias, z: near, w: unused
        glm::ivec4 dims;  // x, y, z cluster counts, w: light count
    };
    std::vector<std::uint32_t> grid(sizeof(Header) / 4 + static_cast<std::size_t>(clusterCount) * 2, 0);
    Header header{glm::vec4(sliceScale, sliceBias, nearPlane, 0.0f), glm::ivec4(cx, cy, cz, static_cast<int>(frameLights_.size()))};
    std::memcpy(grid.data(), &header, sizeof header);
    std::uint32_t* clusters = grid.data() + sizeof(Header) / 4;
    std::uint32_t offset = 0;
    for (int c = 0; c < clusterCount; ++c) {
        clusters[c * 2] = offset;
        clusters[c * 2 + 1] = 0;
        offset += counts[static_cast<std::size_t>(c)];
    }
    std::vector<std::uint32_t> indices(std::max<std::size_t>(1, pairs.size()), 0);
    for (const auto& pr : pairs) {
        std::uint32_t& n = clusters[pr.first * 2 + 1];
        indices[clusters[pr.first * 2] + n] = pr.second;
        ++n;
    }
    glNamedBufferData(lightBuffer_, static_cast<GLsizeiptr>(lights.size() * sizeof(GpuLight)), lights.data(), GL_STREAM_DRAW);
    glNamedBufferData(lightGridBuffer_, static_cast<GLsizeiptr>(grid.size() * 4), grid.data(), GL_STREAM_DRAW);
    glNamedBufferData(lightIndexBuffer_, static_cast<GLsizeiptr>(indices.size() * 4), indices.data(), GL_STREAM_DRAW);
    glNamedBufferData(lightShadowBuffer_, static_cast<GLsizeiptr>(shadowMatrices.size() * sizeof(glm::mat4)), shadowMatrices.data(),
                      GL_STREAM_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, lightBuffer_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, lightGridBuffer_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, lightIndexBuffer_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, lightShadowBuffer_);
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
    ++frameNumber_;
    lastDrawCalls_ = drawCalls_;
    lastInstanced_ = instanced_;
    building_.drawCalls = drawCalls_;
    building_.instanced = instanced_;
    building_.culled = culled_;
    building_.triangles = triangles_;
    building_.uploads = uploads_;
    building_.occluded = lastOccluded_;
    building_.lights = static_cast<int>(frameLights_.size());
    building_.lightShadows = static_cast<int>(localShadows_.size());
    uploads_ = 0;
    collectQueries(); // finishes building_ into stats_ and starts the next frame
    drawCalls_ = 0;
    instanced_ = 0;
    triangles_ = 0;
    updateStreaming(); // before releaseUnused(): uses last frame's distances
    releaseUnused();
    texturesUploaded_ = 0;
    culled_ = 0;
    GLint target = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
    targetFbo_ = target;
    targetX_ = view.viewportX;
    targetY_ = view.viewportY;
    resolvedEnv_ = environmentAt(scene.environment, scene.sunDirection, &sky_);
    env_ = &resolvedEnv_;
    time_ = view.time;
    collectDistortions(view);
    keptAlive_.clear();
    updateModelGrid(scene);
    pollOcclusion();
    view_ = view.view;
    projection_ = view.projection;
    // Render scale: the scene is drawn at a fraction of the output size and stretched in the
    // final pass (post / FXAA), for slow GPUs; menus and HUD stay sharp.
    outWidth_ = std::max(1, view.viewportWidth);
    outHeight_ = std::max(1, view.viewportHeight);
    const float scale = std::clamp(quality_.renderScale, 0.25f, 1.0f);
    ensureTargets(std::max(1, static_cast<int>(std::lround(outWidth_ * scale))), std::max(1, static_cast<int>(std::lround(outHeight_ * scale))));

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, jointBuffer_);
    updateEnvironment(scene, view);
    updateEnvironmentMap(scene);
    collectLights(scene, view);
    beginPass(PassShadows);
    renderShadows(scene, view);
    renderLocalShadows(scene, view);
    endPass();
    uploadLightClusters(view);

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
    beginPass(PassDepth);
    occ_.testing = occ_.valid && quality_.occlusionCulling && view.shading != Shading::Wireframe &&
                   glm::distance(occ_.eye, view.cameraPosition) < 3.0f;
    // Occlusion depends on where the eye is, not where it looks: grow boxes by the distance moved.
    occ_.inflate = glm::distance(occ_.eye, view.cameraPosition) + 0.05f;
    drawGeometry(scene, view, true, viewProj);
    endPass();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (quality_.occlusionCulling)
        captureOcclusion(view);

    if (quality_.ssao) {
        beginPass(PassSsao);
        renderSsao();
        endPass();
    }

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
    glBindTextureUnit(13, localShadowTex_ ? localShadowTex_ : shadowDummy_);
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
    beginPass(PassOpaque);
    occ_.hidden = 0;
    drawGeometry(scene, view, false, viewProj);
    lastOccluded_ = occ_.hidden;
    occ_.testing = false;
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
    endPass(); // opaque + sky
    if (!transparent_.empty()) {
        beginPass(PassTransparent);
        useMeshProgram(programs_.mesh);
        glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniformMatrix3fv(loc_.envRotation, 1, GL_FALSE, glm::value_ptr(glm::mat3(1.0f)));
        glUniform1i(loc_.shading, static_cast<int>(view.shading));
        drawTransparent(view);
        glUniform1i(loc_.shading, 0);
        endPass();
    }

    if (!view.sprites.empty() && !wireframe) {
        beginPass(PassTransparent);
        drawSprites(view.sprites, viewProj, view.shading, false);
        endPass();
    }

    if (!view.debug.empty()) {
        beginPass(PassTransparent);
        drawDebug(view.debug, viewProj);
        endPass();
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

void Renderer::drawTerrain(const Terrain& t, const RenderView& view)
{
    const int res = t.resolution;
    constexpr int kChunk = GpuTerrain::kChunk, kLevels = GpuTerrain::kLevels;
    const std::size_t gridCount = static_cast<std::size_t>(res) * res;
    if (terrain_.meshVersion != t.meshVersion || terrain_.resolution != res) {
        // Grid vertices, then a copy of each lowered by kSkirt metres (skirts).
        constexpr float kSkirt = 6.0f;
        std::vector<Vertex> vertices(gridCount * 2);
        const float d = t.spacing();
        auto h = [&](int x, int z) {
            return t.heights[static_cast<std::size_t>(std::clamp(z, 0, res - 1)) * res + std::clamp(x, 0, res - 1)];
        };
        for (int z = 0; z < res; ++z)
            for (int x = 0; x < res; ++x) {
                Vertex& v = vertices[static_cast<std::size_t>(z) * res + x];
                v.position = {t.origin.x + x * d, h(x, z), t.origin.y + z * d};
                v.normal = glm::normalize(glm::vec3(h(x - 1, z) - h(x + 1, z), 2.0f * d, h(x, z - 1) - h(x, z + 1)));
                v.uv = {static_cast<float>(x) / (res - 1), static_cast<float>(z) / (res - 1)};
                Vertex& skirt = vertices[gridCount + static_cast<std::size_t>(z) * res + x];
                skirt = v;
                skirt.position.y -= kSkirt;
            }
        if (!terrain_.vao) {
            glCreateVertexArrays(1, &terrain_.vao);
            glCreateBuffers(1, &terrain_.vbo);
            glCreateBuffers(1, &terrain_.ibo);
        }
        glNamedBufferData(terrain_.vbo, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_DYNAMIC_DRAW);
        const int quads = res - 1;
        const int chunksX = (quads + kChunk - 1) / kChunk;
        if (terrain_.resolution != res) {
            // Index ranges of every chunk at every level (the same for any heights).
            terrain_.chunks.assign(static_cast<std::size_t>(chunksX) * chunksX, {});
            terrain_.chunksX = chunksX;
            std::vector<std::uint32_t> indices;
            auto vid = [&](int x, int z) { return static_cast<std::uint32_t>(z * res + x); };
            const auto skirtBase = static_cast<std::uint32_t>(gridCount);
            for (int cz = 0; cz < chunksX; ++cz)
                for (int cx = 0; cx < chunksX; ++cx) {
                    GpuTerrain::Chunk& chunk = terrain_.chunks[static_cast<std::size_t>(cz) * chunksX + cx];
                    const int x0 = cx * kChunk, z0 = cz * kChunk;
                    const int x1 = std::min(quads, x0 + kChunk), z1 = std::min(quads, z0 + kChunk);
                    for (int level = 0; level < kLevels; ++level) {
                        const int step = 1 << level;
                        chunk.first[level] = static_cast<std::uint32_t>(indices.size());
                        // Sample positions: every step-th vertex, always including the chunk edges.
                        std::vector<int> xs, zs;
                        for (int x = x0; x < x1; x += step)
                            xs.push_back(x);
                        xs.push_back(x1);
                        for (int z = z0; z < z1; z += step)
                            zs.push_back(z);
                        zs.push_back(z1);
                        for (std::size_t b = 0; b + 1 < zs.size(); ++b)
                            for (std::size_t a = 0; a + 1 < xs.size(); ++a) {
                                const std::uint32_t i00 = vid(xs[a], zs[b]), i10 = vid(xs[a + 1], zs[b]);
                                const std::uint32_t i01 = vid(xs[a], zs[b + 1]), i11 = vid(xs[a + 1], zs[b + 1]);
                                indices.insert(indices.end(), {i00, i01, i10, i10, i01, i11}); // counter-clockwise from above
                            }
                        // Skirts along the four edges.
                        auto skirt = [&](std::uint32_t a, std::uint32_t b) { // both windings: seen from either side
                            indices.insert(indices.end(), {a, b, skirtBase + a, skirtBase + a, b, skirtBase + b,
                                                           b, a, skirtBase + a, b, skirtBase + a, skirtBase + b});
                        };
                        for (std::size_t a = 0; a + 1 < xs.size(); ++a) {
                            skirt(vid(xs[a], z0), vid(xs[a + 1], z0));
                            skirt(vid(xs[a + 1], z1), vid(xs[a], z1));
                        }
                        for (std::size_t b = 0; b + 1 < zs.size(); ++b) {
                            skirt(vid(x0, zs[b + 1]), vid(x0, zs[b]));
                            skirt(vid(x1, zs[b]), vid(x1, zs[b + 1]));
                        }
                        chunk.count[level] = static_cast<std::uint32_t>(indices.size()) - chunk.first[level];
                    }
                }
            glNamedBufferData(terrain_.ibo, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_STATIC_DRAW);
            setupVertexArray(terrain_.vao, terrain_.vbo);
            glVertexArrayElementBuffer(terrain_.vao, terrain_.ibo);
        }
        // Chunk bounds from the heights.
        for (int cz = 0; cz < chunksX; ++cz)
            for (int cx = 0; cx < chunksX; ++cx) {
                const int x0 = cx * kChunk, z0 = cz * kChunk;
                const int x1 = std::min(quads, x0 + kChunk), z1 = std::min(quads, z0 + kChunk);
                float lo = 1e30f, hi = -1e30f;
                for (int z = z0; z <= z1; ++z)
                    for (int x = x0; x <= x1; ++x) {
                        const float y = t.heights[static_cast<std::size_t>(z) * res + x];
                        lo = std::min(lo, y);
                        hi = std::max(hi, y);
                    }
                terrain_.chunks[static_cast<std::size_t>(cz) * chunksX + cx].bounds = {
                    {t.origin.x + x0 * d, lo, t.origin.y + z0 * d}, {t.origin.x + x1 * d, hi, t.origin.y + z1 * d}};
            }
        terrain_.resolution = res;
        terrain_.meshVersion = t.meshVersion;
    }
    if (terrain_.splatVersion != t.splatVersion || terrain_.splatResolution != t.splatResolution) {
        if (terrain_.splat && terrain_.splatResolution != t.splatResolution) {
            glDeleteTextures(1, &terrain_.splat);
            terrain_.splat = 0;
        }
        if (!terrain_.splat) {
            glCreateTextures(GL_TEXTURE_2D, 1, &terrain_.splat);
            glTextureStorage2D(terrain_.splat, 1, GL_RGBA8, t.splatResolution, t.splatResolution);
            glTextureParameteri(terrain_.splat, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(terrain_.splat, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(terrain_.splat, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(terrain_.splat, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTextureSubImage2D(terrain_.splat, 0, 0, 0, t.splatResolution, t.splatResolution, GL_RGBA, GL_UNSIGNED_BYTE, t.splat.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        terrain_.splatResolution = t.splatResolution;
        terrain_.splatVersion = t.splatVersion;
    }
    const Aabb bounds = t.bounds();
    drawDistance_ = distanceTo(view.cameraPosition, bounds);
    for (const TerrainLayer& layer : t.layers) // layer maps stay resident like everything near the camera
        if (layer.material)
            for (const auto& asset : layer.material->maps)
                if (asset)
                    keepTextureAlive(asset->texture.get());
    if (!visible(bounds))
        return;
    glUniformMatrix4fv(loc_.model, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
    glUniform1f(loc_.highlight, 0.0f);
    if (!depthOnly_) {
        applySurface(nullptr, nullptr, glm::vec3(1.0f), true); // resets every map flag
        const Program& mesh = programs_.mesh;
        glUniform1i(loc_.grid, 0);
        glUniform1i(mesh.loc("uTerrain"), 1);
        glUniform2fv(mesh.loc("uTerrainOrigin"), 1, glm::value_ptr(t.origin));
        glUniform1f(mesh.loc("uTerrainSize"), t.size);
        glm::vec4 tiling(0.25f), roughness(0.9f), normalStrength(1.0f);
        glm::ivec4 hasAlbedo(0), hasNormal(0);
        glm::vec3 colors[4] = {glm::vec3(0.45f, 0.42f, 0.36f), glm::vec3(0.38f, 0.42f, 0.25f), glm::vec3(0.32f, 0.3f, 0.27f),
                               glm::vec3(0.5f)};
        const unsigned albedoUnits[4] = {0, 10, 11, 12}, normalUnits[4] = {1, 2, 3, 5};
        for (int i = 0; i < Terrain::kLayers; ++i) {
            const TerrainLayer& layer = t.layers[static_cast<std::size_t>(i)];
            tiling[i] = layer.tiling;
            const Material* m = layer.material.get();
            if (!m)
                continue;
            colors[i] = m->color;
            roughness[i] = m->roughness;
            normalStrength[i] = m->normalStrength;
            auto bind = [&](MapKind kind, unsigned unit) {
                const auto& asset = m->map(kind);
                const Texture* tex = asset ? asset->texture.get() : nullptr;
                const unsigned handle = tex && tex->valid() ? texture(*tex) : 0u;
                if (handle)
                    glBindTextureUnit(unit, handle);
                return handle != 0 ? 1 : 0;
            };
            hasAlbedo[i] = bind(MapKind::Albedo, albedoUnits[i]);
            hasNormal[i] = bind(MapKind::Normal, normalUnits[i]);
        }
        glBindTextureUnit(4, terrain_.splat);
        glUniform4fv(mesh.loc("uLayerTiling"), 1, glm::value_ptr(tiling));
        glUniform3fv(mesh.loc("uLayerColor"), 4, glm::value_ptr(colors[0]));
        glUniform4iv(mesh.loc("uLayerHasAlbedo"), 1, glm::value_ptr(hasAlbedo));
        glUniform4iv(mesh.loc("uLayerHasNormal"), 1, glm::value_ptr(hasNormal));
        glUniform4fv(mesh.loc("uLayerRoughness"), 1, glm::value_ptr(roughness));
        glUniform4fv(mesh.loc("uLayerNormalStrength"), 1, glm::value_ptr(normalStrength));
    } else {
        glUniform1i(loc_.alphaMode, 0);
    }
    glBindVertexArray(terrain_.vao);
    // Chunks: frustum and occlusion culling, level by distance (coarser in shadow maps).
    const float lodBase = 60.0f * quality_.lodScale;
    for (const GpuTerrain::Chunk& chunk : terrain_.chunks) {
        if (!visible(chunk.bounds) || occluded(chunk.bounds)) {
            ++culled_;
            continue;
        }
        const float d = distanceTo(view.cameraPosition, chunk.bounds);
        int level = d < lodBase ? 0 : static_cast<int>(std::floor(std::log2(d / lodBase))) + 1;
        if (shadowCascade_ >= 0)
            level += shadowCascade_ == 0 ? 1 : 2;
        level = std::clamp(level, 0, kLevels - 1);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(chunk.count[level]), GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(static_cast<std::uintptr_t>(chunk.first[level]) * sizeof(std::uint32_t)));
        ++drawCalls_;
        triangles_ += chunk.count[level] / 3;
    }
    if (!depthOnly_)
        glUniform1i(programs_.mesh.loc("uTerrain"), 0);
}

void Renderer::addFoliage(const Terrain& t, const RenderView& view)
{
    constexpr float kCell = 32.0f;
    FoliageCache& c = foliageCache_;
    if (c.terrain != &t || c.version != t.foliageVersion) {
        c.terrain = &t;
        c.version = t.foliageVersion;
        c.types.assign(t.foliageTypes.size(), {});
        c.transforms.resize(t.foliage.size());
        std::vector<std::unordered_map<std::uint64_t, std::size_t>> cellIndex(t.foliageTypes.size());
        for (std::size_t i = 0; i < t.foliage.size(); ++i) {
            const FoliageInstance& f = t.foliage[i];
            if (f.type >= t.foliageTypes.size())
                continue;
            glm::mat4 m = glm::translate(glm::mat4(1.0f), f.position);
            if (f.tiltX != 0.0f || f.tiltZ != 0.0f) {
                m = glm::rotate(m, f.tiltX, glm::vec3(1.0f, 0.0f, 0.0f));
                m = glm::rotate(m, f.tiltZ, glm::vec3(0.0f, 0.0f, 1.0f));
            }
            m = glm::rotate(m, glm::radians(f.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
            m = glm::scale(m, glm::vec3(f.scale));
            c.transforms[i] = m;
            const FoliageType& type = t.foliageTypes[f.type];
            const auto cx = static_cast<std::int64_t>(std::floor(f.position.x / kCell));
            const auto cz = static_cast<std::int64_t>(std::floor(f.position.z / kCell));
            const std::uint64_t key = (static_cast<std::uint64_t>(cx) << 32) ^ static_cast<std::uint32_t>(cz);
            auto [it, added] = cellIndex[f.type].emplace(key, c.types[f.type].size());
            if (added)
                c.types[f.type].push_back({Aabb{f.position, f.position}, {}});
            FoliageCell& cell = c.types[f.type][it->second];
            cell.instances.push_back(static_cast<std::uint32_t>(i));
            // Instance bounds: the model's box, scaled, around the position (rotation-safe radius).
            glm::vec3 reach(1.0f);
            if (type.model && !type.model->parts.empty()) {
                const Aabb mb = type.model->localBounds.min != type.model->localBounds.max ? type.model->localBounds : Aabb{};
                reach = glm::vec3(glm::max(glm::length(mb.min), glm::length(mb.max)));
                if (reach.x <= 0.0f) {
                    Aabb pb{type.model->parts[0].positions.empty() ? glm::vec3(0.0f) : type.model->parts[0].positions[0],
                            type.model->parts[0].positions.empty() ? glm::vec3(0.0f) : type.model->parts[0].positions[0]};
                    for (const MeshPart& part : type.model->parts)
                        for (const glm::vec3& p : part.positions) {
                            pb.min = glm::min(pb.min, p);
                            pb.max = glm::max(pb.max, p);
                        }
                    reach = glm::vec3(glm::max(glm::length(pb.min), glm::length(pb.max)));
                }
            }
            cell.bounds.min = glm::min(cell.bounds.min, f.position - reach * f.scale);
            cell.bounds.max = glm::max(cell.bounds.max, f.position + reach * f.scale);
        }
    }
    for (std::size_t ti = 0; ti < t.foliageTypes.size() && ti < c.types.size(); ++ti) {
        const FoliageType& type = t.foliageTypes[ti];
        if (!type.model || type.model->parts.empty())
            continue;
        drawDistance_ = 1e30f;
        keepAliveOnce(*type.model);
        for (const FoliageType::Lod& lod : type.lods)
            if (lod.model)
                keepAliveOnce(*lod.model);
        if (shadowCascade_ >= 0 && !type.castShadows)
            continue;
        const Aabb& lb = type.model->localBounds;
        const float diagonal = glm::length(lb.max - lb.min) * type.maxScale;
        if (shadowMinCaster_ > 0.0f && diagonal > 0.0f && diagonal < shadowMinCaster_)
            continue; // far shadow cascade: grass and small plants cast nothing there
        // Low ground cover (as for detail models): shadows only up close, in the first cascade.
        const bool detail = !type.collision && (lb.max.y - lb.min.y) * type.maxScale < 2.0f;
        const float shadowReach = detail && shadowCascade_ >= 0 ? (shadowCascade_ == 0 ? quality_.detailShadowDistance : 0.0f) : 1e30f;
        if (shadowReach <= 0.0f)
            continue;
        // LOD levels: the model, then each lod beyond its distance (lodScale applies).
        const Model* levels[8] = {type.model.get()};
        float from[8] = {0.0f};
        int levelCount = 1;
        for (const FoliageType::Lod& lod : type.lods)
            if (lod.model && !lod.model->parts.empty() && levelCount < 8) {
                levels[levelCount] = lod.model.get();
                from[levelCount++] = lod.distance * quality_.lodScale;
            }
        const float farDensity = detail ? std::min(type.farDensity, quality_.detailDensityFar) : type.farDensity;
        for (const FoliageCell& cell : c.types[ti]) {
            const float cellDistance = distanceTo(view.cameraPosition, cell.bounds);
            if ((type.cullDistance > 0.0f && cellDistance > type.cullDistance) || cellDistance > shadowReach ||
                !visible(cell.bounds) || occluded(cell.bounds)) {
                culled_ += static_cast<int>(cell.instances.size());
                continue;
            }
            for (std::uint32_t i : cell.instances) {
                const float d = glm::distance(view.cameraPosition, t.foliage[i].position);
                if ((type.cullDistance > 0.0f && d > type.cullDistance + 2.0f) || d > shadowReach)
                    continue;
                if (farDensity < 1.0f && type.cullDistance > 0.0f) {
                    // Thinning toward the cull distance, stable per instance.
                    std::uint32_t r = i * 2654435761u;
                    r ^= r >> 15;
                    r *= 2246822519u;
                    r ^= r >> 13;
                    const float keep = 1.0f - (1.0f - farDensity) * glm::smoothstep(0.3f, 1.0f, d / type.cullDistance);
                    if (static_cast<float>(r & 0xffffff) / 16777216.0f > keep)
                        continue;
                }
                int level = 0;
                while (level + 1 < levelCount && d > from[level + 1])
                    ++level;
                if (shadowCascade_ >= 0 && level == 0 && levelCount > 1)
                    level = 1; // shadows: the simpler copy
                const Model* draw = levels[level];
                const std::uint64_t key = reinterpret_cast<std::uintptr_t>(&draw->parts[0]);
                auto [it, added] = batchIndex_.emplace(key, batches_.size());
                if (added)
                    batches_.push_back({draw, {}, cellDistance});
                InstanceBatch& batch = batches_[it->second];
                batch.distance = std::min(batch.distance, cellDistance);
                batch.transforms.push_back(c.transforms[i]);
            }
        }
    }
}

void Renderer::flushInstances()
{
    // One buffer upload for all batches of this pass, then one draw per mesh part and batch.
    instanceData_.clear();
    std::vector<std::pair<int, int>> ranges; // base, count per batch (count 1 = plain draw)
    for (const InstanceBatch& b : batches_) {
        if (b.transforms.size() < 2) {
            ranges.emplace_back(-1, 1);
            continue;
        }
        ranges.emplace_back(static_cast<int>(instanceData_.size()), static_cast<int>(b.transforms.size()));
        instanceData_.insert(instanceData_.end(), b.transforms.begin(), b.transforms.end());
    }
    if (!instanceData_.empty()) {
        const std::size_t bytes = instanceData_.size() * sizeof(glm::mat4);
        if (!instanceBuffer_)
            glCreateBuffers(1, &instanceBuffer_);
        if (bytes > instanceCapacity_) {
            instanceCapacity_ = std::max(bytes, instanceCapacity_ * 2);
            glNamedBufferData(instanceBuffer_, static_cast<GLsizeiptr>(instanceCapacity_), nullptr, GL_STREAM_DRAW);
        } else {
            glInvalidateBufferData(instanceBuffer_); // orphan: earlier passes may still read it
        }
        glNamedBufferSubData(instanceBuffer_, 0, static_cast<GLsizeiptr>(bytes), instanceData_.data());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, instanceBuffer_);
    }
    for (std::size_t i = 0; i < batches_.size(); ++i) {
        const InstanceBatch& b = batches_[i];
        drawDistance_ = b.distance;
        if (ranges[i].first < 0) {
            drawModel(*b.model, b.transforms.front(), 0.0f);
        } else {
            drawModelInstanced(*b.model, ranges[i].first, ranges[i].second);
            if (collectTransparent_)
                instanced_ += ranges[i].second;
        }
    }
    batches_.clear();
    batchIndex_.clear();
}

void Renderer::drawModelInstanced(const Model& model, int base, int count)
{
    glUniform1f(loc_.highlight, 0.0f);
    glUniform1i(loc_.skinned, 0);
    glUniform1i(loc_.instanced, 1);
    glUniform1i(loc_.instanceBase, base);
    for (const MeshPart& part : model.parts) {
        const GpuMesh& mesh = upload(part);
        const Material* material = model.materialFor(part);
        const SurfaceAlpha alpha = surfaceAlpha(material, &part);
        if (alpha.mode == AlphaMode::Blend) {
            if (depthOnly_)
                continue;
            if (collectTransparent_) {
                for (int i = 0; i < count; ++i)
                    transparent_.push_back({&part, material, nullptr, instanceData_[static_cast<std::size_t>(base + i)],
                                            drawDistance_, 0.0f});
                continue;
            }
        }
        applySurface(material, &part, part.color, false);
        glBindVertexArray(mesh.vao);
        const bool twoSided = alpha.doubleSided && glIsEnabled(GL_CULL_FACE);
        if (twoSided)
            glDisable(GL_CULL_FACE);
        glDrawElementsInstanced(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr, count);
        ++drawCalls_;
        triangles_ += static_cast<long long>(mesh.indexCount / 3) * count;
        if (twoSided)
            glEnable(GL_CULL_FACE);
    }
    glUniform1i(loc_.instanced, 0);
    applySurface(nullptr, nullptr, glm::vec3(1.0f), false);
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

std::vector<std::uint8_t> Renderer::readPixels(int width, int height) const
{
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height)) * 3);
    if (rgb.empty())
        return rgb;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    // OpenGL rows start at the bottom.
    const std::size_t row = static_cast<std::size_t>(width) * 3;
    for (int y = 0; y < height / 2; ++y)
        std::swap_ranges(rgb.begin() + static_cast<std::ptrdiff_t>(y * row), rgb.begin() + static_cast<std::ptrdiff_t>((y + 1) * row),
                         rgb.begin() + static_cast<std::ptrdiff_t>((height - 1 - y) * row));
    return rgb;
}

void Renderer::drawSprites(const std::vector<SpriteBatch>& batches, const glm::mat4& viewProj, Shading shading, bool overlay)
{
    // Decals first (they lie on surfaces), then particles back to front.
    std::vector<const SpriteBatch*> order;
    order.reserve(batches.size());
    std::size_t total = 0;
    for (const SpriteBatch& b : batches)
        if (!b.instances.empty()) {
            order.push_back(&b);
            total += b.instances.size();
        }
    if (order.empty())
        return;
    std::stable_sort(order.begin(), order.end(), [](const SpriteBatch* a, const SpriteBatch* b) {
        if (a->decal != b->decal)
            return a->decal;
        return a->distance > b->distance;
    });
    if (!spriteBuffer_ || total > spriteCapacity_) {
        if (spriteBuffer_)
            glDeleteBuffers(1, &spriteBuffer_);
        spriteCapacity_ = std::max<std::size_t>(total * 3 / 2, 1024);
        glCreateBuffers(1, &spriteBuffer_);
        glNamedBufferData(spriteBuffer_, static_cast<GLsizeiptr>(spriteCapacity_ * sizeof(SpriteInstance)), nullptr, GL_DYNAMIC_DRAW);
    }
    std::vector<SpriteInstance> all;
    all.reserve(total);
    std::vector<int> bases;
    bases.reserve(order.size());
    for (const SpriteBatch* b : order) {
        bases.push_back(static_cast<int>(all.size()));
        all.insert(all.end(), b->instances.begin(), b->instances.end());
    }
    glNamedBufferSubData(spriteBuffer_, 0, static_cast<GLsizeiptr>(all.size() * sizeof(SpriteInstance)), all.data());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, spriteBuffer_);

    const Program& p = programs_.sprite;
    glUseProgram(p.id);
    glUniformMatrix4fv(p.loc("uSpriteViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(emptyVao_);
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    const bool budget = ignoreUploadBudget_;
    ignoreUploadBudget_ = true; // a particle texture shows on its first frame
    for (std::size_t i = 0; i < order.size(); ++i) {
        const SpriteBatch& b = *order[i];
        unsigned tex = 0;
        if (b.texture && b.texture->texture && b.texture->texture->valid()) {
            drawDistance_ = b.distance;
            tex = texture(*b.texture->texture);
        }
        glBindTextureUnit(0, tex ? tex : whiteTex_);
        glUniform1i(p.loc("uSpriteHasTexture"), tex ? 1 : 0);
        glUniform2i(p.loc("uSpriteGrid"), std::max(1, b.columns), std::max(1, b.rows));
        glUniform1i(p.loc("uSpriteLit"), b.lit && !overlay && shading != Shading::Unlit ? 1 : 0);
        glUniform1i(p.loc("uSpriteNoFog"), overlay ? 1 : 0);
        glUniform1i(p.loc("uSpriteDecal"), b.decal ? 1 : 0);
        glUniform1i(p.loc("uSpriteAdditive"), b.blend == SpriteBlend::Additive ? 1 : 0);
        glUniform1f(p.loc("uDecalLift"), b.decal ? 0.004f : 0.0f);
        // Decals lie on surfaces: a depth bias that grows with the slope keeps them in front at
        // grazing angles and at distance, where a fixed lift loses to depth precision (speckles).
        if (b.decal) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-2.0f, -16.0f);
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        glUniform1i(p.loc("uSpriteBase"), bases[i]);
        if (b.blend == SpriteBlend::Additive)
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
        else
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(b.instances.size()));
        ++drawCalls_;
    }
    ignoreUploadBudget_ = budget;
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

void Renderer::drawDebug(const std::vector<const DebugGeometry*>& items, const glm::mat4& viewProj)
{
    std::size_t total = 0;
    for (const DebugGeometry* d : items)
        if (d)
            total += d->triangles.size() + d->lines.size();
    if (total == 0)
        return;
    if (!debugBuffer_ || total > debugCapacity_) {
        if (debugBuffer_)
            glDeleteBuffers(1, &debugBuffer_);
        debugCapacity_ = std::max<std::size_t>(total * 3 / 2, 4096);
        glCreateBuffers(1, &debugBuffer_);
        glNamedBufferData(debugBuffer_, static_cast<GLsizeiptr>(debugCapacity_ * sizeof(glm::vec4)), nullptr, GL_DYNAMIC_DRAW);
    }
    std::vector<glm::vec4> all;
    all.reserve(total);
    for (const DebugGeometry* d : items) {
        if (!d)
            continue;
        for (const glm::vec3& v : d->triangles)
            all.emplace_back(v, 1.0f);
        // w: the vertex colour packed as RGBA8 bits when the geometry has per-line colours.
        const bool perVertex = d->lineColors.size() == d->lines.size();
        for (std::size_t i = 0; i < d->lines.size(); ++i)
            all.emplace_back(d->lines[i], perVertex ? glm::uintBitsToFloat(glm::packUnorm4x8(d->lineColors[i])) : 1.0f);
    }
    glNamedBufferSubData(debugBuffer_, 0, static_cast<GLsizeiptr>(all.size() * sizeof(glm::vec4)), all.data());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, debugBuffer_);
    const Program& p = programs_.debug;
    glUseProgram(p.id);
    glUniformMatrix4fv(p.loc("uDebugViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(emptyVao_);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.0f, -4.0f); // in front of the surface it lies on
    GLint first = 0;
    for (const DebugGeometry* d : items) {
        if (!d)
            continue;
        if (d->xray)
            glDisable(GL_DEPTH_TEST);
        else
            glEnable(GL_DEPTH_TEST);
        const auto tris = static_cast<GLsizei>(d->triangles.size());
        if (tris > 0) {
            glUniform4fv(p.loc("uDebugColor"), 1, glm::value_ptr(d->fill));
            glDrawArrays(GL_TRIANGLES, first, tris);
            if (d->edges.a > 0.0f) {
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glUniform4fv(p.loc("uDebugColor"), 1, glm::value_ptr(d->edges));
                glDrawArrays(GL_TRIANGLES, first, tris);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
            drawCalls_ += 2;
            first += tris;
        }
        const auto lines = static_cast<GLsizei>(d->lines.size());
        if (lines > 0) {
            glUniform4fv(p.loc("uDebugColor"), 1, glm::value_ptr(d->lineColor));
            glUniform1i(p.loc("uDebugVertexColor"), d->lineColors.size() == d->lines.size() ? 1 : 0);
            glDrawArrays(GL_LINES, first, lines);
            glUniform1i(p.loc("uDebugVertexColor"), 0);
            ++drawCalls_;
            first += lines;
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_POLYGON_OFFSET_LINE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

void Renderer::renderOverlay(const RenderView& view, const OverlayPass& pass)
{
    // First-person layer: own depth range and projection, so the weapon never clips into walls.
    beginPass(PassOverlay);
    overlayDrawn_ = true;
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
    if (!pass.sprites.empty())
        drawSprites(pass.sprites, pass.projection, view.shading, true);
    glBindVertexArray(0);
    endPass();
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

void Renderer::collectDistortions(const RenderView& view)
{
    distortions_.clear();
    overlayDrawn_ = false;
    aspect_ = static_cast<float>(std::max(1, view.viewportWidth)) / static_cast<float>(std::max(1, view.viewportHeight));
    const glm::mat4 viewProj = view.projection * view.view;
    struct Candidate {
        glm::vec4 data;
        float weight;
    };
    std::vector<Candidate> found;
    for (const ScreenDistortion& d : view.distortions) {
        if (d.strength <= 0.0f || d.radius <= 0.0f)
            continue;
        const glm::vec4 clip = viewProj * glm::vec4(d.position, 1.0f);
        if (clip.w <= 0.05f)
            continue;
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        const float r = d.radius * view.projection[1][1] / clip.w; // NDC height units
        if (std::abs(ndc.x) - r * 2.0f > 1.0f || std::abs(ndc.y) - r * 2.0f > 1.0f || r < 0.002f)
            continue;
        const float rUv = std::min(r * 0.5f, 1.5f);
        found.push_back({glm::vec4(ndc * 0.5f + 0.5f, rUv, d.lens ? -d.strength : d.strength), d.strength * rUv});
    }
    std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return a.weight > b.weight; });
    for (std::size_t i = 0; i < found.size() && i < 8; ++i)
        distortions_.push_back(found[i].data);
}

void Renderer::updateExposure(const Environment& e)
{
    const float dt = lastExposureTime_ < 0.0f ? 0.0f : std::clamp(time_ - lastExposureTime_, 0.0f, 0.5f);
    if (time_ < lastExposureTime_ - 1.0f)
        exposureReset_ = true; // time went back: another level or a reload
    lastExposureTime_ = time_;
    if (e.autoExposure <= 0.0f) {
        exposureReset_ = true; // jump to the right value when it is switched on
        return;
    }
    constexpr int kSize = 128, kLevels = 8; // 128 .. 1
    if (!lumTex_) {
        lumTex_ = makeTexture2D(GL_RG16F, kSize, kSize, GL_LINEAR_MIPMAP_LINEAR, kLevels);
        lumFbo_ = makeFbo(lumTex_);
        for (int i = 0; i < 2; ++i) {
            adaptTex_[i] = makeTexture2D(GL_R32F, 1, 1, GL_NEAREST);
            adaptFbo_[i] = makeFbo(adaptTex_[i]);
        }
        label(GL_TEXTURE, lumTex_, "exposure metering");
        exposureReset_ = true;
    }
    glBindVertexArray(emptyVao_);
    glBindFramebuffer(GL_FRAMEBUFFER, lumFbo_);
    glViewport(0, 0, kSize, kSize);
    glUseProgram(programs_.luminance.id);
    glUniform1i(programs_.luminance.loc("uMaskOverlay"), overlayDrawn_ ? 1 : 0);
    glBindTextureUnit(0, hdrColor_);
    glBindTextureUnit(1, depthTex_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glGenerateTextureMipmap(lumTex_);

    const int next = 1 - adaptIndex_;
    glBindFramebuffer(GL_FRAMEBUFFER, adaptFbo_[next]);
    glViewport(0, 0, 1, 1);
    const Program& p = programs_.exposure;
    glUseProgram(p.id);
    glUniform1f(p.loc("uLevel"), static_cast<float>(kLevels - 1));
    glUniform1f(p.loc("uDt"), dt);
    glUniform2f(p.loc("uEvRange"), std::min(e.exposureMinEv, e.exposureMaxEv), std::max(e.exposureMinEv, e.exposureMaxEv));
    glUniform2f(p.loc("uSpeed"), std::max(0.01f, e.adaptBrighten), std::max(0.01f, e.adaptDarken));
    glUniform1i(p.loc("uReset"), exposureReset_ ? 1 : 0);
    glBindTextureUnit(0, lumTex_);
    glBindTextureUnit(1, adaptTex_[adaptIndex_]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    adaptIndex_ = next;
    exposureReset_ = false;
}

void Renderer::updateLut(const Environment& e)
{
    if (e.lut == lutPath_)
        return;
    lutPath_ = e.lut;
    if (lutTex_)
        glDeleteTextures(1, &lutTex_);
    lutTex_ = 0;
    lutSize_ = 0;
    if (e.lut.empty())
        return;
    // Strip of N slices side by side (N*N x N): x within a slice = red, y = green (top row 0), slice = blue.
    std::ifstream in(e.lut, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int w = 0, h = 0, comp = 0;
    unsigned char* pixels = bytes.empty() ? nullptr
                                          : stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp, 4);
    if (!pixels || h < 2 || w != h * h) {
        LOG_WARN("render", "color LUT %s: expected an N*N x N strip (e.g. 1024x32), got %dx%d", e.lut.filename().string().c_str(), w, h);
        if (pixels)
            stbi_image_free(pixels);
        return;
    }
    const int n = h;
    std::vector<unsigned char> volume(static_cast<std::size_t>(n) * n * n * 4);
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                const unsigned char* src = pixels + (static_cast<std::size_t>(g) * w + static_cast<std::size_t>(b) * n + r) * 4;
                unsigned char* dst = volume.data() + ((static_cast<std::size_t>(b) * n + g) * n + r) * 4;
                std::memcpy(dst, src, 4);
            }
    stbi_image_free(pixels);
    glCreateTextures(GL_TEXTURE_3D, 1, &lutTex_);
    glTextureStorage3D(lutTex_, 1, GL_RGBA8, n, n, n);
    glTextureSubImage3D(lutTex_, 0, 0, 0, 0, n, n, n, GL_RGBA, GL_UNSIGNED_BYTE, volume.data());
    glTextureParameteri(lutTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(lutTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
        glTextureParameteri(lutTex_, wrap, GL_CLAMP_TO_EDGE);
    label(GL_TEXTURE, lutTex_, "color LUT");
    lutSize_ = n;
    LOG_INFO("render", "color LUT %s (%d^3)", e.lut.filename().string().c_str(), n);
}

void Renderer::present()
{
    if (!pendingPresent_)
        return;
    pendingPresent_ = false;
    resetGlState();
    beginPass(PassPost);
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
        glViewport(targetX_, targetY_, outWidth_, outHeight_);
    updateExposure(e);
    updateLut(e);
    glBindFramebuffer(GL_FRAMEBUFFER, fxaa ? ldrFbo_ : static_cast<unsigned>(targetFbo_));
    if (fxaa)
        glViewport(0, 0, width_, height_);
    else
        glViewport(targetX_, targetY_, outWidth_, outHeight_);
    const Program& post = programs_.post;
    glUseProgram(post.id);
    glUniform1f(post.loc("uExposure"), e.exposure);
    glUniform1f(post.loc("uAutoExposure"), adaptTex_[0] ? std::clamp(e.autoExposure, 0.0f, 1.0f) : 0.0f);
    {
        // White balance: warm/cold along orange-blue, tint along green-magenta, luminance kept.
        const float t = std::clamp(e.temperature, -1.0f, 1.0f), m = std::clamp(e.greenMagenta, -1.0f, 1.0f);
        glm::vec3 wb(1.0f + 0.22f * t + 0.06f * m, 1.0f + 0.02f * t - 0.12f * m, 1.0f - 0.22f * t + 0.06f * m);
        wb /= glm::dot(wb, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        glUniform3fv(post.loc("uWhiteBalance"), 1, glm::value_ptr(wb));
    }
    glUniform3fv(post.loc("uLift"), 1, glm::value_ptr(e.lift));
    glUniform3fv(post.loc("uGamma"), 1, glm::value_ptr(e.gamma));
    glUniform3fv(post.loc("uGain"), 1, glm::value_ptr(e.gain));
    glUniform1f(post.loc("uLutStrength"), lutTex_ ? std::clamp(e.lutStrength, 0.0f, 1.0f) : 0.0f);
    glUniform1f(post.loc("uLutSize"), static_cast<float>(std::max(lutSize_, 2)));
    glBindTextureUnit(3, adaptTex_[adaptIndex_] ? adaptTex_[adaptIndex_] : whiteTex_);
    if (lutTex_)
        glBindTextureUnit(4, lutTex_);
    glUniform1f(post.loc("uBloomIntensity"), quality_.bloom ? e.bloomIntensity : 0.0f);
    glUniform1f(post.loc("uSaturation"), e.saturation);
    glUniform1f(post.loc("uContrast"), e.contrast);
    glUniform3fv(post.loc("uTint"), 1, glm::value_ptr(e.tint));
    glUniform1f(post.loc("uVignette"), e.vignette);
    glUniform1f(post.loc("uGrain"), e.grain);
    glUniform1f(post.loc("uTime"), time_);
    glUniform1i(post.loc("uLumaAlpha"), fxaa ? 1 : 0);
    glUniform1i(post.loc("uDistortCount"), static_cast<int>(distortions_.size()));
    if (!distortions_.empty())
        glUniform4fv(post.loc("uDistort"), static_cast<int>(distortions_.size()), glm::value_ptr(distortions_[0]));
    glUniform1f(post.loc("uAspect"), aspect_);
    glUniform1i(post.loc("uDistortMask"), overlayDrawn_ ? 1 : 0);
    glBindTextureUnit(0, hdrColor_);
    glBindTextureUnit(1, bloomTex_.empty() ? whiteTex_ : bloomTex_[0]);
    glBindTextureUnit(2, depthTex_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (fxaa) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(targetFbo_));
        glViewport(targetX_, targetY_, outWidth_, outHeight_);
        glUseProgram(programs_.fxaa.id);
        glBindTextureUnit(0, ldrTex_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    // Leave depth of the scene in the target too, for tools that draw on top.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(targetFbo_));
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
    endPass();
}

const char* Renderer::passName(int pass)
{
    static const char* names[PassCount] = {"Shadows", "Depth pre-pass", "SSAO", "Opaque + sky", "Transparent", "Post (bloom, tone, FXAA)",
                                           "First person"};
    return pass >= 0 && pass < PassCount ? names[pass] : "?";
}

void Renderer::beginPass(int pass)
{
    if (activePass_ >= 0)
        endPass();
    activePass_ = pass;
    passStart_ = std::chrono::steady_clock::now();
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, static_cast<GLuint>(pass), -1, passName(pass)); // named in RenderDoc
    unsigned& q = queries_[queryFrame_][pass];
    if (!q)
        glCreateQueries(GL_TIME_ELAPSED, 1, &q);
    glBeginQuery(GL_TIME_ELAPSED, q);
    queryIssued_[queryFrame_][pass] = true;
}

void Renderer::endPass()
{
    if (activePass_ < 0)
        return;
    glEndQuery(GL_TIME_ELAPSED);
    glPopDebugGroup();
    building_.cpuMs[activePass_] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - passStart_).count();
    activePass_ = -1;
}

void Renderer::collectQueries()
{
    // GPU times of the oldest frame in the ring (its queries are done by now, normally).
    const int oldest = (queryFrame_ + 1) % kQueryFrames;
    bool valid = true, any = false;
    double gpu[PassCount] = {};
    for (int p = 0; p < PassCount; ++p) {
        if (!queryIssued_[oldest][p])
            continue;
        any = true;
        GLint available = 0;
        glGetQueryObjectiv(queries_[oldest][p], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) {
            valid = false;
            continue;
        }
        GLuint64 ns = 0;
        glGetQueryObjectui64v(queries_[oldest][p], GL_QUERY_RESULT, &ns);
        gpu[p] = static_cast<double>(ns) / 1.0e6;
        queryIssued_[oldest][p] = false;
    }
    // CPU numbers are from the frame that just ended; GPU numbers a few frames older.
    stats_.drawCalls = building_.drawCalls;
    stats_.instanced = building_.instanced;
    stats_.culled = building_.culled;
    stats_.triangles = building_.triangles;
    stats_.uploads = building_.uploads;
    stats_.occluded = building_.occluded;
    stats_.cpuTotalMs = 0.0;
    for (int p = 0; p < PassCount; ++p) {
        stats_.cpuMs[p] = building_.cpuMs[p];
        stats_.cpuTotalMs += building_.cpuMs[p];
    }
    if (any && valid) {
        stats_.gpuTotalMs = 0.0;
        for (int p = 0; p < PassCount; ++p) {
            stats_.gpuMs[p] = gpu[p];
            stats_.gpuTotalMs += gpu[p];
        }
        stats_.gpuValid = true;
    }
    building_ = FrameStats{};
    queryFrame_ = (queryFrame_ + 1) % kQueryFrames;
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
    ++uploads_;
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
    ++uploads_;
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
        ++drawCalls_;
        triangles_ += mesh.indexCount / 3;
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

void Renderer::keepAliveLods(const Model& model, const Model* drawn)
{
    // Every level stays uploaded, so switching LOD never waits for an upload.
    if (&model != drawn)
        keepAliveOnce(model);
    if (model.lod && model.lod.get() != drawn)
        keepAliveOnce(*model.lod);
    for (const Model::FarLod& far : model.farLods)
        if (far.model && far.model.get() != drawn)
            keepAliveOnce(*far.model);
}

void Renderer::keepAliveOnce(const Model& model)
{
    // Copies share their mesh list: one walk over its parts and maps per frame is enough.
    // Material overrides differ per copy: those are walked one by one.
    const void* key = model.parts.empty() || !model.overrideMaterials.empty() ? static_cast<const void*>(&model)
                                                                               : static_cast<const void*>(&model.parts[0]);
    if (keptAlive_.insert(key).second)
        keepAlive(model);
}

void Renderer::updateModelGrid(const Scene& scene)
{
    // Rebuilt when models are added, removed, moved or change their culling settings.
    std::uint64_t h = 1469598103934665603ull ^ scene.models.size();
    auto mix = [&h](const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t k = 0; k < n; ++k)
            h = (h ^ b[k]) * 1099511628211ull;
    };
    for (const Model& m : scene.models) {
        mix(&m.position, sizeof m.position);
        mix(&m.rotation, sizeof m.rotation);
        mix(&m.scale, sizeof m.scale);
        mix(&m.cullDistance, sizeof m.cullDistance);
        const void* parts = m.parts.empty() ? nullptr : &m.parts[0];
        mix(&parts, sizeof parts);
        const std::uint8_t flags = static_cast<std::uint8_t>(m.collision | (m.castShadows << 1) | (m.skeleton ? 4 : 0));
        mix(&flags, 1);
    }
    if (grid_.scene == &scene && grid_.hash == h)
        return;
    grid_.scene = &scene;
    grid_.hash = h;
    ++grid_.rebuilds;
    const std::size_t n = scene.models.size();
    grid_.bounds.resize(n);
    grid_.diagonal.resize(n);
    grid_.random.resize(n);
    grid_.detail.resize(n);
    grid_.cells.clear();
    constexpr float kCell = 32.0f;
    std::unordered_map<std::uint64_t, std::size_t> index;
    for (std::size_t i = 0; i < n; ++i) {
        const Model& m = scene.models[i];
        const Aabb b = m.bounds();
        grid_.bounds[i] = b;
        grid_.diagonal[i] = glm::length(b.max - b.min);
        std::uint32_t r = static_cast<std::uint32_t>(i) * 2654435761u;
        r ^= r >> 15;
        r *= 2246822519u;
        r ^= r >> 13;
        grid_.random[i] = static_cast<float>(r & 0xffffff) / 16777216.0f;
        grid_.detail[i] = m.cullDistance > 0.0f && !m.collision && !m.skeleton && b.max.y - b.min.y < 2.0f; // low: grass, weeds, debris
        const glm::vec3 c = (b.min + b.max) * 0.5f;
        const auto cx = static_cast<std::int32_t>(std::floor(c.x / kCell)), cz = static_cast<std::int32_t>(std::floor(c.z / kCell));
        const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) | static_cast<std::uint32_t>(cz);
        auto [it, added] = index.emplace(key, grid_.cells.size());
        if (added)
            grid_.cells.emplace_back();
        ModelCell& cell = grid_.cells[it->second];
        if (cell.models.empty())
            cell.maxCull = m.cullDistance;
        else if (cell.maxCull > 0.0f)
            cell.maxCull = m.cullDistance > 0.0f ? std::max(cell.maxCull, m.cullDistance) : 0.0f;
        cell.bounds.min = glm::min(cell.bounds.min, b.min);
        cell.bounds.max = glm::max(cell.bounds.max, b.max);
        cell.maxDiagonal = std::max(cell.maxDiagonal, grid_.diagonal[i]);
        cell.models.push_back(static_cast<std::uint32_t>(i));
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
        for (const Model::FarLod& far : m.farLods)
            if (far.model)
                models.emplace_back(far.model.get(), d);
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

// ---------------------------------------------------------------------------
// Occlusion culling. After the depth pre-pass a small copy of the depth buffer
// (farthest depth per block) is read back through a pixel buffer with a fence; a
// frame or two later the CPU tests boxes against it in the view it was taken from.
// Occlusion only depends on the eye position, so turning is safe; boxes grow by the
// distance the eye moved since, and the test is off after a jump.

void Renderer::captureOcclusion(const RenderView& view)
{
    const int w = 160, h = std::max(1, static_cast<int>(std::lround(160.0 * height_ / std::max(1, width_))));
    if (occ_.width != w || occ_.height != h || !occ_.tex) {
        releaseOcclusion();
        glCreateTextures(GL_TEXTURE_2D, 1, &occ_.tex);
        glTextureStorage2D(occ_.tex, 1, GL_R32F, w, h);
        glCreateFramebuffers(1, &occ_.fbo);
        glNamedFramebufferTexture(occ_.fbo, GL_COLOR_ATTACHMENT0, occ_.tex, 0);
        for (OcclusionReadback& r : occ_.ring) {
            glCreateBuffers(1, &r.pbo);
            glNamedBufferStorage(r.pbo, static_cast<GLsizeiptr>(w) * h * sizeof(float), nullptr, GL_CLIENT_STORAGE_BIT);
        }
        occ_.width = w;
        occ_.height = h;
    }
    OcclusionReadback& r = occ_.ring[static_cast<std::size_t>(occ_.next)];
    if (r.fence)
        return; // still in flight (GPU far behind): skip this frame's copy
    glBindFramebuffer(GL_FRAMEBUFFER, occ_.fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(programs_.hiz.id);
    glUniform2i(programs_.hiz.loc("uSrcSize"), width_, height_);
    glUniform2i(programs_.hiz.loc("uDstSize"), w, h);
    glBindTextureUnit(0, depthTex_);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, r.pbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, w, h, GL_RED, GL_FLOAT, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    r.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    r.viewProj = view.projection * view.view;
    r.eye = view.cameraPosition;
    occ_.next = (occ_.next + 1) % static_cast<int>(occ_.ring.size());
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo_);
    glViewport(0, 0, width_, height_);
}

void Renderer::pollOcclusion()
{
    // Newest finished copy wins; nothing waits for the GPU.
    for (int k = 0; k < static_cast<int>(occ_.ring.size()); ++k) {
        OcclusionReadback& r = occ_.ring[static_cast<std::size_t>((occ_.next + k) % static_cast<int>(occ_.ring.size()))];
        if (!r.fence)
            continue;
        const GLenum status = glClientWaitSync(static_cast<GLsync>(r.fence), 0, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
            continue;
        glDeleteSync(static_cast<GLsync>(r.fence));
        r.fence = nullptr;
        occ_.depth.resize(static_cast<std::size_t>(occ_.width) * occ_.height);
        glGetNamedBufferSubData(r.pbo, 0, static_cast<GLsizeiptr>(occ_.depth.size() * sizeof(float)), occ_.depth.data());
        occ_.viewProj = r.viewProj;
        occ_.eye = r.eye;
        occ_.valid = true;
    }
}

void Renderer::releaseOcclusion()
{
    for (OcclusionReadback& r : occ_.ring) {
        if (r.fence)
            glDeleteSync(static_cast<GLsync>(r.fence));
        if (r.pbo)
            glDeleteBuffers(1, &r.pbo);
        r = {};
    }
    if (occ_.fbo)
        glDeleteFramebuffers(1, &occ_.fbo);
    if (occ_.tex)
        glDeleteTextures(1, &occ_.tex);
    occ_.fbo = occ_.tex = 0;
    occ_.width = occ_.height = 0;
    occ_.valid = false;
    occ_.testing = false; // a resize mid-frame (capture runs between the depth and opaque passes) drops the old copy
    occ_.depth.clear();
}

bool Renderer::occluded(const Aabb& box) const
{
    if (!occ_.testing || shadowCascade_ >= 0)
        return false;
    const glm::vec3 lo = box.min - glm::vec3(occ_.inflate), hi = box.max + glm::vec3(occ_.inflate);
    glm::vec2 mn(1e30f), mx(-1e30f);
    float nearest = 1.0f;
    for (int c = 0; c < 8; ++c) {
        const glm::vec4 p = occ_.viewProj * glm::vec4((c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z, 1.0f);
        if (p.w <= 1e-3f)
            return false; // crosses the eye plane
        const glm::vec3 ndc = glm::vec3(p) / p.w;
        mn = glm::min(mn, glm::vec2(ndc));
        mx = glm::max(mx, glm::vec2(ndc));
        nearest = std::min(nearest, ndc.z * 0.5f + 0.5f);
    }
    // NaN/inf (degenerate matrices while a viewport resizes to 0) never reach the
    // float->int conversions below, which are undefined for out-of-range values.
    if (!(nearest > 0.0f) || !std::isfinite(mn.x + mn.y + mx.x + mx.y))
        return false;
    const int w = occ_.width, h = occ_.height;
    if (w <= 0 || h <= 0 || occ_.depth.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h))
        return false;
    auto pixel = [](float ndc, int size) {
        return static_cast<int>(std::floor(std::clamp(ndc * 0.5f + 0.5f, -1.0f, 2.0f) * static_cast<float>(size)));
    };
    const int x0 = std::max(0, pixel(mn.x, w));
    const int x1 = std::min(w - 1, pixel(mx.x, w));
    const int y0 = std::max(0, pixel(mn.y, h));
    const int y1 = std::min(h - 1, pixel(mx.y, h));
    if (x0 > x1 || y0 > y1)
        return false; // outside that view: the frustum test decides
    if ((x1 - x0 + 1) * (y1 - y0 + 1) > 2048)
        return false; // big on screen: rarely hidden, not worth the test
    for (int y = y0; y <= y1; ++y) {
        const float* row = &occ_.depth[static_cast<std::size_t>(y) * w];
        for (int x = x0; x <= x1; ++x)
            if (row[x] >= nearest)
                return false;
    }
    ++const_cast<Renderer*>(this)->occ_.hidden;
    return true;
}

} // namespace engine
