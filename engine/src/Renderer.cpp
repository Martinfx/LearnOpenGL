#include "engine/Renderer.h"
#include "engine/Atmosphere.h"
#include "engine/Vfs.h"
#include "engine/Water.h"

#include <glad/gl.h>
#include "engine/EntityIO.h"
#include "engine/Probes.h"
#include "engine/JobSystem.h"
#include "engine/Log.h"
#include "engine/Paths.h"
#include "engine/Profile.h"
#include <chrono>
#include <thread>
#include <optional>
#ifdef TRACY_ENABLE
#include <tracy/TracyOpenGL.hpp> // after glad: GPU zones use its timer queries
#endif
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

// Where distance LOD, culling and streaming measure from (the map bake moves it to the ground).
glm::vec3 lodEye(const RenderView& view) { return view.lodEye.value_or(view.cameraPosition); }

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
    glm::mat4 rainMatrix; // world -> rain occlusion map (top-down)
    glm::vec4 weather;    // x: wetness, y: lightning flash, z: 1 = rain map valid, w: puddles
    glm::vec4 wind;       // xz: direction the wind blows, y: strength 0..1, w: gusts 0..1
    glm::vec4 volumetric; // x: 1 = on, y: near, z: 1 / log(far / near), w: far
    glm::vec4 probes;     // x: 1 = baked probes on, y: reflection cube max mip
    glm::mat4 prevViewProj; // previous frame, no jitter (SSR)
    glm::vec4 ssr;          // x: on, y: max distance, z: intensity, w: max roughness
    glm::vec4 atmosphere;   // x: physical sky 0..1, y: radiance scale
    glm::vec4 shadowSoft;   // x: PCSS on, y: tan of the sun's angular radius, z: max caster distance, w: max filter radius
    glm::vec4 groundHaze;   // x: density, y: ground height, z: thickness
    glm::vec4 contact;      // contact shadows: x ray length (0 = off), y thickness, z fade-out distance
    glm::vec4 clouds;       // volumetric clouds: x on, y base height, z thickness, w density
    glm::vec4 benderInfo;   // x: grass bender count
    glm::vec4 benders[2 * kMaxGrassBenders]; // per bender: xyz centre, w radius; x strength, y mode
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

unsigned linkCompute(const std::map<std::string, std::string>& files, const std::string& name, std::string* error)
{
    const auto cs = files.find(name);
    if (cs == files.end()) {
        if (error)
            *error = "Missing shader " + name;
        return 0;
    }
    const unsigned shader = compileStage(GL_COMPUTE_SHADER, withIncludes(cs->second, files), name, error);
    if (!shader)
        return 0;
    const unsigned program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        std::array<char, 4096> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        if (error)
            *error = name + ": " + log.data();
        glDeleteProgram(program);
        return 0;
    }
    label(GL_PROGRAM, program, name);
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
        q.shadowResolution = 1024; q.shadowCascades = 2; q.ssao = false; q.bloom = false; q.fxaa = true; q.taa = false;
        q.textureMaxSize = 1024; q.textureStreamDistance = 20.0f;
        q.lodScale = 0.5f; q.detailDensityFar = 0.1f; q.detailShadowDistance = 0.0f;
        q.localShadowResolution = 512; q.localShadowLayers = 6;
        q.volumetricFog = 0;
        q.ssr = false;
        q.parallax = false;
        q.softShadows = false;
        q.ssgi = false;
        q.contactShadows = false;
        q.skyOcclusionDirs = 3;
        q.volumetricClouds = 0;
        q.depthOfField = false;
        break;
    case 1:
        q.shadowResolution = 1024; q.shadowCascades = 3; q.ssao = true; q.bloom = true;
        q.textureMaxSize = 2048; q.textureStreamDistance = 30.0f;
        q.lodScale = 0.75f; q.detailDensityFar = 0.15f; q.detailShadowDistance = 10.0f;
        q.localShadowResolution = 512;
        q.taa = false; // FXAA
        q.ssr = false;
        q.ssgi = false;
        q.volumetricFog = 1;
        q.volumetricClouds = 1;
        q.skyOcclusionDirs = 3;
        q.softShadowTaps = 8;
        break;
    case 2:
        // Tuned on a GTX 1060 at 1080p (FPS_ENGINE_PROFILE on the overgrown street): SSGI, the
        // fine fog grid and the 6-direction sky search were the dearest; Ultra keeps them.
        q.ssgi = false;
        q.volumetricFog = 1;
        q.skyOcclusionDirs = 4;
        q.ssrSteps = 16;
        q.softShadowTaps = 8;
        break;
    case 3:
        q.shadowResolution = 4096; q.shadowCascades = 3; q.textureStreamDistance = 80.0f;
        q.lodScale = 1.5f; q.detailDensityFar = 0.35f; q.detailShadowDistance = 25.0f;
        q.localShadowLayers = 18;
        q.ssaoFullRes = true;
        break;
    default: break;
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
                             {&p.fxaa, "fullscreen.vert", "fxaa.frag"},   {&p.taa, "fullscreen.vert", "taa.frag"}, {&p.ssrHistory, "fullscreen.vert", "ssr_history.frag"},
                             {&p.copy, "fullscreen.vert", "copy.frag"},
                             {&p.grid, "fullscreen.vert", "grid.frag"}, {&p.hiz, "fullscreen.vert", "hiz.frag"},
                             {&p.sprite, "sprite.vert", "sprite.frag"},
                             {&p.debug, "debug.vert", "debug.frag"},
                             {&p.luminance, "fullscreen.vert", "luminance.frag"},
                             {&p.exposure, "fullscreen.vert", "exposure.frag"},
                             {&p.water, "water.vert", "water.frag"},
                             {&p.underwater, "fullscreen.vert", "underwater.frag"},
                             {&p.distort, "sprite.vert", "distort.frag"},
                             {&p.clouds, "fullscreen.vert", "clouds.frag"}};
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
    for (auto [program, name] : {std::pair{&p.volumeInject, "volumetric_inject.comp"}, std::pair{&p.volumeIntegrate, "volumetric_integrate.comp"}}) {
        program->id = linkCompute(src, name, error);
        program->locations.clear();
        if (!program->id) {
            destroyPrograms(p);
            return false;
        }
    }
    LOG_INFO("render", "%zu shader programs in %.0f ms (%d from the binary cache)", std::size(entries) + 2,
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), cached);
    return true;
}

void Renderer::destroyPrograms(Programs& p)
{
    for (Program* program : {&p.mesh, &p.depth, &p.sky, &p.envCapture, &p.prefilter, &p.brdf, &p.ssao, &p.blur, &p.bloomDown,
                             &p.bloomUp, &p.post, &p.fxaa, &p.taa, &p.ssrHistory, &p.copy, &p.grid, &p.hiz, &p.sprite, &p.debug, &p.luminance, &p.exposure, &p.water, &p.underwater, &p.distort,
                             &p.volumeInject, &p.volumeIntegrate, &p.clouds}) {
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
    loc_.sway = p.loc("uSway");
    loc_.fade = p.loc("uFade");
    loc_.instanceBase = p.loc("uInstanceBase");
    loc_.alphaCutoff = p.loc("uAlphaCutoff");
    loc_.opacity = p.loc("uOpacity");
    const char* names[9] = {"uHasTexture",      "uHasNormalMap",   "uHasRoughnessMap", "uHasMetallicMap", "uHasOcclusionMap",
                            "uHasEmissiveMap", "uHasDetailAlbedo", "uHasDetailNormal", "uHasHeightMap"};
    static_assert(kMapCount == 9, "a texture slot per MapKind");
    for (int i = 0; i < kMapCount; ++i)
        loc_.hasMap[i] = p.loc(names[i]);
    loc_.heightDepth = p.loc("uHeightDepth");
    loc_.detail = p.loc("uDetail");
    loc_.paintBase = p.loc("uPaintBase");
    loc_.paintBaseVertex = p.loc("uPaintBaseVertex");
    loc_.blendLayers = p.loc("uBlendLayers");
    loc_.blendColor = p.loc("uBlendColor");
    loc_.blendTiling = p.loc("uBlendTiling");
    loc_.blendSurface = p.loc("uBlendSurface");
    loc_.blendNormalStrength = p.loc("uBlendNormalStrength");
    loc_.blendSharpness = p.loc("uBlendSharpness");
    glUniform1i(loc_.paintBase, -1);
    glUniform1i(loc_.blendLayers, 0);
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
    const float farDepth = 1.0f;
    glClearTexImage(shadowDummy_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &farDepth);
    glCreateSamplers(1, &shadowRawSampler_);
    glSamplerParameteri(shadowRawSampler_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(shadowRawSampler_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(shadowRawSampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glSamplerParameteri(shadowRawSampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float farBorder[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glSamplerParameterfv(shadowRawSampler_, GL_TEXTURE_BORDER_COLOR, farBorder);
    glSamplerParameteri(shadowRawSampler_, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glCreateBuffers(1, &interiorBuffer_);
    label(GL_BUFFER, interiorBuffer_, "interior volumes");
    const glm::ivec4 noInteriors(0);
    glNamedBufferData(interiorBuffer_, sizeof noInteriors, &noInteriors, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, interiorBuffer_);
    // Baked probes: none until a level with a .probes file is drawn.
    glCreateBuffers(1, &probeBuffer_);
    glCreateBuffers(1, &probeVolumeBuffer_);
    glNamedBufferData(probeBuffer_, sizeof noInteriors, &noInteriors, GL_DYNAMIC_DRAW);
    glNamedBufferData(probeVolumeBuffer_, sizeof noInteriors, &noInteriors, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, probeBuffer_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, probeVolumeBuffer_);
    glCreateTextures(GL_TEXTURE_CUBE_MAP_ARRAY, 1, &probeDummyCube_);
    glTextureStorage3D(probeDummyCube_, 1, GL_RGBA16F, 1, 1, 6);
    glCreateTextures(GL_TEXTURE_3D, 1, &probeDummy3d_);
    glTextureStorage3D(probeDummy3d_, 1, GL_RGBA16F, 1, 1, 1);
    glCreateTextures(GL_TEXTURE_3D, 1, &volumeDummy_); // no volumetric fog: transmittance 1
    glTextureStorage3D(volumeDummy_, 1, GL_RGBA16F, 1, 1, 1);
    const float clearFog[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    glClearTexImage(volumeDummy_, 0, GL_RGBA, GL_FLOAT, clearFog);

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
#ifdef TRACY_ENABLE
    // Tracy has one GPU context: it belongs to the first renderer (the game's, the editor viewport's).
    if (!tracyGpuOwner()) {
        tracyGpuOwner() = this;
        TracyGpuContext;
        TracyGpuContextName("OpenGL", 6);
    }
#endif
    return true;
}

void Renderer::shutdown()
{
    finishCullJobs();
#ifdef TRACY_ENABLE
    if (tracyGpuOwner() == this)
        tracyGpuOwner() = nullptr;
#endif
    releaseOcclusion();
    if (rainTex_)
        glDeleteTextures(1, &rainTex_);
    if (rainFbo_)
        glDeleteFramebuffers(1, &rainFbo_);
    rainTex_ = rainFbo_ = 0;
    if (cloudNoiseJob_ && cloudNoiseJob_->valid())
        cloudNoiseJob_->wait();
    cloudNoiseJob_.reset();
    for (unsigned* t : {&cloudNoise_, &cloudTex_})
        if (*t)
            glDeleteTextures(1, t);
    if (cloudFbo_)
        glDeleteFramebuffers(1, &cloudFbo_);
    cloudNoise_ = cloudTex_ = cloudFbo_ = 0;
    cloudSize_ = 0;
    meshes_.clear();
    if (poolVao_)
        glDeleteVertexArrays(1, &poolVao_);
    for (unsigned* b : {&poolVbo_, &poolIbo_, &iotaBuffer_, &indirectBuffer_})
        if (*b)
            glDeleteBuffers(1, b);
    poolVao_ = poolVbo_ = poolIbo_ = iotaBuffer_ = indirectBuffer_ = 0;
    probesHeld_.reset();
    probeReflections_ = probeVolumes_ = 0;
    poolVertices_ = RangeAllocator();
    poolIndices_ = RangeAllocator();
    iotaCount_ = indirectCapacity_ = 0;
    if (terrain_.vao) {
        glDeleteVertexArrays(1, &terrain_.vao);
        glDeleteBuffers(1, &terrain_.vbo);
        glDeleteBuffers(1, &terrain_.ibo);
    }
    for (unsigned* tex : {&terrain_.splat, &terrain_.albedoArray, &terrain_.normalArray})
        if (*tex)
            glDeleteTextures(1, tex);
    if (terrain_.copyFbo)
        glDeleteFramebuffers(1, &terrain_.copyFbo);
    terrain_ = GpuTerrain{};
    for (auto& frame : queries_)
        for (unsigned& q : frame)
            if (q) {
                glDeleteQueries(1, &q);
                q = 0;
            }
    foliageCache_ = FoliageCache{};
    for (auto& [owner, gpu] : paints_)
        glDeleteBuffers(1, &gpu.buffer);
    paints_.clear();
    if (instanceBuffer_)
        glDeleteBuffers(1, &instanceBuffer_);
    instanceBuffer_ = 0;
    instanceCapacity_ = 0;
    for (auto& [id, t] : textures_)
        glDeleteTextures(1, &t.handle);
    textures_.clear();
    destroyTargets();
    destroyPrograms(programs_);
    if (shadowRawSampler_) {
        glDeleteSamplers(1, &shadowRawSampler_);
        shadowRawSampler_ = 0;
    }
    for (unsigned* t : {&shadowTex_, &envRaw_, &envMap_, &brdfLut_, &whiteTex_, &shadowDummy_, &localShadowTex_, &lumTex_, &adaptTex_[0],
                        &adaptTex_[1], &lutTex_, &volumeScatter_[0], &volumeScatter_[1], &volumeIntegrated_, &volumeDummy_, &probeCubes_,
                        &probeIrradiance_[0], &probeIrradiance_[1], &probeIrradiance_[2], &probeDummyCube_, &probeDummy3d_})
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
    for (unsigned* b : {&vbo_, &jointBuffer_, &envBuffer_, &lightBuffer_, &lightGridBuffer_, &lightIndexBuffer_, &lightShadowBuffer_, &interiorBuffer_, &probeBuffer_, &probeVolumeBuffer_})
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
    for (unsigned* t : {&hdrColor_, &depthTex_, &ssaoTex_, &ssaoBlurTex_, &ssgiTex_, &ldrTex_, &sceneColorCopy_, &sceneDepthCopy_, &distortTex_,
                         &taaHistory_[0], &taaHistory_[1], &taaDepth_, &ssrColor_})
        if (*t) {
            glDeleteTextures(1, t);
            *t = 0;
        }
    for (unsigned* f : {&hdrFbo_, &ssaoFbo_, &ssaoBlurFbo_, &ldrFbo_, &distortFbo_, &taaFbo_[0], &taaFbo_[1], &ssrFbo_})
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
    sceneColorCopy_ = makeTexture2D(GL_RGBA16F, width, height, GL_LINEAR);
    sceneDepthCopy_ = makeTexture2D(GL_DEPTH_COMPONENT32F, width, height, GL_NEAREST);
    for (unsigned tex : {sceneColorCopy_, sceneDepthCopy_}) {
        glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    // Ambient occlusion and bounce light at half resolution (Ultra: full); the lit pass reads
    // them filtered by screen position.
    ssaoWidth_ = quality_.ssaoFullRes ? width : (width + 1) / 2;
    ssaoHeight_ = quality_.ssaoFullRes ? height : (height + 1) / 2;
    ssaoTex_ = makeTexture2D(GL_RG16F, ssaoWidth_, ssaoHeight_, GL_NEAREST); // visibility, linear depth
    ssaoFbo_ = makeFbo(ssaoTex_);
    ssgiTex_ = makeTexture2D(GL_R11F_G11F_B10F, ssaoWidth_, ssaoHeight_, GL_NEAREST); // bounce light (SSGI)
    glNamedFramebufferTexture(ssaoFbo_, GL_COLOR_ATTACHMENT1, ssgiTex_, 0);
    {
        const GLenum buffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glNamedFramebufferDrawBuffers(ssaoFbo_, 2, buffers);
    }
    ssaoBlurTex_ = makeTexture2D(GL_RGBA16F, ssaoWidth_, ssaoHeight_, GL_LINEAR); // rgb bounce light, a visibility
    ssaoBlurFbo_ = makeFbo(ssaoBlurTex_);
    ldrTex_ = makeTexture2D(GL_RGBA8, width, height, GL_LINEAR);
    ldrFbo_ = makeFbo(ldrTex_);
    distortTex_ = makeTexture2D(GL_RG16F, width, height, GL_LINEAR);
    distortFbo_ = makeFbo(distortTex_, depthTex_);
    label(GL_TEXTURE, distortTex_, "Air distortion");
    for (int i = 0; i < 2; ++i) {
        taaHistory_[i] = makeTexture2D(GL_RGBA16F, width, height, GL_LINEAR);
        glTextureParameteri(taaHistory_[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(taaHistory_[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        taaFbo_[i] = makeFbo(taaHistory_[i]);
        label(GL_TEXTURE, taaHistory_[i], "TAA history " + std::to_string(i));
    }
    taaDepth_ = makeTexture2D(GL_DEPTH_COMPONENT32F, width, height, GL_NEAREST);
    taaValid_ = false;
    {
        const int levels = 1 + static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(width, height)))));
        glCreateTextures(GL_TEXTURE_2D, 1, &ssrColor_);
        glTextureStorage2D(ssrColor_, std::min(levels, 6), GL_RGBA16F, width, height);
        glTextureParameteri(ssrColor_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(ssrColor_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(ssrColor_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(ssrColor_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        ssrFbo_ = makeFbo(ssrColor_);
        label(GL_TEXTURE, ssrColor_, "SSR previous frame");
        ssrValid_ = false;
    }
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
    b.rainMatrix = rainMatrix_;
    const float windAngle = glm::radians(e.windDirection);
    b.wind = glm::vec4(std::cos(windAngle), std::clamp(e.wind, 0.0f, 1.0f), std::sin(windAngle), std::clamp(e.windGusts, 0.0f, 1.0f));
    for (const GrassBender& g : view.benders) {
        const int i = static_cast<int>(b.benderInfo.x);
        if (i >= kMaxGrassBenders)
            break;
        if (g.radius <= 0.0f || g.strength <= 0.0f)
            continue;
        b.benders[2 * i] = glm::vec4(g.position, g.radius);
        b.benders[2 * i + 1] = glm::vec4(g.strength, static_cast<float>(g.mode), 0.0f, 0.0f);
        b.benderInfo.x += 1.0f;
    }
    b.weather = glm::vec4(wetness_, lightning_, rainActive_ && rainTex_ ? 1.0f : 0.0f, std::clamp(e.puddles, 0.0f, 1.0f));
    b.sunColor = glm::vec4(linear(e.sunColor), quality_.shadowResolution > 0 ? 1.0f : 0.0f);
    b.skyZenith = glm::vec4(linear(e.skyZenith), e.cloudCover);
    b.skyHorizon = glm::vec4(linear(e.skyHorizon), time_);
    b.groundColor = glm::vec4(linear(e.groundColor), e.ambientIntensity);
    b.fogColor = glm::vec4(linear(e.fogColor), view.fog ? e.fogDensity : 0.0f);
    b.fogParams = glm::vec4(e.fogHeight, e.fogHeightFalloff, e.fogSunScatter, e.ssaoIntensity);
    b.groundHaze = glm::vec4(view.fog ? std::max(0.0f, e.groundHaze) : 0.0f, e.groundHazeHeight, std::max(0.1f, e.groundHazeThickness), 0.0f);
    // z: sky occlusion strength, w: world height of the top-down map's top (its depth spans 300 m down).
    b.atmosphere = glm::vec4(0.0f, 0.0f, rainActive_ && rainTex_ ? std::clamp(e.skyOcclusion, 0.0f, 1.0f) : 0.0f, rainTop_);
    if (const float a = std::clamp(e.atmosphere, 0.0f, 1.0f); a > 0.0f) {
        // Physical atmosphere: the sky table for the shaders, and the colours the rest of the
        // lighting reads (clouds, fog, ground haze) from the same model, so they agree.
        const glm::vec3 toSun = glm::normalize(glm::vec3(b.sunPos));
        AtmosphereParams air;
        air.haze = std::max(0.0f, e.atmosphereHaze);
        std::size_t h = 7;
        for (float v : {std::round(toSun.x * 2000.0f), std::round(toSun.y * 2000.0f), std::round(toSun.z * 2000.0f), air.haze})
            hashCombine(h, v);
        if (h != skyLutHash_ || !skyLut_) {
            skyLutHash_ = h;
            const SkyLut lut = buildSkyLut(toSun, air);
            if (!skyLut_) {
                glCreateTextures(GL_TEXTURE_2D, 1, &skyLut_);
                glTextureStorage2D(skyLut_, 1, GL_RGB16F, SkyLut::kWidth, SkyLut::kHeight);
                glTextureParameteri(skyLut_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTextureParameteri(skyLut_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTextureParameteri(skyLut_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTextureParameteri(skyLut_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glTextureSubImage2D(skyLut_, 0, 0, 0, SkyLut::kWidth, SkyLut::kHeight, GL_RGB, GL_FLOAT, lut.texels.data());
            skyLutZenith_ = sampleSkyLut(lut, glm::vec3(0.0f, 1.0f, 0.0f), toSun);
            glm::vec3 ring(0.0f); // the horizon all around (fog sees every direction)
            for (int i = 0; i < 16; ++i) {
                const float az = static_cast<float>(i) / 16.0f * 6.2831853f;
                ring += sampleSkyLut(lut, glm::vec3(std::cos(az), 0.05f, std::sin(az)), toSun) / 16.0f;
            }
            skyLutHorizon_ = ring;
            const glm::vec3 t = atmosphereTransmittance(toSun, air);
            skyLutSunTint_ = t / std::max({t.r, t.g, t.b, 1e-4f});
        }
        // Radiance for the authored sun (not the day cycle's dimmed one: the table has the dimming).
        const float scale = std::max(0.0f, scene.environment.sunIntensity) * std::max(0.0f, e.atmosphereBrightness);
        b.atmosphere.x = a;
        b.atmosphere.y = scale;
        const float cover = std::clamp(e.cloudCover, 0.0f, 1.0f);
        b.skyZenith = glm::vec4(glm::mix(glm::vec3(b.skyZenith), skyLutZenith_ * scale, a), b.skyZenith.w);
        b.skyHorizon = glm::vec4(glm::mix(glm::vec3(b.skyHorizon), skyLutHorizon_ * scale, a), b.skyHorizon.w);
        // Aerial perspective: the fog takes the horizon's colour (greyer under a cloud cover).
        const glm::vec3 haze = glm::mix(skyLutHorizon_ * scale, glm::vec3(glm::dot(skyLutHorizon_ * scale, glm::vec3(0.2126f, 0.7152f, 0.0722f))), cover * 0.7f);
        b.fogColor = glm::vec4(glm::mix(glm::vec3(b.fogColor), haze, a), b.fogColor.w);
        if (!e.time.enabled) // the day cycle already reddens a low sun
            b.sunColor = glm::vec4(glm::mix(glm::vec3(b.sunColor), glm::vec3(b.sunColor) * skyLutSunTint_, a), b.sunColor.w);
    }
    b.cameraPos = glm::vec4(view.cameraPosition, quality_.ssao ? 1.0f : 0.0f);
    b.view = view.view;
    b.proj = projection_; // with the TAA jitter, like the depth buffer
    b.invViewProj = glm::inverse(projection_ * view.view);
    b.probes = glm::vec4(view.capture && !captureProbes_ ? 0.0f : 1.0f, static_cast<float>(kEnvMips - 1), 0.0f, 0.0f);
    // Screen-space reflections: a perspective, lit, continuous view with a previous frame of the same size.
    // The same previous frame lights the screen-space bounce (SSGI, gathered by the SSAO pass).
    const bool historyView = !view.capture && view.shading == Shading::Lit && view.projection[3][3] == 0.0f && programs_.ssrHistory.id;
    const bool ssrView = quality_.ssr && historyView;
    const bool ssgiView = quality_.ssgi && quality_.ssao && historyView && e.bounceLight > 0.0f;
    ssrRecord_ = ssrView || ssgiView;
    ssgiActive_ = ssgiView && ssrValid_;
    ssgiViewToPrev_ = ssrViewProj_ * glm::inverse(view.view);
    b.probes.z = ssgiActive_ ? std::clamp(e.bounceLight, 0.0f, 2.0f) : 0.0f;
    // Contact shadows march through the pre-pass depth copy (taken when SSR or this needs it).
    contactActive_ = quality_.contactShadows && historyView && e.contactShadowLength > 0.0f && quality_.shadowResolution > 0;
    if (contactActive_)
        b.contact = glm::vec4(std::min(e.contactShadowLength, 2.0f), 0.25f + e.contactShadowLength, 40.0f, 0.0f);
    b.contact.w = static_cast<float>(std::clamp(quality_.skyOcclusionDirs, 3, 8));
    // Volumetric clouds: once their noise is built (renderClouds() starts it), in views with a sky.
    cloudActive_ = quality_.volumetricClouds > 0 && e.volumetricClouds > 0.5f && view.fog && view.shading != Shading::Wireframe &&
                   view.projection[3][3] == 0.0f && programs_.clouds.id && (cloudNoise_ || !view.capture);
    if (cloudActive_)
        b.clouds = glm::vec4(cloudNoise_ ? 1.0f : 0.0f, std::clamp(e.cloudBase, 100.0f, 8000.0f), std::clamp(e.cloudThickness, 50.0f, 8000.0f),
                             std::clamp(e.cloudDensity, 0.0f, 10.0f));
    b.prevViewProj = ssrViewProj_;                     // the frame now in ssrColor_
    ssrPrevViewProj_ = view.projection * view.view;    // becomes ssrViewProj_ once this frame is recorded
    if (ssrView && ssrValid_)
        b.ssr = glm::vec4(static_cast<float>(std::clamp(quality_.ssrSteps, 8, 32)), 60.0f, std::clamp(e.ssrIntensity, 0.0f, 1.0f), 0.6f);
    if (volumetricActive(view)) {
        const float nearDepth = 0.5f, farDepth = std::clamp(e.fogVolumetricRange, 8.0f, 256.0f);
        b.volumetric = glm::vec4(1.0f, nearDepth, 1.0f / std::log(farDepth / nearDepth), farDepth);
    }

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
    {
        // Soft shadows: the sun's angular radius, widened by clouds (the light comes from a
        // larger bright patch of sky), so the penumbra grows with the distance to the caster.
        const float cover = std::clamp(e.cloudCover, 0.0f, 1.0f);
        const float radius = glm::radians(std::clamp(e.sunSize, 0.0f, 20.0f) * 0.5f) * (1.0f + 3.0f * cover * cover);
        b.shadowSoft = glm::vec4(quality_.softShadows && e.sunSize > 0.0f ? (quality_.softShadowTaps > 8 ? 16.0f : 8.0f) : 0.0f, std::tan(std::min(radius, 0.6f)), 30.0f, 0.6f);
    }
    b.viewport = glm::vec4(width_, height_, 1.0f / static_cast<float>(width_), 1.0f / static_cast<float>(height_)); // render targets
    glNamedBufferSubData(envBuffer_, 0, sizeof(b), &b);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, envBuffer_);
    // A CPU copy for the environment capture: reading the buffer back would make the driver
    // move it out of video memory for good.
    envBlockCopy_.resize(sizeof(b));
    std::memcpy(envBlockCopy_.data(), &b, sizeof(b));
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
    hashCombine(h, static_cast<float>(skyLutHash_ % 1000003u));
    hashCombine(h, e.atmosphere);
    hashCombine(h, e.atmosphereBrightness);
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
    if (envBlockCopy_.size() == sizeof(captureBlock))
        std::memcpy(&captureBlock, envBlockCopy_.data(), sizeof(captureBlock));
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

bool Renderer::CullRequest::same(const CullRequest& o) const
{
    return std::memcmp(&cull, &o.cull, sizeof cull) == 0 && eye == o.eye && skipNear == o.skipNear && cascade == o.cascade &&
           minCaster == o.minCaster && occlusion == o.occlusion;
}

void Renderer::cullModels(const CullRequest& request, CullResult& out) const
{
    const auto start = std::chrono::steady_clock::now();
    const glm::mat4 t = glm::transpose(request.cull);
    const std::array<glm::vec4, 6> planes = {t[3] + t[0], t[3] - t[0], t[3] + t[1], t[3] - t[1], t[3] + t[2], t[3] - t[2]};
    auto inside = [&](const Aabb& box) {
        for (int i = 0; i < 6; ++i) {
            if (request.skipNear && i == 4)
                continue;
            const glm::vec4& p = planes[static_cast<std::size_t>(i)];
            const glm::vec3 corner(p.x >= 0.0f ? box.max.x : box.min.x, p.y >= 0.0f ? box.max.y : box.min.y,
                                   p.z >= 0.0f ? box.max.z : box.min.z);
            if (glm::dot(glm::vec3(p), corner) + p.w < 0.0f)
                return false;
        }
        return true;
    };
    const ModelGrid& grid = grid_;
    const bool shadowPass = request.cascade >= 0;
    out.candidates.clear();
    out.culledCells.clear();
    out.hiddenCells = 0;
    // 1. Whole cells first: beyond every cull distance inside, out of the frustum, or
    //    (far shadow cascades) only small casters. The models of the rest are candidates.
    for (std::size_t c = 0; c < grid.cells.size(); ++c) {
        const ModelCell& cell = grid.cells[c];
        const float cellDistance = distanceTo(request.eye, cell.bounds);
        const bool cellFar = cell.maxCull > 0.0f && cellDistance > cell.maxCull;
        const bool cellTiny = request.minCaster > 0.0f && cell.maxDiagonal < request.minCaster;
        bool hidden = false;
        if (cellFar || cellTiny || !inside(cell.bounds) || (hidden = request.occlusion && hiddenByOcclusion(cell.bounds))) {
            out.culledCells.push_back(static_cast<std::uint32_t>(c));
            out.hiddenCells += hidden;
            continue;
        }
        out.candidates.insert(out.candidates.end(), cell.models.begin(), cell.models.end());
    }
    // 2. Per model, on the job system: only the packed arrays of the grid are read
    //    (bounds, cull distance, flags), no GL.
    out.distances.resize(out.candidates.size());
    out.drawn.resize(out.candidates.size());
    JobSystem::global().parallelFor(out.candidates.size(), 512, [&](std::size_t begin, std::size_t end) {
        for (std::size_t k = begin; k < end; ++k) {
            const std::uint32_t i = out.candidates[k];
            const Aabb& bounds = grid.bounds[i];
            const float distance = distanceTo(request.eye, bounds);
            const float cull = grid.cullDistance[i];
            bool skip = (cull > 0.0f && distance > cull) || (shadowPass && !(grid.flags[i] & ModelGrid::kCastShadows)) ||
                        (request.minCaster > 0.0f && grid.diagonal[i] < request.minCaster);
            if (!skip && grid.detail[i]) {
                // Grass and weeds: fewer of them with distance, shadows only up close.
                const float keep = 1.0f - (1.0f - quality_.detailDensityFar) * glm::smoothstep(0.3f, 1.0f, distance / cull);
                skip = grid.random[i] > keep || (shadowPass && (request.cascade > 0 || distance > quality_.detailShadowDistance));
            }
            out.distances[k] = distance;
            out.drawn[k] = skip || !inside(bounds) ? 0 : request.occlusion && hiddenByOcclusion(bounds) ? 2 : 1;
        }
    });
    const_cast<Renderer*>(this)->cullNs_ +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
}

void Renderer::startCull(const CullRequest& request)
{
    for (const auto& job : cullJobs_)
        if (job->request.same(request))
            return;
    auto job = std::make_unique<CullJob>();
    job->request = request;
    CullJob* raw = job.get();
    job->done = JobSystem::global().submit([this, raw] { cullModels(raw->request, raw->result); });
    cullJobs_.push_back(std::move(job));
}

const Renderer::CullResult& Renderer::cullFor(const CullRequest& request)
{
    for (const auto& job : cullJobs_)
        if (job->request.same(request)) {
            if (job->done.valid()) {
                const auto start = std::chrono::steady_clock::now();
                // Help with queued jobs instead of sleeping while the result is not there yet.
                while (job->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                    if (!JobSystem::global().runOne())
                        std::this_thread::yield();
                job->done.get(); // rethrows a failure
                cullWaitMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            }
            return job->result;
        }
    auto job = std::make_unique<CullJob>();
    job->request = request;
    cullModels(job->request, job->result);
    cullJobs_.push_back(std::move(job));
    return cullJobs_.back()->result;
}

void Renderer::finishCullJobs()
{
    for (const auto& job : cullJobs_)
        if (job->done.valid())
            job->done.wait();
    cullJobs_.clear();
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
                drawDistance_ = distanceTo(lodEye(view), b.bounds());
                for (const auto& asset : b.material->maps)
                    if (asset)
                        keepTextureAlive(asset->texture.get());
            }
            ++culled_;
            continue;
        }
        drawDistance_ = distanceTo(lodEye(view), b.bounds());
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
    // 1.+2. Cells, then models (cullModels): this frame's result for the same view if one was
    //       started on a worker (startCull) or already made by another pass of the view.
    CullRequest request;
    request.cull = cull;
    request.eye = lodEye(view);
    request.skipNear = skipNear;
    request.cascade = shadowCascade_;
    request.minCaster = shadowMinCaster_;
    request.occlusion = occ_.testing && shadowCascade_ < 0;
    const CullResult& result = cullFor(request);
    for (std::uint32_t c : result.culledCells) {
        const ModelCell& cell = grid.cells[c];
        if (mainPass)
            for (std::uint32_t i : cell.models)
                keepAliveLods(scene.models[i], nullptr); // stays uploaded while out of view
        culled_ += static_cast<int>(cell.models.size());
    }
    occ_.hidden += result.hiddenCells;
    const std::vector<std::uint32_t>& candidates = result.candidates;
    const std::vector<float>& distances = result.distances;
    const std::vector<std::uint8_t>& drawn = result.drawn;
    // 3. Draw (in grid order, so batches and texture streaming stay deterministic).
    for (std::size_t k = 0; k < candidates.size(); ++k) {
        {
        const std::uint32_t i = candidates[k];
        const Model& model = scene.models[i];
        const Aabb& modelBounds = grid.bounds[i];
        drawDistance_ = distances[k];
        if (drawn[k] != 1) {
            if (mainPass)
                keepAliveLods(model, nullptr);
            occ_.hidden += drawn[k] == 2;
            ++culled_;
            continue;
        }
        if (skipPlants_ && model.sway > 0.0f) // the top-down map: leaves let rain drip and sky light through
            continue;
        const bool selected = static_cast<int>(scene.boxes.size() + i) == view.selectedObject;
        // Distance LOD: the simpler copy beyond lodDistance (the other one stays uploaded).
        const Model* draw = &model;
        if (model.lod || model.deferred) {
            draw = &model.lodFor(glm::distance(lodEye(view), (modelBounds.min + modelBounds.max) * 0.5f), quality_.lodScale);
            if (draw == &model && model.lod && (shadowCascade_ > 0 || (shadowPass && grid.detail[i])))
                draw = model.lod.get(); // shadows: the simple copy is enough
            if (mainPass)
                keepAliveLods(model, draw);
        }
        if (quality_.impostors && model.impostorDistance > 0.0f && !model.skeleton && !model.parts.empty() &&
            glm::distance(lodEye(view), (modelBounds.min + modelBounds.max) * 0.5f) > model.impostorDistance * quality_.lodScale)
            if (const Model* impostor = impostorFor(model))
                draw = impostor;
        // Dissolve over the last 15 % before the cull distance instead of popping out.
        const glm::vec2 fade = model.cullDistance > 0.0f && !shadowPass ? glm::vec2(model.cullDistance * 0.85f, model.cullDistance) : glm::vec2(0.0f);
        const VertexPaint* paint = draw == &model && model.paint && !depthOnly ? model.paint.get() : nullptr;
        if (!quality_.instancing || selected || draw->skeleton || draw->parts.empty() || paint) {
            glUniform1f(loc_.sway, model.sway);
            glUniform2f(loc_.fade, fade.x, fade.y);
            drawModel(*draw, grid.transforms[i], selected ? 1.0f : 0.0f, nullptr, paint);
            glUniform1f(loc_.sway, 0.0f);
            glUniform2f(loc_.fade, 0.0f, 0.0f);
            continue;
        }
        // Batch with other copies: same shared mesh list, material overrides and sway.
        std::uint64_t key = reinterpret_cast<std::uintptr_t>(&draw->parts[0]);
        for (const auto& [slot, material] : draw->overrideMaterials)
            key = key * 1099511628211ull ^ reinterpret_cast<std::uintptr_t>(material.get());
        if (model.sway > 0.0f)
            key = key * 1099511628211ull ^ static_cast<std::uint64_t>(model.sway * 1000.0f + 1.0f);
        if (fade.y > 0.0f)
            key = key * 1099511628211ull ^ static_cast<std::uint64_t>(fade.y * 16.0f + 7.0f);
        auto [it, added] = batchIndex_.emplace(key, batches_.size());
        if (added)
            batches_.push_back({draw, {}, drawDistance_, model.sway, fade});
        InstanceBatch& batch = batches_[it->second];
        batch.transforms.push_back(grid.transforms[i]);
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
        if (e.offline)
            continue; // simulated by the A-life far away
        if ((!view.editorHelpers || shadowCascade_ >= 0) && e.helper())
            continue; // map markers, sound sources, lights: editor only, never cast shadows
        if (!e.alive && e.type == "pickup")
            continue; // taken
        const float highlight = static_cast<int>(scene.boxes.size() + scene.models.size() + i) == view.selectedObject ? 1.0f : 0.0f;
        // Generous box: animations reach outside the rest pose.
        const float reach = std::max({e.size.x, e.size.y, e.size.z}) * 1.5f;
        drawDistance_ = std::max(0.0f, glm::distance(lodEye(view), e.position) - reach);
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
            e.type == "relay" || e.type == "reverb" || e.type == "ambience" || e.type == "water" || e.type == "interior" ||
            e.type == "reflection_probe" || e.type == "irradiance_volume" || e.type == "post_volume") { // marker cube; a sound zone itself is the entity size
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

void Renderer::copySceneForWater()
{
    glCopyImageSubData(hdrColor_, GL_TEXTURE_2D, 0, 0, 0, 0, sceneColorCopy_, GL_TEXTURE_2D, 0, 0, 0, 0, width_, height_, 1);
    glCopyImageSubData(depthTex_, GL_TEXTURE_2D, 0, 0, 0, 0, sceneDepthCopy_, GL_TEXTURE_2D, 0, 0, 0, 0, width_, height_, 1);
}

void Renderer::drawWater(const Scene& scene, const RenderView& view, const glm::mat4& viewProj)
{
    const std::vector<WaterBody> bodies = waterBodies(scene);
    if (bodies.empty())
        return;
    beginPass(PassTransparent);
    copySceneForWater();
    const Program& p = programs_.water;
    glUseProgram(p.id);
    glUniformMatrix4fv(p.loc("uViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindTextureUnit(0, sceneColorCopy_);
    glBindTextureUnit(1, sceneDepthCopy_);
    glBindTextureUnit(8, envMap_);
    glBindVertexArray(emptyVao_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE); // later transparent things sort against the surface
    glDisable(GL_CULL_FACE); // seen from below too
    for (const WaterBody& w : bodies) {
        const Aabb box{glm::vec3(w.surface * glm::vec4(-0.5f, 0.0f, -0.5f, 1.0f)), glm::vec3(w.surface * glm::vec4(0.5f, 0.0f, 0.5f, 1.0f))};
        Aabb b{glm::min(box.min, box.max) - glm::vec3(0.0f, 0.01f, 0.0f), glm::max(box.min, box.max) + glm::vec3(0.0f, 0.01f, 0.0f)};
        // Rotated: grow to the other two corners as well.
        for (glm::vec2 c : {glm::vec2(-0.5f, 0.5f), glm::vec2(0.5f, -0.5f)}) {
            const glm::vec3 q(w.surface * glm::vec4(c.x, 0.0f, c.y, 1.0f));
            b.min = glm::min(b.min, q);
            b.max = glm::max(b.max, q);
        }
        if (!visible(b))
            continue;
        glUniformMatrix4fv(p.loc("uWaterModel"), 1, GL_FALSE, glm::value_ptr(w.surface));
        glUniform3fv(p.loc("uWaterColor"), 1, glm::value_ptr(w.color));
        glUniform1f(p.loc("uClarity"), w.clarity);
        glUniform2fv(p.loc("uFlow"), 1, glm::value_ptr(w.flow));
        glUniform1f(p.loc("uWaves"), w.waves);
        glUniform1f(p.loc("uFoam"), w.foam);
        glUniform1f(p.loc("uReflection"), w.reflection);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        ++drawCalls_;
    }
    glEnable(GL_CULL_FACE);
    glDepthFunc(GL_LESS);
    endPass();
    (void)view;
}

void Renderer::drawUnderwater(const Scene& scene, const RenderView& view)
{
    WaterBody w;
    if (!waterAt(scene, view.cameraPosition, &w))
        return;
    beginPass(PassTransparent);
    copySceneForWater();
    const Program& p = programs_.underwater;
    glUseProgram(p.id);
    glBindTextureUnit(0, sceneColorCopy_);
    glBindTextureUnit(1, sceneDepthCopy_);
    glUniform3fv(p.loc("uWaterColor"), 1, glm::value_ptr(w.color));
    glUniform1f(p.loc("uClarity"), w.clarity);
    glUniform1f(p.loc("uSurface"), w.level);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    endPass();
}

void Renderer::renderRainMap(const Scene& scene, const RenderView& view)
{
    constexpr int kRes = 512;
    constexpr float kHalf = 48.0f;    // m around the camera
    constexpr float kHeight = 300.0f; // camera above the center
    if (!rainTex_) {
        glCreateTextures(GL_TEXTURE_2D, 1, &rainTex_);
        glTextureStorage2D(rainTex_, 1, GL_DEPTH_COMPONENT24, kRes, kRes);
        glTextureParameteri(rainTex_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(rainTex_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(rainTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(rainTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glCreateFramebuffers(1, &rainFbo_);
        glNamedFramebufferTexture(rainFbo_, GL_DEPTH_ATTACHMENT, rainTex_, 0);
        glNamedFramebufferDrawBuffer(rainFbo_, GL_NONE);
        rainCenter_ = glm::vec3(1e9f);
    }
    // Static level: redraw when the camera has moved a quarter of the area, or every
    // few seconds for moving objects (doors, props).
    const glm::vec3 eye = view.cameraPosition;
    if (glm::distance(glm::vec2(eye.x, eye.z), glm::vec2(rainCenter_.x, rainCenter_.z)) < kHalf * 0.25f &&
        frameNumber_ - rainFrame_ < 240)
        return;
    rainCenter_ = glm::vec3(std::round(eye.x / 4.0f) * 4.0f, eye.y, std::round(eye.z / 4.0f) * 4.0f);
    rainFrame_ = frameNumber_;
    const glm::vec3 top(rainCenter_.x, eye.y + kHeight * 0.5f, rainCenter_.z);
    rainTop_ = top.y;
    const glm::mat4 lightView = glm::lookAt(top, top - glm::vec3(0, 1, 0), glm::vec3(0, 0, -1));
    rainMatrix_ = glm::ortho(-kHalf, kHalf, -kHalf, kHalf, 0.0f, kHeight) * lightView;
    useMeshProgram(programs_.depth);
    glViewport(0, 0, kRes, kRes);
    glBindFramebuffer(GL_FRAMEBUFFER, rainFbo_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(rainMatrix_));
    RenderView topView = view;
    topView.drawPlayerStart = false;
    shadowCascade_ = 2;       // like a far shadow cascade: simple LODs, no grass
    shadowMinCaster_ = 0.6f;  // debris does not keep the rain off
    skipPlants_ = true;       // nor do leaves (they drip), and the sky shows through crowns
    drawGeometry(scene, topView, true, rainMatrix_, true);
    skipPlants_ = false;
    shadowCascade_ = -1;
    shadowMinCaster_ = 0.0f;
    glEnable(GL_CULL_FACE);
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
    auto distance = [&](const LocalLight& l) { return std::max(0.0f, glm::distance(l.position, lodEye(view)) - l.range); };
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
        return glm::distance(frameLights_[a].position, lodEye(view)) < glm::distance(frameLights_[b].position, lodEye(view));
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
    std::pmr::vector<GpuLight> lights(&frameArena_); // scratch: frame arena, no heap allocation
    lights.reserve(std::max<std::size_t>(1, frameLights_.size()));
    std::pmr::vector<glm::mat4> shadowMatrices(&frameArena_);
    std::pmr::vector<int> shadowLayer(frameLights_.size(), -1, &frameArena_);
    std::pmr::vector<float> shadowTexel(frameLights_.size(), 0.0f, &frameArena_);
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
    std::pmr::vector<glm::vec4> viewSpace(frameLights_.size(), &frameArena_);
    for (std::size_t i = 0; i < frameLights_.size(); ++i) {
        viewSpace[i] = glm::vec4(glm::vec3(view.view * glm::vec4(frameLights_[i].position, 1.0f)), frameLights_[i].range);
        farPlane = std::max(farPlane, -viewSpace[i].z + viewSpace[i].w);
    }
    const float sliceScale = static_cast<float>(cz) / std::log(farPlane / nearPlane);
    const float sliceBias = -std::log(nearPlane) * sliceScale;
    const int clusterCount = cx * cy * cz;
    std::pmr::vector<std::pair<std::uint32_t, std::uint32_t>> pairs(&frameArena_); // (cluster, light)
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
    std::pmr::vector<std::uint32_t> counts(static_cast<std::size_t>(clusterCount), 0, &frameArena_);
    for (const auto& pr : pairs)
        ++counts[pr.first];
    struct Header {
        glm::vec4 params; // x: slice scale, y: slice bias, z: near, w: unused
        glm::ivec4 dims;  // x, y, z cluster counts, w: light count
    };
    std::pmr::vector<std::uint32_t> grid(sizeof(Header) / 4 + static_cast<std::size_t>(clusterCount) * 2, 0, &frameArena_);
    Header header{glm::vec4(sliceScale, sliceBias, nearPlane, 0.0f), glm::ivec4(cx, cy, cz, static_cast<int>(frameLights_.size()))};
    std::memcpy(grid.data(), &header, sizeof header);
    std::uint32_t* clusters = grid.data() + sizeof(Header) / 4;
    std::uint32_t offset = 0;
    for (int c = 0; c < clusterCount; ++c) {
        clusters[c * 2] = offset;
        clusters[c * 2 + 1] = 0;
        offset += counts[static_cast<std::size_t>(c)];
    }
    std::pmr::vector<std::uint32_t> indices(std::max<std::size_t>(1, pairs.size()), 0, &frameArena_);
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

void Renderer::uploadInteriors(const Scene& scene)
{
    // Interior volumes: the sky light and the fog are dimmed inside (common.glsl interiorFactors).
    struct GpuInterior {
        glm::vec4 centerFade, halfSky, rotationFog;
    };
    std::pmr::vector<GpuInterior> data(&frameArena_);
    for (const InteriorVolume& v : interiorVolumes(scene)) {
        if (data.size() >= 64)
            break;
        const float yaw = glm::radians(v.yawDegrees);
        data.push_back({glm::vec4(v.center, std::max(v.fade, 0.01f)), glm::vec4(v.half, v.sky), glm::vec4(std::cos(yaw), std::sin(yaw), v.fog, 0.0f)});
    }
    std::pmr::vector<std::uint8_t> bytes(sizeof(glm::ivec4) + data.size() * sizeof(GpuInterior), 0, &frameArena_);
    const glm::ivec4 count(static_cast<int>(data.size()), 0, 0, 0);
    std::memcpy(bytes.data(), &count, sizeof count);
    if (!data.empty())
        std::memcpy(bytes.data() + sizeof count, data.data(), data.size() * sizeof(GpuInterior));
    glNamedBufferData(interiorBuffer_, static_cast<GLsizeiptr>(bytes.size()), bytes.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, interiorBuffer_);
}

void Renderer::syncProbes(const Scene& scene, const std::shared_ptr<const ProbeSet>& probes)
{
    (void)scene;
    if (probes == probesHeld_)
        return;
    probesHeld_ = probes;
    for (unsigned* t : {&probeCubes_, &probeIrradiance_[0], &probeIrradiance_[1], &probeIrradiance_[2]})
        if (*t) {
            glDeleteTextures(1, t);
            *t = 0;
        }
    probeReflections_ = probeVolumes_ = 0;
    struct GpuProbe {
        glm::vec4 centerFade, halfLayer, rotation;
    };
    struct GpuVolume {
        glm::vec4 centerFade, halfZ0, rotation, dims;
    };
    std::vector<std::uint8_t> reflBytes(sizeof(glm::ivec4), 0), volBytes(sizeof(glm::ivec4), 0);
    auto rotationOf = [](const ProbeShape& s, float extra, float intensity) {
        const float yaw = glm::radians(s.yawDegrees);
        return glm::vec4(std::cos(yaw), std::sin(yaw), extra, intensity);
    };
    const ProbeSet* set = probesHeld_.get();
    if (set) {
        // Reflection probes: each baked face set goes through a temporary cubemap and the GGX
        // prefilter into its layer of the array (same mips as the sky's environment map).
        std::vector<const ReflectionProbe*> probes;
        for (const ReflectionProbe& p : set->reflections)
            if (p.size > 0 && p.faces.size() == 6u * static_cast<std::size_t>(p.size * p.size) && probes.size() < 64)
                probes.push_back(&p);
        if (!probes.empty()) {
            glCreateTextures(GL_TEXTURE_CUBE_MAP_ARRAY, 1, &probeCubes_);
            glTextureStorage3D(probeCubes_, kEnvMips, GL_RGBA16F, kEnvSize, kEnvSize, static_cast<GLsizei>(probes.size() * 6));
            glTextureParameteri(probeCubes_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTextureParameteri(probeCubes_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            label(GL_TEXTURE, probeCubes_, "reflection probes");
            GLint previousFbo = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFbo);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glBindVertexArray(emptyVao_);
            glBindFramebuffer(GL_FRAMEBUFFER, captureFbo_);
            for (std::size_t i = 0; i < probes.size(); ++i) {
                const ReflectionProbe& p = *probes[i];
                unsigned raw = 0;
                glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &raw);
                const int levels = 1 + static_cast<int>(std::floor(std::log2(static_cast<float>(p.size))));
                glTextureStorage2D(raw, levels, GL_RGBA16F, p.size, p.size);
                glTextureParameteri(raw, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTextureParameteri(raw, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTextureSubImage3D(raw, 0, 0, 0, 0, p.size, p.size, 6, GL_RGB, GL_UNSIGNED_INT_5_9_9_9_REV, p.faces.data());
                glGenerateTextureMipmap(raw);
                glUseProgram(programs_.prefilter.id);
                glBindTextureUnit(0, raw);
                for (int mip = 0; mip < kEnvMips; ++mip) {
                    const int size = std::max(1, kEnvSize >> mip);
                    glViewport(0, 0, size, size);
                    glUniform1f(programs_.prefilter.loc("uRoughness"), static_cast<float>(mip) / static_cast<float>(kEnvMips - 1));
                    for (int face = 0; face < 6; ++face) {
                        glNamedFramebufferTextureLayer(captureFbo_, GL_COLOR_ATTACHMENT0, probeCubes_, mip, static_cast<int>(i) * 6 + face);
                        glUniformMatrix3fv(programs_.prefilter.loc("uFace"), 1, GL_FALSE, glm::value_ptr(cubeFace(face)));
                        glDrawArrays(GL_TRIANGLES, 0, 3);
                    }
                }
                glDeleteTextures(1, &raw);
                const GpuProbe g{glm::vec4(p.shape.center, std::max(p.shape.fade, 0.01f)), glm::vec4(p.shape.half, static_cast<float>(i)),
                                 rotationOf(p.shape, p.parallax ? 1.0f : 0.0f, p.intensity)};
                const auto* b = reinterpret_cast<const std::uint8_t*>(&g);
                reflBytes.insert(reflBytes.end(), b, b + sizeof g);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(previousFbo));
            probeReflections_ = static_cast<int>(probes.size());
        }
        // Irradiance volumes: one atlas (volumes stacked along z), SH r/g/b in three textures.
        std::vector<const IrradianceVolume*> volumes;
        glm::ivec3 atlas(1, 1, 0);
        for (const IrradianceVolume& v : set->volumes)
            if (v.sh.size() == 3u * static_cast<std::size_t>(v.dims.x * v.dims.y * v.dims.z) && volumes.size() < 64) {
                volumes.push_back(&v);
                atlas.x = std::max(atlas.x, v.dims.x);
                atlas.y = std::max(atlas.y, v.dims.y);
                atlas.z += v.dims.z;
            }
        if (!volumes.empty()) {
            std::vector<glm::vec4> channel[3];
            for (auto& c : channel)
                c.assign(static_cast<std::size_t>(atlas.x * atlas.y * atlas.z), glm::vec4(0.0f));
            int z0 = 0;
            for (const IrradianceVolume* v : volumes) {
                for (int z = 0; z < v->dims.z; ++z)
                    for (int y = 0; y < v->dims.y; ++y)
                        for (int x = 0; x < v->dims.x; ++x) {
                            const std::size_t src = 3u * static_cast<std::size_t>(x + v->dims.x * (y + v->dims.y * z));
                            const std::size_t dst = static_cast<std::size_t>(x + atlas.x * (y + atlas.y * (z0 + z)));
                            for (int c = 0; c < 3; ++c)
                                channel[c][dst] = v->sh[src + static_cast<std::size_t>(c)];
                        }
                const GpuVolume g{glm::vec4(v->shape.center, std::max(v->shape.fade, 0.01f)), glm::vec4(v->shape.half, static_cast<float>(z0)),
                                  rotationOf(v->shape, 0.0f, v->intensity), glm::vec4(glm::vec3(v->dims), 0.0f)};
                const auto* b = reinterpret_cast<const std::uint8_t*>(&g);
                volBytes.insert(volBytes.end(), b, b + sizeof g);
                z0 += v->dims.z;
            }
            for (int c = 0; c < 3; ++c) {
                glCreateTextures(GL_TEXTURE_3D, 1, &probeIrradiance_[c]);
                glTextureStorage3D(probeIrradiance_[c], 1, GL_RGBA16F, atlas.x, atlas.y, atlas.z);
                glTextureParameteri(probeIrradiance_[c], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTextureParameteri(probeIrradiance_[c], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                for (GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
                    glTextureParameteri(probeIrradiance_[c], wrap, GL_CLAMP_TO_EDGE);
                glTextureSubImage3D(probeIrradiance_[c], 0, 0, 0, 0, atlas.x, atlas.y, atlas.z, GL_RGBA, GL_FLOAT, channel[c].data());
            }
            probeVolumes_ = static_cast<int>(volumes.size());
            // Atlas size for the shader in the header's yzw.
            const glm::ivec4 header(probeVolumes_, atlas.x, atlas.y, atlas.z);
            std::memcpy(volBytes.data(), &header, sizeof header);
        }
    }
    const glm::ivec4 reflHeader(probeReflections_, 0, 0, 0);
    std::memcpy(reflBytes.data(), &reflHeader, sizeof reflHeader);
    if (probeVolumes_ == 0) {
        const glm::ivec4 none(0);
        std::memcpy(volBytes.data(), &none, sizeof none);
    }
    glNamedBufferData(probeBuffer_, static_cast<GLsizeiptr>(reflBytes.size()), reflBytes.data(), GL_DYNAMIC_DRAW);
    glNamedBufferData(probeVolumeBuffer_, static_cast<GLsizeiptr>(volBytes.size()), volBytes.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, probeBuffer_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, probeVolumeBuffer_);
    LOG_INFO("render", "baked probes: %d reflection, %d irradiance volumes", probeReflections_, probeVolumes_);
}

void Renderer::captureCube(const Scene& scene, glm::vec3 position, int size, std::vector<float>& faces, float time)
{
    // GL cube face order: +X, -X, +Y, -Y, +Z, -Z (view direction and up as the GL spec expects).
    static const glm::vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const glm::vec3 ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    GLint previousFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFbo);
    const std::size_t faceFloats = static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 3;
    for (int face = 0; face < 6; ++face) {
        RenderView v;
        v.view = glm::lookAt(position, position + dirs[face], ups[face]);
        v.projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.05f, 2000.0f);
        v.cameraPosition = position;
        v.viewportWidth = v.viewportHeight = size;
        v.time = time;
        v.deferPresent = true;
        v.capture = true;
        render(scene, v);
        pendingPresent_ = false;
        const std::size_t start = faces.size();
        faces.resize(start + faceFloats);
        glGetTextureImage(hdrColor_, 0, GL_RGB, GL_FLOAT, static_cast<GLsizei>(faceFloats * sizeof(float)), faces.data() + start);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(previousFbo));
}

bool Renderer::volumetricActive(const RenderView& view) const
{
    return quality_.volumetricFog > 0 && view.fog && !view.capture && env_ && (env_->fogDensity > 0.0f || env_->groundHaze > 0.0f) &&
           env_->fogVolumetricRange > 0.0f &&
           view.projection[3][3] == 0.0f && programs_.volumeInject.id && programs_.volumeIntegrate.id;
}

void Renderer::renderVolumetricFog(const RenderView& view)
{
    if (!volumetricActive(view)) {
        volumeHistory_ = false;
        return;
    }
    const glm::ivec3 dims = quality_.volumetricFog >= 2 ? glm::ivec3(160, 90, 64) : glm::ivec3(96, 54, 48);
    if (dims != volumeDims_) {
        for (unsigned* t : {&volumeScatter_[0], &volumeScatter_[1], &volumeIntegrated_})
            if (*t)
                glDeleteTextures(1, t);
        for (unsigned* t : {&volumeScatter_[0], &volumeScatter_[1], &volumeIntegrated_}) {
            glCreateTextures(GL_TEXTURE_3D, 1, t);
            glTextureStorage3D(*t, 1, GL_RGBA16F, dims.x, dims.y, dims.z);
            glTextureParameteri(*t, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(*t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            for (GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
                glTextureParameteri(*t, wrap, GL_CLAMP_TO_EDGE);
        }
        label(GL_TEXTURE, volumeIntegrated_, "volumetric fog");
        volumeDims_ = dims;
        volumeHistory_ = false;
    }
    const Environment& e = *env_;
    const glm::mat4 viewProj = view.projection * view.view;
    const int previous = volumeCurrent_;
    volumeCurrent_ ^= 1;
    ++volumeFrame_;
    // Slice jitter: a low-discrepancy sequence, so the history converges to the average.
    const float jitter = std::fmod(static_cast<float>(volumeFrame_) * 0.618034f, 1.0f) - 0.5f;
    const Program& inject = programs_.volumeInject;
    glUseProgram(inject.id);
    glUniform3i(inject.loc("uDims"), dims.x, dims.y, dims.z);
    glUniform1f(inject.loc("uJitter"), jitter);
    glUniformMatrix4fv(inject.loc("uInvView"), 1, GL_FALSE, glm::value_ptr(glm::inverse(view.view)));
    glUniformMatrix4fv(inject.loc("uVolumePrevViewProj"), 1, GL_FALSE, glm::value_ptr(volumePrevViewProj_));
    glUniform1i(inject.loc("uHistoryValid"), volumeHistory_ ? 1 : 0);
    glUniform1f(inject.loc("uHistoryWeight"), 0.88f);
    glUniform4f(inject.loc("uVolumeLight"), e.fogLightShafts, e.fogAnisotropy, e.fogNoise, e.fogLocalLights);
    glBindTextureUnit(0, volumeScatter_[previous]);
    glBindTextureUnit(6, shadowTex_ ? shadowTex_ : shadowDummy_);
    glBindSampler(6, 0); // comparison sampler of the texture
    glBindTextureUnit(13, localShadowTex_ ? localShadowTex_ : shadowDummy_);
    glBindImageTexture(0, volumeScatter_[volumeCurrent_], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute(static_cast<unsigned>((dims.x + 7) / 8), static_cast<unsigned>((dims.y + 7) / 8), static_cast<unsigned>(dims.z));
    glBindSampler(6, shadowRawSampler_); // back to raw depth: the mesh program (pre-pass too) declares it non-shadow
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    const Program& integrate = programs_.volumeIntegrate;
    glUseProgram(integrate.id);
    glUniform3i(integrate.loc("uDims"), dims.x, dims.y, dims.z);
    glBindTextureUnit(0, volumeScatter_[volumeCurrent_]);
    glBindImageTexture(1, volumeIntegrated_, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute(static_cast<unsigned>((dims.x + 7) / 8), static_cast<unsigned>((dims.y + 7) / 8), 1);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    glBindTextureUnit(0, 0);
    volumePrevViewProj_ = viewProj;
    volumeHistory_ = true;
}

void Renderer::renderSsao()
{
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(emptyVao_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFbo_);
    glViewport(0, 0, ssaoWidth_, ssaoHeight_);
    glUseProgram(programs_.ssao.id);
    glUniform1f(programs_.ssao.loc("uRadius"), env_ ? env_->ssaoRadius : 0.6f);
    glUniformMatrix4fv(programs_.ssao.loc("uInvProj"), 1, GL_FALSE, glm::value_ptr(glm::inverse(projection_)));
    // With TAA the slices turn every frame and the history averages them.
    glUniform1f(programs_.ssao.loc("uFrame"), taaActive_ ? static_cast<float>(taaFrame_ % 64) : 0.0f);
    glUniform1f(programs_.ssao.loc("uGi"), ssgiActive_ ? 1.0f : 0.0f);
    glUniform1f(programs_.ssao.loc("uGiRadius"), 4.0f);
    glUniformMatrix4fv(programs_.ssao.loc("uViewToPrev"), 1, GL_FALSE, glm::value_ptr(ssgiViewToPrev_));
    glBindTextureUnit(0, depthTex_);
    glBindTextureUnit(1, ssrColor_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFbo_);
    glUseProgram(programs_.blur.id);
    glBindTextureUnit(0, ssaoTex_);
    glBindTextureUnit(1, ssgiTex_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindTextureUnit(1, 0);
    glViewport(0, 0, width_, height_);
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
    ENGINE_ZONE("Renderer::render");
    finishCullJobs(); // culling results of the previous render() (and of overlays after it)
    frameArena_.reset(); // scratch arrays of the previous frame
    resetGlState();
    // The mesh program reads the sun's shadow map as raw depth (PCSS) in every pass, the
    // depth pre-pass included: a comparison sampler there is undefined behaviour.
    glBindSampler(6, shadowRawSampler_);
    // Free GPU data nothing drew since the previous render() (removed models).
    ++frameNumber_;
    releaseUnusedPaint();
    lastDrawCalls_ = drawCalls_;
    lastInstanced_ = instanced_;
    building_.drawCalls = drawCalls_;
    building_.instanced = instanced_;
    building_.meshDraws = meshDraws_;
    building_.culled = culled_;
    building_.triangles = triangles_;
    building_.uploads = uploads_;
    building_.occluded = lastOccluded_;
    building_.lights = static_cast<int>(frameLights_.size());
    building_.lightShadows = static_cast<int>(localShadows_.size());
    building_.cullMs = static_cast<double>(cullNs_.exchange(0)) / 1e6;
    building_.cullWaitMs = cullWaitMs_;
    cullWaitMs_ = 0.0;
    uploads_ = 0;
    collectQueries(); // finishes building_ into stats_ and starts the next frame
    drawCalls_ = 0;
    instanced_ = 0;
    meshDraws_ = 0;
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
    bakePendingImpostors(); // binds its own target, restores targetFbo_
    resolvedEnv_ = environmentAt(view.environment ? *view.environment : scene.environment, view.sunDirection.value_or(scene.sunDirection), &sky_);
    // Post-process volumes around the camera change the image settings (not in probe captures or the map bake).
    if (!view.capture && !view.environment)
        applyPostVolumes(postVolumes(scene), view.cameraPosition, resolvedEnv_);
    env_ = &resolvedEnv_;
    time_ = view.time;
    screenFx_ = {};
    if (!view.capture && view.shading == Shading::Lit) {
        screenFx_.damage = std::clamp(view.damage, 0.0f, 1.0f);
        screenFx_.lowHealth = std::clamp(view.lowHealth, 0.0f, 1.0f);
        screenFx_.radiation = std::clamp(view.radiation, 0.0f, 1.0f);
        screenFx_.lensRain = quality_.lensEffects ? std::clamp(view.lensRain, 0.0f, 1.0f) : 0.0f;
        if (quality_.depthOfField && view.focusDistance > 0.0f && view.projection[3][3] == 0.0f) {
            screenFx_.focus = view.focusDistance;
            screenFx_.dofBlur = std::clamp(view.dofBlur, 0.0f, 1.0f);
        }
    }
    collectDistortions(view);
    keptAlive_.clear();
    updateModelGrid(scene);
    pollOcclusion();
    view_ = view.view;
    projection_ = view.projection;
    // Temporal AA: a sub-pixel offset of the projection, new every frame (Halton 2,3, 8 steps).
    const bool taaWas = taaActive_;
    taaActive_ = quality_.taa && view.temporal && !view.capture && view.shading != Shading::Wireframe &&
                 view.projection[3][3] == 0.0f && programs_.taa.id;
    if (!taaActive_ || !taaWas)
        taaValid_ = false;
    // Render scale: the scene is drawn at a fraction of the output size and stretched in the
    // final pass (post / FXAA), for slow GPUs; menus and HUD stay sharp.
    outWidth_ = std::max(1, view.viewportWidth);
    outHeight_ = std::max(1, view.viewportHeight);
    // Dynamic resolution only follows the game's continuous view (editor viewports and captures keep theirs).
    const bool dynamic = quality_.dynamicTargetFps > 0.0f && view.temporal && !view.capture;
    if (dynamic)
        updateDynamicResolution();
    else
        dynScale_ = 1.0f;
    const float scale = view.capture ? 1.0f : std::clamp(quality_.renderScale * dynScale_, 0.25f, 1.0f);
    building_.renderScale = scale;
    ensureTargets(std::max(1, static_cast<int>(std::lround(outWidth_ * scale))), std::max(1, static_cast<int>(std::lround(outHeight_ * scale))));
    {
        // Motion blur: only a continuous perspective view knows its previous frame.
        const bool motion = quality_.motionBlur > 0.0f && view.temporal && !view.capture && view.projection[3][3] == 0.0f;
        motionValid_ = motion && motionActive_;
        motionActive_ = motion;
        motionPrevViewProj_ = motionValid_ ? motionViewProj_ : view.projection * view.view;
        motionViewProj_ = view.projection * view.view;
    }
    if (taaActive_) {
        auto halton = [](std::uint64_t i, int b) {
            float f = 1.0f, r = 0.0f;
            for (; i > 0; i /= static_cast<std::uint64_t>(b)) {
                f /= static_cast<float>(b);
                r += f * static_cast<float>(i % static_cast<std::uint64_t>(b));
            }
            return r;
        };
        const std::uint64_t n = (taaFrame_++ % 8) + 1;
        const glm::vec2 ndc((halton(n, 2) - 0.5f) * 2.0f / static_cast<float>(width_), (halton(n, 3) - 0.5f) * 2.0f / static_cast<float>(height_));
        taaJitterUV_ = ndc * 0.5f;
        projection_ = glm::translate(glm::mat4(1.0f), glm::vec3(ndc, 0.0f)) * view.projection;
        taaPrevViewProj_ = taaViewProj_;
        taaViewProj_ = view.projection * view.view;
        taaJitteredViewProj_ = projection_ * view.view;
    }

    // Occlusion depends on where the eye is, not where it looks: grow boxes by the distance moved.
    const bool occlusionTesting = occ_.valid && quality_.occlusionCulling && view.shading != Shading::Wireframe && !view.capture &&
                                  glm::distance(occ_.eye, view.cameraPosition) < 3.0f;
    occ_.inflate = glm::distance(occ_.eye, view.cameraPosition) + 0.05f;
    // Frame preparation on workers: the main view's models are culled while the main thread
    // submits the rain map, environment, probes, lights and shadows (same request as the
    // depth and main passes below, so they find this result).
    if (quality_.asyncCull) {
        CullRequest main;
        main.cull = projection_ * view.view;
        main.eye = lodEye(view);
        main.occlusion = occlusionTesting;
        startCull(main);
    }

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, jointBuffer_);
    wetness_ = std::clamp(view.wetness >= 0.0f ? view.wetness : resolvedEnv_.rain, 0.0f, 1.0f);
    lightning_ = std::clamp(view.lightning, 0.0f, 1.0f);
    // The top-down height map serves the rain and the large-scale sky occlusion.
    rainActive_ = (wetness_ > 0.0f || resolvedEnv_.rain > 0.0f || resolvedEnv_.skyOcclusion > 0.0f) && view.fog; // not in ortho views
    if (rainActive_)
        renderRainMap(scene, view); // before the environment block: it carries the map's matrix
    updateEnvironment(scene, view);
    glBindTextureUnit(31, skyLut_ ? skyLut_ : whiteTex_); // physical sky table (common.glsl uSkyLut)
    if (quality_.asyncCull && shadowTex_ && quality_.shadowResolution > 0) // the sun's cascades due this frame (renderShadows)
        for (int i = 0; i < cascadeCount_; ++i)
            if (cascadeUpdate_ & (1u << i)) {
                CullRequest c;
                c.cull = cascades_[i].matrix;
                c.eye = lodEye(view);
                c.skipNear = true;
                c.cascade = i;
                c.minCaster = quality_.shadowCache && i > 0 ? cascades_[i].radius * 0.03f : 0.0f;
                startCull(c);
            }
    updateEnvironmentMap(scene);
    collectLights(scene, view);
    uploadInteriors(scene);
    // While a bake runs, every view shows the last bounce (no re-upload between captures and the
    // editor viewport); the first bounce's captures render without probes, whatever is held.
    if (captureProbes_)
        syncProbes(scene, captureProbes_);
    else if (!view.capture)
        syncProbes(scene, scene.probes);
    if (cloudActive_) {
        beginPass(PassClouds);
        renderClouds();
        endPass();
    }
    beginPass(PassShadows);
    renderShadows(scene, view);
    renderLocalShadows(scene, view);
    endPass();
    uploadLightClusters(view);
    beginPass(PassVolumetric);
    renderVolumetricFog(view);
    endPass();

    // Depth pre-pass: SSAO needs depth, and the main pass then shades each pixel once.
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo_);
    glViewport(0, 0, width_, height_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClear(GL_DEPTH_BUFFER_BIT);
    const glm::mat4 viewProj = projection_ * view.view; // jittered with TAA
    useMeshProgram(programs_.depth);
    glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    beginPass(PassDepth);
    occ_.testing = occlusionTesting;
    drawGeometry(scene, view, true, viewProj);
    endPass();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (quality_.occlusionCulling && !view.capture)
        captureOcclusion(view);
    if ((ssrRecord_ && ssrValid_) || contactActive_) // SSR / contact shadows march through the opaque depth while the main pass writes it
        glCopyImageSubData(depthTex_, GL_TEXTURE_2D, 0, 0, 0, 0, sceneDepthCopy_, GL_TEXTURE_2D, 0, 0, 0, 0, width_, height_, 1);

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
    // The pre-pass already wrote the final depth: without depth writes the GPU keeps testing
    // early even for shaders that discard (grass, leaves), so hidden layers of foliage are no
    // longer fully lit before being thrown away.
    glDepthMask(GL_FALSE);
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    glClearBufferfv(GL_COLOR, 0, clear);
    useMeshProgram(programs_.mesh);
    glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniformMatrix3fv(loc_.envRotation, 1, GL_FALSE, glm::value_ptr(glm::mat3(1.0f)));
    glBindTextureUnit(6, shadowTex_ ? shadowTex_ : shadowDummy_);
    glBindSampler(6, shadowRawSampler_); // pbr.frag reads raw depth (PCSS)
    glBindTextureUnit(7, quality_.ssao ? ssaoBlurTex_ : whiteTex_);
    glBindTextureUnit(8, envMap_);
    glBindTextureUnit(9, brdfLut_);
    glBindTextureUnit(13, localShadowTex_ ? localShadowTex_ : shadowDummy_);
    glBindTextureUnit(14, rainTex_ ? rainTex_ : whiteTex_);
    glBindTextureUnit(15, volumetricActive(view) && volumeIntegrated_ ? volumeIntegrated_ : volumeDummy_);
    for (int i = 0; i < 3; ++i)
        glBindTextureUnit(16 + i, probeIrradiance_[i] ? probeIrradiance_[i] : probeDummy3d_);
    glBindTextureUnit(19, probeCubes_ ? probeCubes_ : probeDummyCube_);
    glBindTextureUnit(20, sceneDepthCopy_);
    glBindTextureUnit(21, ssrColor_);
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
        if (cloudActive_ && cloudTex_)
            glBindTextureUnit(16, cloudTex_); // uCloudMap
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    endPass(); // opaque + sky
    if (!wireframe)
        drawWater(scene, view, viewProj);
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

    if (!wireframe)
        drawUnderwater(scene, view);
    if (!wireframe && !view.capture) {
        beginPass(PassTransparent);
        renderDistortion(view, viewProj); // after everything that writes scene depth
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
    finishCullJobs(); // nothing may read the model grid after render() returns
    pendingPresent_ = true;
    if (!view.deferPresent)
        present();
}

void Renderer::drawTerrain(const Terrain& t, const RenderView& view)
{
    const int res = t.resolution;
    constexpr int kChunk = GpuTerrain::kChunk, kLevels = GpuTerrain::kLevels;
    const std::size_t gridCount = static_cast<std::size_t>(res) * res;
    // Grid vertices, then a copy of each lowered by kSkirt metres (skirts).
    constexpr float kSkirt = 6.0f;
    const float d = t.spacing();
    auto h = [&](int x, int z) {
        return t.heights[static_cast<std::size_t>(std::clamp(z, 0, res - 1)) * res + std::clamp(x, 0, res - 1)];
    };
    auto makeVertex = [&](int x, int z) {
        Vertex v;
        v.position = {t.origin.x + x * d, h(x, z), t.origin.y + z * d};
        v.normal = glm::normalize(glm::vec3(h(x - 1, z) - h(x + 1, z), 2.0f * d, h(x, z - 1) - h(x, z + 1)));
        v.uv = {static_cast<float>(x) / (res - 1), static_cast<float>(z) / (res - 1)};
        return v;
    };
    auto chunkBounds = [&](int cx, int cz) {
        const int quads = res - 1;
        const int x0 = cx * kChunk, z0 = cz * kChunk;
        const int x1 = std::min(quads, x0 + kChunk), z1 = std::min(quads, z0 + kChunk);
        float lo = 1e30f, hi = -1e30f;
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                const float y = t.heights[static_cast<std::size_t>(z) * res + x];
                lo = std::min(lo, y);
                hi = std::max(hi, y);
            }
        terrain_.chunks[static_cast<std::size_t>(cz) * terrain_.chunksX + cx].bounds = {
            {t.origin.x + x0 * d, lo, t.origin.y + z0 * d}, {t.origin.x + x1 * d, hi, t.origin.y + z1 * d}};
    };
    if (terrain_.meshVersion != t.meshVersion && terrain_.resolution == res && terrain_.vao && terrain_.origin == t.origin &&
        terrain_.size == t.size) {
        // Brush strokes: follow the edit list from the version on the GPU; only those tiles are re-sent.
        std::uint64_t v = terrain_.meshVersion;
        int x0 = res, z0 = res, x1 = -1, z1 = -1;
        for (const Terrain::MeshEdit& e : t.meshEdits)
            if (e.before == v) {
                x0 = std::min(x0, e.x0);
                z0 = std::min(z0, e.z0);
                x1 = std::max(x1, e.x1);
                z1 = std::max(z1, e.z1);
                v = e.after;
            }
        if (v == t.meshVersion && x1 >= x0) {
            // Normals use the neighbours: one more sample around.
            x0 = std::max(0, x0 - 1);
            z0 = std::max(0, z0 - 1);
            x1 = std::min(res - 1, x1 + 1);
            z1 = std::min(res - 1, z1 + 1);
            std::vector<Vertex> row(static_cast<std::size_t>(x1 - x0 + 1));
            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x)
                    row[static_cast<std::size_t>(x - x0)] = makeVertex(x, z);
                const std::size_t first = static_cast<std::size_t>(z) * res + x0;
                const auto bytes = static_cast<GLsizeiptr>(row.size() * sizeof(Vertex));
                glNamedBufferSubData(terrain_.vbo, static_cast<GLintptr>(first * sizeof(Vertex)), bytes, row.data());
                for (Vertex& rv : row)
                    rv.position.y -= kSkirt;
                glNamedBufferSubData(terrain_.vbo, static_cast<GLintptr>((gridCount + first) * sizeof(Vertex)), bytes, row.data());
            }
            for (int cz = std::max(0, (z0 - 1) / kChunk); cz <= std::min(terrain_.chunksX - 1, z1 / kChunk); ++cz)
                for (int cx = std::max(0, (x0 - 1) / kChunk); cx <= std::min(terrain_.chunksX - 1, x1 / kChunk); ++cx)
                    chunkBounds(cx, cz);
            terrain_.meshVersion = t.meshVersion;
            ++uploads_;
        }
    }
    if (terrain_.meshVersion != t.meshVersion || terrain_.resolution != res) {
        std::vector<Vertex> vertices(gridCount * 2);
        for (int z = 0; z < res; ++z)
            for (int x = 0; x < res; ++x) {
                Vertex& v = vertices[static_cast<std::size_t>(z) * res + x];
                v = makeVertex(x, z);
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
            for (int cx = 0; cx < chunksX; ++cx)
                chunkBounds(cx, cz);
        terrain_.resolution = res;
        terrain_.origin = t.origin;
        terrain_.size = t.size;
        terrain_.meshVersion = t.meshVersion;
    }
    if (terrain_.splatVersion != t.splatVersion || terrain_.splatResolution != t.splatResolution) {
        if (terrain_.splat && terrain_.splatResolution != t.splatResolution) {
            glDeleteTextures(1, &terrain_.splat);
            terrain_.splat = 0;
        }
        if (!terrain_.splat) {
            glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &terrain_.splat);
            glTextureStorage3D(terrain_.splat, 1, GL_RGBA8, t.splatResolution, t.splatResolution, 2);
            glTextureParameteri(terrain_.splat, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(terrain_.splat, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(terrain_.splat, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(terrain_.splat, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        // Interleaved 8 bytes per pixel -> two RGBA planes (layers 0-3, 4-7).
        const std::size_t pixels = static_cast<std::size_t>(t.splatResolution) * t.splatResolution;
        terrain_.splatPlanes.resize(pixels * 8);
        if (t.splat.size() >= pixels * Terrain::kLayers) {
            std::uint8_t* a = terrain_.splatPlanes.data();
            std::uint8_t* b = a + pixels * 4;
            for (std::size_t i = 0; i < pixels; ++i) {
                std::memcpy(a + i * 4, &t.splat[i * Terrain::kLayers], 4);
                std::memcpy(b + i * 4, &t.splat[i * Terrain::kLayers + 4], 4);
            }
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTextureSubImage3D(terrain_.splat, 0, 0, 0, 0, t.splatResolution, t.splatResolution, 2, GL_RGBA, GL_UNSIGNED_BYTE,
                            terrain_.splatPlanes.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        terrain_.splatResolution = t.splatResolution;
        terrain_.splatVersion = t.splatVersion;
    }
    const Aabb bounds = t.bounds();
    drawDistance_ = distanceTo(lodEye(view), bounds);
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
        constexpr int kL = Terrain::kLayers;
        float tiling[kL], roughness[kL], normalStrength[kL];
        // Default tints (no material): dirt, grass, gravel, grey, then four more muted ones.
        glm::vec3 colors[kL] = {{0.45f, 0.42f, 0.36f}, {0.38f, 0.42f, 0.25f}, {0.32f, 0.3f, 0.27f}, {0.5f, 0.5f, 0.5f},
                                {0.4f, 0.33f, 0.25f}, {0.3f, 0.36f, 0.28f}, {0.42f, 0.4f, 0.38f}, {0.25f, 0.24f, 0.22f}};
        for (int i = 0; i < kL; ++i) {
            const TerrainLayer& layer = t.layers[static_cast<std::size_t>(i)];
            tiling[i] = layer.tiling;
            roughness[i] = 0.9f;
            normalStrength[i] = 1.0f;
            if (const Material* m = layer.material.get()) {
                colors[i] = m->color;
                roughness[i] = m->roughness;
                normalStrength[i] = m->normalStrength;
            }
        }
        const int maps = updateTerrainLayers(t);
        glBindTextureUnit(10, terrain_.albedoArray);
        glBindTextureUnit(11, terrain_.normalArray);
        glBindTextureUnit(12, terrain_.splat);
        glUniform1fv(mesh.loc("uLayerTiling"), kL, tiling);
        glUniform3fv(mesh.loc("uLayerColor"), kL, glm::value_ptr(colors[0]));
        glUniform1i(mesh.loc("uLayerMaps"), maps);
        glUniform1fv(mesh.loc("uLayerRoughness"), kL, roughness);
        glUniform1fv(mesh.loc("uLayerNormalStrength"), kL, normalStrength);
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
        const float d = distanceTo(lodEye(view), chunk.bounds);
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

int Renderer::updateTerrainLayers(const Terrain& t)
{
    // Every layer's albedo and normal map is drawn (resampled, with its mip chain) into one
    // slice of a texture array, so the shader samples 8 layers through 2 units. A slice is
    // redrawn only when its source texture changes (material edit, texture streamed in).
    constexpr int kSize = GpuTerrain::kLayerMapSize, kL = Terrain::kLayers;
    const int levels = static_cast<int>(std::log2(static_cast<float>(kSize))) + 1;
    if (!terrain_.albedoArray) {
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &terrain_.albedoArray);
        glTextureStorage3D(terrain_.albedoArray, levels, GL_SRGB8_ALPHA8, kSize, kSize, kL);
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &terrain_.normalArray);
        glTextureStorage3D(terrain_.normalArray, levels, GL_RGBA8, kSize, kSize, kL);
        for (unsigned tex : {terrain_.albedoArray, terrain_.normalArray}) {
            glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTextureParameterf(tex, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
        }
        glCreateFramebuffers(1, &terrain_.copyFbo);
    }
    int maps = 0;
    bool changed[2] = {false, false};
    struct Copy {
        unsigned source, target;
        int slice;
    };
    std::vector<Copy> copies;
    for (int i = 0; i < kL; ++i) {
        const Material* m = t.layers[static_cast<std::size_t>(i)].material.get();
        auto handleOf = [&](MapKind kind) {
            const auto& asset = m ? m->map(kind) : nullptr;
            const Texture* tex = asset ? asset->texture.get() : nullptr;
            return tex && tex->valid() ? texture(*tex) : 0u;
        };
        const unsigned albedo = handleOf(MapKind::Albedo), normal = handleOf(MapKind::Normal);
        maps |= (albedo ? 1 << i : 0) | (normal ? 256 << i : 0);
        if (albedo && albedo != terrain_.albedoSource[i]) {
            copies.push_back({albedo, terrain_.albedoArray, i});
            changed[0] = true;
        }
        if (normal && normal != terrain_.normalSource[i]) {
            copies.push_back({normal, terrain_.normalArray, i});
            changed[1] = true;
        }
        terrain_.albedoSource[i] = albedo;
        terrain_.normalSource[i] = normal;
    }
    if (copies.empty())
        return maps;
    // Draw state of the pass in progress, restored below.
    GLint fbo = 0, program = 0, vao = 0, viewport[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_VIEWPORT, viewport);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE),
                    srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, terrain_.copyFbo);
    glViewport(0, 0, kSize, kSize);
    glUseProgram(programs_.copy.id);
    glBindVertexArray(emptyVao_);
    for (const Copy& c : copies) {
        // sRGB albedo: decoded when sampled, encoded again when written.
        if (c.target == terrain_.albedoArray)
            glEnable(GL_FRAMEBUFFER_SRGB);
        else
            glDisable(GL_FRAMEBUFFER_SRGB);
        glNamedFramebufferTextureLayer(terrain_.copyFbo, GL_COLOR_ATTACHMENT0, c.target, 0, c.slice);
        glBindTextureUnit(0, c.source);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glNamedFramebufferTextureLayer(terrain_.copyFbo, GL_COLOR_ATTACHMENT0, 0, 0, 0);
    if (changed[0])
        glGenerateTextureMipmap(terrain_.albedoArray);
    if (changed[1])
        glGenerateTextureMipmap(terrain_.normalArray);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(fbo));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glUseProgram(static_cast<GLuint>(program));
    glBindVertexArray(static_cast<GLuint>(vao));
    (depth ? glEnable : glDisable)(GL_DEPTH_TEST);
    (blend ? glEnable : glDisable)(GL_BLEND);
    (cull ? glEnable : glDisable)(GL_CULL_FACE);
    (srgb ? glEnable : glDisable)(GL_FRAMEBUFFER_SRGB);
    glDepthMask(depthMask);
    uploads_ += static_cast<int>(copies.size());
    return maps;
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
        if ((shadowCascade_ >= 0 && !type.castShadows) || (skipPlants_ && type.sway > 0.0f))
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
        // Farthest: the baked impostor (two crossed quads), once it exists.
        if (quality_.impostors && type.impostorDistance > 0.0f && type.model && !type.model->parts.empty() && levelCount < 8)
            if (const Model* impostor = impostorFor(*type.model)) {
                levels[levelCount] = impostor;
                from[levelCount] = std::max(type.impostorDistance * quality_.lodScale, from[levelCount - 1]);
                ++levelCount;
            }
        const float farDensity = detail ? std::min(type.farDensity, quality_.detailDensityFar) : type.farDensity;
        const glm::vec2 fade = type.cullDistance > 0.0f && type.fade > 0.0f && shadowCascade_ < 0
                                   ? glm::vec2(type.cullDistance * (1.0f - type.fade), type.cullDistance)
                                   : glm::vec2(0.0f);
        for (const FoliageCell& cell : c.types[ti]) {
            const float cellDistance = distanceTo(lodEye(view), cell.bounds);
            if ((type.cullDistance > 0.0f && cellDistance > type.cullDistance) || cellDistance > shadowReach ||
                !visible(cell.bounds) || occluded(cell.bounds)) {
                culled_ += static_cast<int>(cell.instances.size());
                continue;
            }
            for (std::uint32_t i : cell.instances) {
                const float d = glm::distance(lodEye(view), t.foliage[i].position);
                if ((type.cullDistance > 0.0f && d > type.cullDistance + (fade.y > 0.0f ? 0.0f : 2.0f)) || d > shadowReach)
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
                std::uint64_t key = reinterpret_cast<std::uintptr_t>(&draw->parts[0]);
                if (type.sway > 0.0f)
                    key = key * 1099511628211ull ^ static_cast<std::uint64_t>(type.sway * 1000.0f + 1.0f);
                if (fade.y > 0.0f)
                    key = key * 1099511628211ull ^ static_cast<std::uint64_t>(fade.x * 16.0f + 3.0f) * 31u ^ static_cast<std::uint64_t>(fade.y * 16.0f);
                auto [it, added] = batchIndex_.emplace(key, batches_.size());
                if (added)
                    batches_.push_back({draw, {}, cellDistance, type.sway, fade});
                InstanceBatch& batch = batches_[it->second];
                batch.distance = std::min(batch.distance, cellDistance);
                batch.transforms.push_back(c.transforms[i]);
            }
        }
    }
}

namespace {
// What applySurface() reads from a material or a mesh part: draws whose surfaces
// match share one multi-draw call. Depth-only passes only care about masked albedo.
struct SurfaceKey {
    const void* a = nullptr;   // material, or the part's base texture
    const void* b = nullptr;   // the part's other textures (hashed)
    std::uint64_t params = 0;  // the part's factors (hashed)
    auto operator<=>(const SurfaceKey&) const = default;
};
std::uint64_t hashFloats(std::initializer_list<float> values)
{
    std::uint64_t h = 1469598103934665603ull;
    for (float f : values) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof bits);
        h = (h ^ bits) * 1099511628211ull;
    }
    return h;
}
SurfaceKey surfaceKey(const Material* material, const MeshPart& part, bool depthOnly)
{
    const SurfaceAlpha alpha = surfaceAlpha(material, &part);
    if (depthOnly && alpha.mode != AlphaMode::Mask)
        return {};
    if (material)
        return {material, nullptr, depthOnly ? 1u : 0u};
    SurfaceKey k;
    k.a = part.texture.get();
    if (depthOnly) {
        k.params = hashFloats({alpha.cutoff}) | 1u;
        return k;
    }
    const std::uintptr_t others = reinterpret_cast<std::uintptr_t>(part.normalTexture.get()) * 31u ^
                                  reinterpret_cast<std::uintptr_t>(part.metalRoughTexture.get()) * 131u ^
                                  reinterpret_cast<std::uintptr_t>(part.occlusionTexture.get()) * 1031u ^
                                  reinterpret_cast<std::uintptr_t>(part.emissiveTexture.get()) * 10007u;
    k.b = reinterpret_cast<const void*>(others);
    k.params = hashFloats({part.color.r, part.color.g, part.color.b, part.roughness, part.metallic, part.normalScale, part.emissive.r,
                           part.emissive.g, part.emissive.b, static_cast<float>(alpha.mode), alpha.cutoff, alpha.opacity});
    return k;
}

struct DrawElementsIndirectCommand {
    std::uint32_t count, instanceCount, firstIndex;
    std::int32_t baseVertex;
    std::uint32_t baseInstance;
};
} // namespace

void Renderer::flushInstances()
{
    // All copies of this pass go to one transform buffer; then every mesh part of every
    // batch becomes an indirect draw command, sorted so that parts with the same surface
    // (material, wind sway, fade, culling) are drawn by one glMultiDrawElementsIndirect.
    instanceData_.clear();
    struct Item {
        const MeshPart* part;
        const Material* material;
        const GpuMesh* mesh;
        std::uint32_t base, count;
        float distance, sway;
        glm::vec2 fade;
        bool twoSided;
        SurfaceKey surface;
    };
    std::vector<Item> items;
    const bool cullOn = glIsEnabled(GL_CULL_FACE);
    for (const InstanceBatch& b : batches_) {
        const auto base = static_cast<std::uint32_t>(instanceData_.size());
        const auto count = static_cast<std::uint32_t>(b.transforms.size());
        instanceData_.insert(instanceData_.end(), b.transforms.begin(), b.transforms.end());
        if (collectTransparent_ && count > 1)
            instanced_ += static_cast<int>(count);
        for (const MeshPart& part : b.model->parts) {
            const GpuMesh& mesh = upload(part);
            const Material* material = b.model->materialFor(part);
            const SurfaceAlpha alpha = surfaceAlpha(material, &part);
            if (alpha.mode == AlphaMode::Blend) {
                if (depthOnly_)
                    continue; // glass neither occludes nor casts shadows
                if (collectTransparent_) {
                    for (std::uint32_t i = 0; i < count; ++i)
                        transparent_.push_back({&part, material, nullptr, b.transforms[i], b.distance, 0.0f});
                    continue;
                }
            }
            items.push_back({&part, material, &mesh, base, count, b.distance, b.sway, b.fade, alpha.doubleSided && cullOn,
                             surfaceKey(material, part, depthOnly_)});
        }
    }
    batches_.clear();
    batchIndex_.clear();
    if (items.empty())
        return;
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
    ensureIota(instanceData_.size());

    std::sort(items.begin(), items.end(), [](const Item& x, const Item& y) {
        return std::tie(x.sway, x.fade.x, x.fade.y, x.twoSided, x.surface) < std::tie(y.sway, y.fade.x, y.fade.y, y.twoSided, y.surface);
    });
    std::vector<DrawElementsIndirectCommand> commands;
    commands.reserve(items.size());
    for (const Item& it : items)
        commands.push_back({static_cast<std::uint32_t>(it.mesh->indexCount), it.count, static_cast<std::uint32_t>(it.mesh->firstIndex),
                            it.mesh->baseVertex, it.base});
    const std::size_t commandBytes = commands.size() * sizeof(DrawElementsIndirectCommand);
    if (!indirectBuffer_)
        glCreateBuffers(1, &indirectBuffer_);
    if (commandBytes > indirectCapacity_) {
        indirectCapacity_ = std::max(commandBytes, indirectCapacity_ * 2);
        glNamedBufferData(indirectBuffer_, static_cast<GLsizeiptr>(indirectCapacity_), nullptr, GL_STREAM_DRAW);
    } else {
        glInvalidateBufferData(indirectBuffer_);
    }
    glNamedBufferSubData(indirectBuffer_, 0, static_cast<GLsizeiptr>(commandBytes), commands.data());
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer_);

    glUniform1f(loc_.highlight, 0.0f);
    glUniform1i(loc_.skinned, 0);
    glUniform1i(loc_.instanced, 1);
    glUniform1i(loc_.instanceBase, 0);
    glBindVertexArray(poolVao_);
    for (std::size_t first = 0; first < items.size();) {
        std::size_t last = first + 1;
        const Item& f = items[first];
        float distance = f.distance;
        long long tris = static_cast<long long>(f.mesh->indexCount / 3) * f.count;
        while (last < items.size() && items[last].sway == f.sway && items[last].fade == f.fade && items[last].twoSided == f.twoSided &&
               items[last].surface == f.surface) {
            distance = std::min(distance, items[last].distance);
            tris += static_cast<long long>(items[last].mesh->indexCount / 3) * items[last].count;
            ++last;
        }
        drawDistance_ = distance; // texture streaming: the nearest copy
        glUniform1f(loc_.sway, f.sway);
        glUniform2f(loc_.fade, f.fade.x, f.fade.y);
        applySurface(f.material, f.part, f.part->color, false);
        if (f.twoSided)
            glDisable(GL_CULL_FACE);
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                    reinterpret_cast<const void*>(first * sizeof(DrawElementsIndirectCommand)),
                                    static_cast<GLsizei>(last - first), 0);
        if (f.twoSided)
            glEnable(GL_CULL_FACE);
        ++drawCalls_;
        meshDraws_ += static_cast<int>(last - first);
        triangles_ += tris;
        first = last;
    }
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glUniform1i(loc_.instanced, 0);
    glUniform1f(loc_.sway, 0.0f);
    glUniform2f(loc_.fade, 0.0f, 0.0f);
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
                glDrawElementsBaseVertex(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT,
                                         reinterpret_cast<const void*>(static_cast<std::uintptr_t>(mesh.firstIndex) * sizeof(std::uint32_t)),
                                         mesh.baseVertex);
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

std::vector<float> Renderer::readSceneDepth(int& width, int& height) const
{
    width = width_;
    height = height_;
    std::vector<float> depth(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_));
    if (depthTex_ && !depth.empty())
        glGetTextureImage(depthTex_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, static_cast<GLsizei>(depth.size() * sizeof(float)), depth.data());
    return depth;
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
    std::pmr::vector<const SpriteBatch*> order(&frameArena_);
    order.reserve(batches.size());
    std::size_t total = 0;
    for (const SpriteBatch& b : batches)
        if (!b.instances.empty() && b.blend != SpriteBlend::Distort) { // distortion: renderDistortion()
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
    std::pmr::vector<SpriteInstance> all(&frameArena_);
    all.reserve(total);
    std::pmr::vector<int> bases(&frameArena_);
    bases.reserve(order.size());
    for (const SpriteBatch* b : order) {
        bases.push_back(static_cast<int>(all.size()));
        all.insert(all.end(), b->instances.begin(), b->instances.end());
    }
    glNamedBufferSubData(spriteBuffer_, 0, static_cast<GLsizeiptr>(all.size() * sizeof(SpriteInstance)), all.data());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, spriteBuffer_);

    // Soft particles read the opaque depth: a copy, the depth buffer itself stays bound for the test.
    bool soft = false;
    if (!overlay)
        for (const SpriteBatch* b : order)
            soft = soft || (b->soft > 0.0f && !b->decal);
    if (soft) {
        glCopyImageSubData(depthTex_, GL_TEXTURE_2D, 0, 0, 0, 0, sceneDepthCopy_, GL_TEXTURE_2D, 0, 0, 0, 0, width_, height_, 1);
        glBindTextureUnit(1, sceneDepthCopy_);
    }
    if (!overlay) { // sun shadow in the air (raw depth like pbr.frag)
        glBindTextureUnit(6, shadowTex_ ? shadowTex_ : shadowDummy_);
        glBindSampler(6, shadowRawSampler_);
    }

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
        unsigned lightMap = 0;
        if (tex && b.lightMap && b.lightMap->texture && b.lightMap->texture->valid())
            lightMap = texture(*b.lightMap->texture);
        glBindTextureUnit(2, lightMap ? lightMap : whiteTex_);
        glUniform1i(p.loc("uSpriteSixWay"), lightMap ? 1 : 0);
        glUniform1f(p.loc("uSpriteSoft"), soft && b.soft > 0.0f && !b.decal ? 1.0f / b.soft : 0.0f);
        glUniform1f(p.loc("uSpriteNearFade"), overlay ? 0.0f : b.nearFade);
        glUniform1f(p.loc("uSpriteScatter"), b.scatter);
        glUniform2i(p.loc("uSpriteGrid"), std::max(1, b.columns), std::max(1, b.rows));
        glUniform1i(p.loc("uSpriteLit"), b.lit && !overlay && shading != Shading::Unlit ? 1 : 0);
        glUniform1i(p.loc("uSpriteNoFog"), overlay ? 1 : 0);
        glUniform1i(p.loc("uSpriteDecal"), b.decal ? 1 : 0);
        glUniform1i(p.loc("uSpriteSheltered"), b.sheltered && !overlay ? 1 : 0);
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
    if (taaActive_ || motionActive_ || screenFx_.focus > 0.0f) // TAA, motion blur and depth of field read the scene depth, which the weapon layer clears
        glCopyImageSubData(depthTex_, GL_TEXTURE_2D, 0, 0, 0, 0, taaDepth_, GL_TEXTURE_2D, 0, 0, 0, 0, width_, height_, 1);
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo_);
    glViewport(0, 0, width_, height_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glClear(GL_DEPTH_BUFFER_BIT);
    useMeshProgram(programs_.mesh);
    const glm::mat4 overlayProj =
        taaActive_ ? glm::translate(glm::mat4(1.0f), glm::vec3(taaJitterUV_ * 2.0f, 0.0f)) * pass.projection : pass.projection;
    glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(overlayProj));
    glUniform1i(loc_.overlay, 1);
    const glm::mat3 viewToWorld = glm::transpose(glm::mat3(view.view));
    glUniformMatrix3fv(loc_.envRotation, 1, GL_FALSE, glm::value_ptr(viewToWorld));
    glUniform3fv(loc_.overlaySun, 1, glm::value_ptr(glm::normalize(pass.sunDirectionViewSpace)));
    glUniform1f(loc_.flash, 0.0f);
    glUniform1i(loc_.skinned, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, jointBuffer_);
    glBindTextureUnit(6, shadowDummy_);
    glBindSampler(6, shadowRawSampler_);
    glBindTextureUnit(7, whiteTex_);
    glBindTextureUnit(8, envMap_);
    glBindTextureUnit(9, brdfLut_);
    if (pass.model)
        drawModel(*pass.model, pass.modelTransform, 0.0f, pass.animator);
    for (const OverlayBox& b : pass.boxes)
        drawCube(glm::scale(glm::translate(glm::mat4(1.0f), b.center), b.size), b.color, 0.0f, b.emissive);
    glUniform1i(loc_.overlay, 0);
    if (!pass.sprites.empty())
        drawSprites(pass.sprites, overlayProj, view.shading, true);
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
        glBindTextureUnit(0, i == 0 ? sceneTex_ : bloomTex_[i - 1]);
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
    // Anomaly warps become camera-facing quads of their radius, drawn with the distortion
    // particles (depth tested, so walls in front hide them). color: g strength, b < 0 lens.
    distortionSprites_.clear();
    overlayDrawn_ = false;
    const glm::vec3 right(view.view[0][0], view.view[1][0], view.view[2][0]);
    const glm::vec3 up(view.view[0][1], view.view[1][1], view.view[2][1]);
    for (const ScreenDistortion& d : view.distortions) {
        if (d.strength <= 0.0f || d.radius <= 0.0f || distortionSprites_.size() >= 64)
            continue;
        SpriteInstance s;
        s.position = d.position;
        s.axisX = right * d.radius;
        s.axisY = up * d.radius;
        s.color = glm::vec4(0.0f, d.strength, d.lens ? -1.0f : 0.0f, 1.0f);
        distortionSprites_.push_back(s);
    }
}

void Renderer::renderDistortion(const RenderView& view, const glm::mat4& viewProj)
{
    distortDrawn_ = false;
    std::pmr::vector<const SpriteBatch*> batches(&frameArena_);
    std::size_t total = distortionSprites_.size();
    for (const SpriteBatch& b : view.sprites)
        if (b.blend == SpriteBlend::Distort && !b.instances.empty()) {
            batches.push_back(&b);
            total += b.instances.size();
        }
    if (total == 0 || !distortFbo_ || !programs_.distort.id)
        return;
    if (!distortBuffer_ || total > distortCapacity_) {
        if (distortBuffer_)
            glDeleteBuffers(1, &distortBuffer_);
        distortCapacity_ = std::max<std::size_t>(total * 3 / 2, 256);
        glCreateBuffers(1, &distortBuffer_);
        glNamedBufferData(distortBuffer_, static_cast<GLsizeiptr>(distortCapacity_ * sizeof(SpriteInstance)), nullptr, GL_DYNAMIC_DRAW);
    }
    std::pmr::vector<SpriteInstance> all(&frameArena_);
    all.reserve(total);
    all.insert(all.end(), distortionSprites_.begin(), distortionSprites_.end());
    std::pmr::vector<int> bases(&frameArena_);
    for (const SpriteBatch* b : batches) {
        bases.push_back(static_cast<int>(all.size()));
        all.insert(all.end(), b->instances.begin(), b->instances.end());
    }
    glNamedBufferSubData(distortBuffer_, 0, static_cast<GLsizeiptr>(all.size() * sizeof(SpriteInstance)), all.data());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, distortBuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, distortFbo_);
    glViewport(0, 0, width_, height_);
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 0, zero);
    const Program& p = programs_.distort;
    glUseProgram(p.id);
    glUniformMatrix4fv(p.loc("uSpriteViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform1f(p.loc("uDecalLift"), 0.0f);
    glBindVertexArray(emptyVao_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE); // offsets add up
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    auto draw = [&](unsigned tex, int columns, int rows, int base, std::size_t count) {
        glBindTextureUnit(0, tex ? tex : whiteTex_);
        glUniform1i(p.loc("uSpriteHasTexture"), tex ? 1 : 0);
        glUniform2i(p.loc("uSpriteGrid"), std::max(1, columns), std::max(1, rows));
        glUniform1i(p.loc("uSpriteBase"), base);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(count));
        ++drawCalls_;
    };
    if (!distortionSprites_.empty())
        draw(0, 1, 1, 0, distortionSprites_.size());
    for (std::size_t i = 0; i < batches.size(); ++i) {
        const SpriteBatch& b = *batches[i];
        const unsigned tex = b.texture && b.texture->texture && b.texture->texture->valid() ? texture(*b.texture->texture) : 0u;
        draw(tex, b.columns, b.rows, bases[i], b.instances.size());
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo_);
    distortDrawn_ = true;
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
    glBindTextureUnit(0, sceneTex_);
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
    engine::VfsStream in(e.lut);
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
    sceneTex_ = hdrColor_;
    if (taaActive_)
        resolveTaa(); // sceneTex_ = the anti-aliased frame
    if (ssrRecord_)
        recordSsrColor();
    if (quality_.bloom)
        renderBloom();
    const Environment defaults;
    const Environment& e = env_ ? *env_ : defaults;
    glBindVertexArray(emptyVao_);
    // FXAA breaks one-pixel lines into dots; with TAA on it covers the views TAA can't.
    const bool fxaa = (quality_.fxaa || quality_.taa) && !taaActive_ && !wireframe_;
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
    glUniform1i(post.loc("uTonemap"), std::clamp(e.tonemap, 0, 2));
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
    glUniform1f(post.loc("uGrain"), quality_.filmGrain ? e.grain : 0.0f);
    glUniform1f(post.loc("uChromatic"), quality_.lensEffects && !wireframe_ ? std::clamp(e.chromaticAberration, 0.0f, 1.0f) : 0.0f);
    glUniform1f(post.loc("uLensDirt"), quality_.lensEffects && quality_.bloom ? std::clamp(e.lensDirt, 0.0f, 1.0f) : 0.0f);
    glUniform4f(post.loc("uScreenFx"), screenFx_.damage, screenFx_.lowHealth, screenFx_.radiation, screenFx_.lensRain);
    // Depth of field: focus (m), blur, and the projection terms that turn depth into distance.
    glUniform4f(post.loc("uDof"), screenFx_.focus, screenFx_.dofBlur * (static_cast<float>(width_) / 1920.0f) * 9.0f, projection_[2][2], projection_[3][2]);
    glUniform1f(post.loc("uTime"), time_);
    glUniform1i(post.loc("uLumaAlpha"), fxaa ? 1 : 0);
    glUniform1i(post.loc("uDistortOn"), distortDrawn_ ? 1 : 0);
    glUniform1i(post.loc("uDistortMask"), overlayDrawn_ ? 1 : 0);
    glBindTextureUnit(5, distortDrawn_ ? distortTex_ : whiteTex_);
    {
        const float scale = static_cast<float>(width_) / static_cast<float>(std::max(outWidth_, 1));
        const float sharpen = scale < 0.99f ? std::clamp(quality_.upscaleSharpness, 0.0f, 1.0f) * std::clamp((1.0f - scale) * 2.5f, 0.0f, 1.0f) : 0.0f;
        glUniform1f(post.loc("uSharpen"), sharpen);
    }
    glUniform1f(post.loc("uMotionBlur"), motionValid_ ? std::clamp(quality_.motionBlur, 0.0f, 1.0f) : 0.0f);
    if (motionValid_) {
        const glm::mat4 reproject = motionPrevViewProj_ * glm::inverse(projection_ * view_);
        glUniformMatrix4fv(post.loc("uMotionReproject"), 1, GL_FALSE, glm::value_ptr(reproject));
    }
    glBindSampler(6, 0);
    glBindTextureUnit(6, overlayDrawn_ && (taaActive_ || motionActive_ || screenFx_.focus > 0.0f) ? taaDepth_ : depthTex_);
    glBindTextureUnit(0, sceneTex_);
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
#ifdef TRACY_ENABLE
    if (tracyGpuOwner() == this)
        TracyGpuCollect;
#endif
    // Leave depth of the scene in the target too, for tools that draw on top.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(targetFbo_));
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
    endPass();
}

void Renderer::recordSsrColor()
{
    glBindFramebuffer(GL_FRAMEBUFFER, ssrFbo_);
    glViewport(0, 0, width_, height_);
    glBindVertexArray(emptyVao_);
    const Program& p = programs_.ssrHistory;
    glUseProgram(p.id);
    glUniform1i(p.loc("uOverlay"), overlayDrawn_ ? 1 : 0);
    glBindTextureUnit(0, sceneTex_);
    glBindTextureUnit(1, depthTex_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glGenerateTextureMipmap(ssrColor_); // blurrier levels for rough surfaces
    ssrViewProj_ = ssrPrevViewProj_;
    ssrValid_ = true;
}

void Renderer::resolveTaa()
{
    const int next = 1 - taaIndex_;
    glBindFramebuffer(GL_FRAMEBUFFER, taaFbo_[next]);
    glViewport(0, 0, width_, height_);
    glBindVertexArray(emptyVao_);
    const Program& taa = programs_.taa;
    glUseProgram(taa.id);
    const glm::mat4 reproject = taaPrevViewProj_ * glm::inverse(taaJitteredViewProj_);
    glUniformMatrix4fv(taa.loc("uReproject"), 1, GL_FALSE, glm::value_ptr(reproject));
    glUniform2fv(taa.loc("uJitterUV"), 1, glm::value_ptr(taaJitterUV_));
    glUniform1i(taa.loc("uHistoryValid"), taaValid_ ? 1 : 0);
    glUniform1i(taa.loc("uOverlay"), overlayDrawn_ ? 1 : 0);
    glUniform1f(taa.loc("uFeedback"), 0.9f);
    glUniform1f(taa.loc("uSharpen"), 0.5f);
    glBindTextureUnit(0, hdrColor_);
    glBindTextureUnit(1, taaHistory_[taaIndex_]);
    glBindTextureUnit(2, overlayDrawn_ ? taaDepth_ : depthTex_);
    glBindTextureUnit(3, depthTex_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    taaIndex_ = next;
    taaValid_ = true;
    sceneTex_ = taaHistory_[next];
}

const char* Renderer::passName(int pass)
{
    static const char* names[PassCount] = {"Shadows", "Volumetric fog", "Depth pre-pass", "SSAO", "Opaque + sky", "Transparent", "Post (TAA, bloom, tone, FXAA)",
                                           "First person", "Clouds"};
    return pass >= 0 && pass < PassCount ? names[pass] : "?";
}

#ifdef TRACY_ENABLE
namespace {
// The render pass running now, as Tracy CPU and GPU zones (passes don't nest).
std::optional<tracy::ScopedZone>& tracyPassZone()
{
    static std::optional<tracy::ScopedZone> zone;
    return zone;
}
std::optional<tracy::GpuCtxScope>& tracyGpuPassZone()
{
    static std::optional<tracy::GpuCtxScope> zone;
    return zone;
}
} // namespace

const Renderer*& Renderer::tracyGpuOwner()
{
    static const Renderer* owner = nullptr;
    return owner;
}
#endif

void Renderer::beginPass(int pass)
{
    if (activePass_ >= 0)
        endPass();
    activePass_ = pass;
#ifdef TRACY_ENABLE
    if (tracyGpuOwner() == this) {
        const char* name = passName(pass);
        tracyPassZone().emplace(static_cast<std::uint32_t>(__LINE__), __FILE__, std::strlen(__FILE__), "render pass", 11, name,
                                std::strlen(name), true);
        tracyGpuPassZone().emplace(static_cast<std::uint32_t>(__LINE__), __FILE__, std::strlen(__FILE__), "render pass", 11, name,
                                   std::strlen(name), true);
    }
#endif
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
#ifdef TRACY_ENABLE
    tracyGpuPassZone().reset();
    tracyPassZone().reset();
#endif
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
    stats_.meshDraws = building_.meshDraws;
    stats_.culled = building_.culled;
    stats_.triangles = building_.triangles;
    stats_.uploads = building_.uploads;
    stats_.occluded = building_.occluded;
    stats_.cullMs = building_.cullMs;
    stats_.cullWaitMs = building_.cullWaitMs;
    stats_.renderScale = building_.renderScale;
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

void Renderer::renderClouds()
{
    if (!cloudNoise_) {
        if (!cloudNoiseJob_)
            cloudNoiseJob_ = std::make_shared<std::future<std::vector<std::uint8_t>>>(JobSystem::global().submit([] { return buildCloudNoise(64); }));
        if (!deterministic_ && cloudNoiseJob_->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return; // the flat layer until the noise is ready (a fraction of a second)
        const std::vector<std::uint8_t> noise = cloudNoiseJob_->get();
        cloudNoiseJob_.reset();
        glCreateTextures(GL_TEXTURE_3D, 1, &cloudNoise_);
        glTextureStorage3D(cloudNoise_, 1, GL_RGBA8, 64, 64, 64);
        glTextureSubImage3D(cloudNoise_, 0, 0, 0, 0, 64, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, noise.data());
        glTextureParameteri(cloudNoise_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(cloudNoise_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        for (GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
            glTextureParameteri(cloudNoise_, wrap, GL_REPEAT);
        label(GL_TEXTURE, cloudNoise_, "Cloud noise");
        // The environment block was filled before the noise existed: switch the clouds on.
        if (envBlockCopy_.size() == sizeof(EnvironmentBlock)) {
            EnvironmentBlock b;
            std::memcpy(&b, envBlockCopy_.data(), sizeof(b));
            b.clouds.x = 1.0f;
            std::memcpy(envBlockCopy_.data(), &b, sizeof(b));
            glNamedBufferSubData(envBuffer_, 0, sizeof(b), &b);
        }
    }
    const int size = quality_.volumetricClouds >= 2 ? 512 : 256;
    if (size != cloudSize_) {
        if (cloudTex_)
            glDeleteTextures(1, &cloudTex_);
        if (cloudFbo_)
            glDeleteFramebuffers(1, &cloudFbo_);
        cloudSize_ = size;
        cloudTex_ = makeTexture2D(GL_RGBA16F, size, size / 2, GL_LINEAR);
        glTextureParameteri(cloudTex_, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(cloudTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        cloudFbo_ = makeFbo(cloudTex_);
        label(GL_TEXTURE, cloudTex_, "Cloud sky map");
        cloudFull_ = true;
    }
    // Anything but the drift changed (sun, cover, layer): trace every texel, else a quarter.
    std::size_t h = 0;
    if (envBlockCopy_.size() == sizeof(EnvironmentBlock)) {
        EnvironmentBlock b;
        std::memcpy(&b, envBlockCopy_.data(), sizeof(b));
        auto q = [](float v) { return static_cast<std::size_t>(std::lround(v * 200.0f)); };
        for (float v : {b.sunDir.x, b.sunDir.y, b.sunDir.z, b.sunDir.w, b.sunColor.x, b.sunColor.y, b.sunColor.z, b.skyZenith.w,
                        b.clouds.y * 0.01f, b.clouds.z * 0.01f, b.clouds.w, b.atmosphere.x, b.weather.y})
            h = h * 1000003u ^ q(v);
    }
    if (h != cloudHash_ || deterministic_)
        cloudFull_ = true;
    cloudHash_ = h;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(emptyVao_);
    glBindFramebuffer(GL_FRAMEBUFFER, cloudFbo_);
    glViewport(0, 0, cloudSize_, cloudSize_ / 2);
    const Program& p = programs_.clouds;
    glUseProgram(p.id);
    glUniform1i(p.loc("uPhase"), cloudFull_ ? -1 : cloudPhase_);
    glUniform1i(p.loc("uSteps"), quality_.volumetricClouds >= 2 ? 40 : 28);
    glBindTextureUnit(0, cloudNoise_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    cloudPhase_ = (cloudPhase_ + 1) & 3;
    cloudFull_ = false;
    glBindTextureUnit(16, cloudTex_); // 2D target of unit 16 (the probes' 3D irradiance uses the 3D one): sky.frag uCloudMap
}

void Renderer::updateDynamicResolution()
{
    // GPU time grows with the pixel count (~ scale^2): aim a little under the budget, step in 5 %
    // so the targets are not made again every frame, and wait between changes for the timer
    // queries (a few frames behind) to show the new size.
    if (!stats_.gpuValid || stats_.gpuTotalMs <= 0.0)
        return;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    dynGpuMs_ = dynGpuMs_ > 0.0 ? dynGpuMs_ + (stats_.gpuTotalMs - dynGpuMs_) * 0.15 : stats_.gpuTotalMs;
    const int bucket = std::clamp(static_cast<int>(std::lround(dynScale_ * 20.0f)), 0, 20);
    if (now - dynLastChange_ > 0.25) { // the queries show the current size by now
        DynSample& d = dynSamples_[bucket];
        d.ms = d.time >= 0.0 && now - d.time < 2.0 ? d.ms + (stats_.gpuTotalMs - d.ms) * 0.1 : stats_.gpuTotalMs;
        d.time = now;
    }
    if (now - dynLastChange_ < 0.4)
        return;
    const double budget = 1000.0 / std::max(quality_.dynamicTargetFps, 10.0f);
    const float lo = std::clamp(quality_.dynamicMinScale, 0.25f, 1.0f);
    // Fit ms = fixed + perPixel * s^2 over the sizes seen in the last 60 s (the view changes).
    double n = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, xMin = 2.0, xMax = -1.0;
    for (int i = 0; i <= 20; ++i)
        if (dynSamples_[i].time >= 0.0 && now - dynSamples_[i].time < 60.0) {
            const double x = (i / 20.0) * (i / 20.0), y = dynSamples_[i].ms;
            n += 1.0; sx += x; sy += y; sxx += x * x; sxy += x * y;
            xMin = std::min(xMin, x);
            xMax = std::max(xMax, x);
        }
    const bool fit = n >= 2.0 && xMax - xMin > 0.15 && n * sxx - sx * sx > 1e-9;
    const double perPixel = fit ? std::max((n * sxy - sx * sy) / (n * sxx - sx * sx), 0.0) : 0.0;
    float next = dynScale_;
    if (dynGpuMs_ > budget) {
        next = static_cast<float>(dynScale_ * std::sqrt(budget * 0.9 / dynGpuMs_));
        // A smaller image only helps by its pixel share: when going down to the minimum would
        // save under a tenth of the frame, the cost is elsewhere (geometry, shadows) and a
        // blurrier image buys nothing; stay sharp.
        if (fit && perPixel * (static_cast<double>(dynScale_) * dynScale_ - static_cast<double>(lo) * lo) < 0.1 * dynGpuMs_)
            next = dynScale_ + 0.05f;
    } else if (dynGpuMs_ < budget * 0.72) {
        next = dynScale_ + 0.05f;
    } else if (fit && dynScale_ < 1.0f) {
        // Within the budget: grow while the fit says the larger image still fits.
        const double fixed = (sy - perPixel * sx) / n;
        const double up = static_cast<double>(dynScale_) + 0.05;
        if (fixed + perPixel * up * up < budget * 0.9)
            next = dynScale_ + 0.05f;
    }
    next = std::clamp(std::floor(next * 20.0f + 0.001f) / 20.0f, lo, 1.0f);
    if (next == dynScale_)
        return;
    dynGpuMs_ *= static_cast<double>(next * next) / static_cast<double>(dynScale_ * dynScale_); // expected at the new size
    dynScale_ = next;
    dynLastChange_ = now;
}

const Model* Renderer::impostorFor(const Model& model)
{
    if (model.parts.empty())
        return nullptr;
    const std::uint64_t key = model.parts[0].id;
    if (const auto it = impostors_.find(key); it != impostors_.end())
        return it->second.get();
    impostorQueue_.try_emplace(key, model); // baked at the start of the next frame
    return nullptr;
}

void Renderer::bakePendingImpostors()
{
    // Baked once the model's base colour textures are on the GPU (they stream in), a few
    // per frame: no hitch when a level with many tree kinds appears.
    int budget = 8;
    for (auto it = impostorQueue_.begin(); it != impostorQueue_.end() && budget > 0;) {
        bool ready = true;
        const float distance = drawDistance_;
        drawDistance_ = 0.0f;
        for (const MeshPart& part : it->second.parts) {
            const Material* material = it->second.materialFor(part);
            const Texture* albedo = material ? (material->maps[0] ? material->maps[0]->texture.get() : nullptr) : part.texture.get();
            if (!albedo || !albedo->valid())
                continue;
            ready = ready && texture(*albedo) != 0; // any resident mip is enough for a 256 px view
        }
        drawDistance_ = distance;
        int& waited = impostorWait_[it->first];
        if (!ready && ++waited < 600) {
            ++it;
            continue;
        }
        impostorWait_.erase(it->first);
        impostors_[it->first] = bakeImpostor(it->second); // null (no bounds, animated) is kept: not tried again
        it = impostorQueue_.erase(it);
        --budget;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(targetFbo_));
}

std::shared_ptr<const Model> Renderer::bakeImpostor(const Model& model, int size)
{
    if (model.parts.empty() || model.skeleton)
        return nullptr;
    Aabb b = model.localBounds;
    if (b.min == b.max) {
        b = {glm::vec3(1e30f), glm::vec3(-1e30f)};
        for (const MeshPart& part : model.parts)
            for (const glm::vec3& p : part.positions) {
                b.min = glm::min(b.min, p);
                b.max = glm::max(b.max, p);
            }
        if (b.min.x > b.max.x)
            return nullptr;
    }
    const glm::vec3 extent = glm::max(b.max - b.min, glm::vec3(0.01f));
    const glm::vec3 center = (b.min + b.max) * 0.5f;
    const float reach = glm::length(extent) + 1.0f;
    const int width = size * 2, height = size;

    // Two orthographic views of the model's base colour (unlit) side by side: from +Z
    // (left half) and from +X (right half), for two quads crossing at the centre.
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    unsigned color = 0, depth = 0, fbo = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &color);
    glTextureStorage2D(color, 1, GL_RGBA8, width, height);
    glCreateRenderbuffers(1, &depth);
    glNamedRenderbufferStorage(depth, GL_DEPTH_COMPONENT24, width, height);
    glCreateFramebuffers(1, &fbo);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, color, 0);
    glNamedFramebufferRenderbuffer(fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    useMeshProgram(programs_.mesh);
    glUniform1i(loc_.shading, 1);
    glUniform1i(loc_.overlay, 0);
    glUniform1i(loc_.emissive, 0);
    glUniform1f(loc_.flash, 0.0f);
    glUniform1f(loc_.sway, 0.0f);
    glUniform2f(loc_.fade, 0.0f, 0.0f);
    glUniform1i(loc_.instanced, 0);
    const bool depthOnly = depthOnly_, collect = collectTransparent_;
    depthOnly_ = collectTransparent_ = false;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    for (int v = 0; v < 2; ++v) {
        glViewport(v * size, 0, size, size);
        const glm::vec3 eye = center + (v == 0 ? glm::vec3(0.0f, 0.0f, reach) : glm::vec3(reach, 0.0f, 0.0f));
        const float halfW = (v == 0 ? extent.x : extent.z) * 0.5f;
        const glm::mat4 proj = glm::ortho(-halfW, halfW, -extent.y * 0.5f, extent.y * 0.5f, 0.01f, reach * 2.0f);
        const glm::mat4 vp = proj * glm::lookAt(eye, center, up);
        glUniformMatrix4fv(loc_.viewProj, 1, GL_FALSE, glm::value_ptr(vp));
        drawModel(model, glm::mat4(1.0f), 0.0f);
    }
    depthOnly_ = depthOnly;
    collectTransparent_ = collect;
    glUniform1i(loc_.shading, 0);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTextureImage(color, 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(pixels.size()), pixels.data());
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &depth);
    glDeleteTextures(1, &color);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    // Top row first, linear -> sRGB, and the colour of covered pixels spread into the
    // empty ones around them (no dark fringe when filtering and mipmapping).
    auto texture = std::make_shared<Texture>();
    texture->id = newResourceId();
    texture->width = width;
    texture->height = height;
    texture->wrap = 1;
    texture->rgba.resize(pixels.size());
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* src = &pixels[(static_cast<std::size_t>(height - 1 - y) * width + x) * 4];
            std::uint8_t* dst = &texture->rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            for (int c = 0; c < 3; ++c)
                dst[c] = static_cast<std::uint8_t>(std::lround(std::pow(src[c] / 255.0f, 1.0f / 2.2f) * 255.0f));
            dst[3] = src[3] > 0 ? 255 : 0;
        }
    std::vector<std::uint8_t>& rgba = texture->rgba;
    for (int pass = 0; pass < 8; ++pass) {
        std::vector<std::uint8_t> next = rgba;
        bool changed = false;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
                if (rgba[i + 3] != 0 || next[i] != 0 || next[i + 1] != 0 || next[i + 2] != 0)
                    continue;
                int sum[3] = {0, 0, 0}, n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                            continue;
                        const std::size_t j = (static_cast<std::size_t>(ny) * width + nx) * 4;
                        if (rgba[j] == 0 && rgba[j + 1] == 0 && rgba[j + 2] == 0 && rgba[j + 3] == 0)
                            continue;
                        for (int c = 0; c < 3; ++c)
                            sum[c] += rgba[j + c];
                        ++n;
                    }
                if (n > 0) {
                    for (int c = 0; c < 3; ++c)
                        next[i + c] = static_cast<std::uint8_t>(std::max(1, sum[c] / n));
                    changed = true;
                }
            }
        rgba.swap(next);
        if (!changed)
            break;
    }

    MeshPart part;
    part.id = newResourceId();
    part.texture = texture;
    part.alphaMode = AlphaMode::Mask;
    part.alphaCutoff = 0.5f;
    part.doubleSided = true;
    part.color = glm::vec3(1.0f);
    part.roughness = 0.9f;
    part.materialName = "impostor";
    auto quad = [&](glm::vec3 a, glm::vec3 bb, glm::vec3 c, glm::vec3 d, float u0, float u1) {
        const auto base = static_cast<std::uint32_t>(part.positions.size());
        for (const glm::vec3& p : {a, bb, c, d})
            part.positions.push_back(p);
        // Lit mostly from above, like a crown of leaves, and a little to the side.
        for (int k = 0; k < 4; ++k)
            part.normals.push_back(glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f)));
        part.texcoords.insert(part.texcoords.end(), {{u0, 1.0f}, {u1, 1.0f}, {u1, 0.0f}, {u0, 0.0f}});
        part.indices.insert(part.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    quad({b.min.x, b.min.y, center.z}, {b.max.x, b.min.y, center.z}, {b.max.x, b.max.y, center.z}, {b.min.x, b.max.y, center.z}, 0.0f, 0.5f);
    quad({center.x, b.min.y, b.max.z}, {center.x, b.min.y, b.min.z}, {center.x, b.max.y, b.min.z}, {center.x, b.max.y, b.max.z}, 0.5f, 1.0f);
    auto impostor = std::make_shared<Model>();
    impostor->name = model.name + " (impostor)";
    impostor->parts.push_back(std::move(part));
    impostor->localBounds = b;
    impostor->castShadows = model.castShadows;
    impostor->sway = model.sway;
    return impostor;
}

void Renderer::growMeshPool(std::size_t vertices, std::size_t indices)
{
    // A bigger buffer, the old content copied over (offsets stay valid), the VAO rebound.
    auto enlarge = [](unsigned& buffer, RangeAllocator& alloc, std::size_t need, std::size_t unit, std::size_t minimum) {
        if (alloc.largestFree() >= need && buffer)
            return;
        std::size_t capacity = std::max(alloc.capacity(), minimum);
        while (capacity - alloc.capacity() < need || capacity < alloc.capacity() + need)
            capacity *= 2;
        unsigned grown = 0;
        glCreateBuffers(1, &grown);
        glNamedBufferStorage(grown, static_cast<GLsizeiptr>(capacity * unit), nullptr, GL_DYNAMIC_STORAGE_BIT);
        if (buffer) {
            glCopyNamedBufferSubData(buffer, grown, 0, 0, static_cast<GLsizeiptr>(alloc.capacity() * unit));
            glDeleteBuffers(1, &buffer);
        }
        buffer = grown;
        alloc.grow(capacity);
    };
    enlarge(poolVbo_, poolVertices_, vertices, sizeof(Vertex), 1u << 20);  // 1 M vertices (56 MB) to start
    enlarge(poolIbo_, poolIndices_, indices, sizeof(std::uint32_t), 4u << 20);
    if (!poolVao_) {
        glCreateVertexArrays(1, &poolVao_);
        // Per-instance index (binding 1, divisor 1): the draw's base instance plus the
        // instance number, so multi-draw commands find their transforms.
        glEnableVertexArrayAttrib(poolVao_, 5);
        glVertexArrayAttribIFormat(poolVao_, 5, 1, GL_UNSIGNED_INT, 0);
        glVertexArrayAttribBinding(poolVao_, 5, 1);
        glVertexArrayBindingDivisor(poolVao_, 1, 1);
        ensureIota(4096);
    }
    setupVertexArray(poolVao_, poolVbo_);
    glVertexArrayElementBuffer(poolVao_, poolIbo_);
    for (auto& [id, mesh] : meshes_)
        mesh.vao = poolVao_;
}

void Renderer::ensureIota(std::size_t count)
{
    if (count <= iotaCount_ && iotaBuffer_)
        return;
    count = std::max(count, iotaCount_ * 2);
    std::vector<std::uint32_t> iota(count);
    for (std::size_t i = 0; i < count; ++i)
        iota[i] = static_cast<std::uint32_t>(i);
    if (iotaBuffer_)
        glDeleteBuffers(1, &iotaBuffer_);
    glCreateBuffers(1, &iotaBuffer_);
    glNamedBufferStorage(iotaBuffer_, static_cast<GLsizeiptr>(count * sizeof(std::uint32_t)), iota.data(), 0);
    iotaCount_ = count;
    if (poolVao_)
        glVertexArrayVertexBuffer(poolVao_, 1, iotaBuffer_, 0, sizeof(std::uint32_t));
}

void Renderer::releaseMesh(const GpuMesh& mesh)
{
    poolVertices_.release(static_cast<std::size_t>(mesh.baseVertex), static_cast<std::size_t>(mesh.vertexCount));
    poolIndices_.release(static_cast<std::size_t>(mesh.firstIndex), static_cast<std::size_t>(mesh.indexCount));
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
    auto v = poolVertices_.allocate(vertices.size());
    auto i = poolIndices_.allocate(part.indices.size());
    if (!v || !i) {
        if (v)
            poolVertices_.release(*v, vertices.size());
        if (i)
            poolIndices_.release(*i, part.indices.size());
        growMeshPool(vertices.size(), part.indices.size());
        v = poolVertices_.allocate(vertices.size());
        i = poolIndices_.allocate(part.indices.size());
    }
    m.vao = poolVao_;
    m.baseVertex = static_cast<int>(*v);
    m.firstIndex = static_cast<int>(*i);
    m.vertexCount = static_cast<int>(vertices.size());
    glNamedBufferSubData(poolVbo_, static_cast<GLintptr>(*v * sizeof(Vertex)), static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                         vertices.data());
    glNamedBufferSubData(poolIbo_, static_cast<GLintptr>(*i * sizeof(std::uint32_t)),
                         static_cast<GLsizeiptr>(part.indices.size() * sizeof(std::uint32_t)), part.indices.data());
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
    // Deterministic (tests): frames instead of seconds, and reads finish before they are used.
    const double now = deterministic_ ? static_cast<double>(frameNumber_) / 60.0 : secondsNow();
    int uploads = 0;
    for (auto& [id, g] : textures_) {
        // Finished reads: swap in the new texture.
        if (g.pending && (deterministic_ || g.pending->wait_for(std::chrono::seconds(0)) == std::future_status::ready)) {
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
    if (quality.ssaoFullRes != quality_.ssaoFullRes)
        width_ = 0; // the ambient occlusion targets change size: ensureTargets() makes them again
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
    m.frameArenaBytes = frameArena_.capacity();
    m.frameArenaPeak = frameArena_.peak();
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

const Renderer::GpuPaint* Renderer::uploadPaint(const VertexPaint& paint, const Model& model)
{
    GpuPaint& gpu = paints_[&paint];
    gpu.lastFrame = frameNumber_;
    if (gpu.buffer && gpu.version == paint.version && gpu.partOffsets.size() == model.parts.size())
        return &gpu;
    // One buffer for all parts; parts whose vertex count differs (the model file changed) get none.
    std::vector<std::uint32_t> packed;
    gpu.partOffsets.assign(model.parts.size(), -1);
    for (std::size_t i = 0; i < model.parts.size() && i < paint.parts.size(); ++i) {
        const auto& colors = paint.parts[i];
        if (colors.empty() || colors.size() != model.parts[i].positions.size())
            continue;
        gpu.partOffsets[i] = static_cast<int>(packed.size());
        for (const glm::u8vec4& c : colors)
            packed.push_back(static_cast<std::uint32_t>(c.r) | static_cast<std::uint32_t>(c.g) << 8 |
                             static_cast<std::uint32_t>(c.b) << 16 | static_cast<std::uint32_t>(c.a) << 24);
    }
    if (packed.empty())
        packed.push_back(0);
    const std::size_t bytes = packed.size() * sizeof(std::uint32_t);
    if (!gpu.buffer || bytes > gpu.capacity) {
        if (gpu.buffer)
            glDeleteBuffers(1, &gpu.buffer);
        glCreateBuffers(1, &gpu.buffer);
        gpu.capacity = bytes;
        glNamedBufferStorage(gpu.buffer, static_cast<GLsizeiptr>(bytes), nullptr, GL_DYNAMIC_STORAGE_BIT);
    }
    glNamedBufferSubData(gpu.buffer, 0, static_cast<GLsizeiptr>(bytes), packed.data());
    gpu.version = paint.version;
    return &gpu;
}

void Renderer::releaseUnusedPaint()
{
    // A painted model nothing drew for ten seconds or so (deleted, or a far cell): free its buffer.
    for (auto it = paints_.begin(); it != paints_.end();)
        if (frameNumber_ - it->second.lastFrame > 600) {
            glDeleteBuffers(1, &it->second.buffer);
            it = paints_.erase(it);
        } else {
            ++it;
        }
}

void Renderer::drawModel(const Model& model, const glm::mat4& transform, float highlight, const Animator* animator,
                         const VertexPaint* paint)
{
    glUniform1f(loc_.highlight, highlight);
    const GpuPaint* gpuPaint = paint && !depthOnly_ ? uploadPaint(*paint, model) : nullptr;
    if (gpuPaint)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, gpuPaint->buffer);
    const Skeleton* skeleton = model.skeleton.get();
    if (skeleton)
        evaluatePose(*skeleton, animator, pose_);
    int uploadedSkin = -1;
    for (std::size_t partIndex = 0; partIndex < model.parts.size(); ++partIndex) {
        const MeshPart& part = model.parts[partIndex];
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
        const int paintOffset = gpuPaint && partIndex < gpuPaint->partOffsets.size() ? gpuPaint->partOffsets[partIndex] : -1;
        if (gpuPaint) {
            glUniform1i(loc_.paintBase, paintOffset);
            glUniform1i(loc_.paintBaseVertex, mesh.baseVertex);
        }
        glBindVertexArray(mesh.vao);
        const bool twoSided = alpha.doubleSided && glIsEnabled(GL_CULL_FACE);
        if (twoSided)
            glDisable(GL_CULL_FACE);
        glDrawElementsBaseVertex(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT,
                                 reinterpret_cast<const void*>(static_cast<std::uintptr_t>(mesh.firstIndex) * sizeof(std::uint32_t)),
                                 mesh.baseVertex);
        ++drawCalls_;
        triangles_ += mesh.indexCount / 3;
        if (twoSided)
            glEnable(GL_CULL_FACE);
    }
    if (gpuPaint)
        glUniform1i(loc_.paintBase, -1);
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
    // Texture units: 0 albedo, 1 normal, 2 roughness, 3 metallic, 4 occlusion, 5 emissive,
    // 22 detail albedo, 23 detail normal, 30 height.
    const Texture* maps[kMapCount] = {};
    glm::vec3 detail(8.0f, 0.0f, 12.0f);
    glm::vec2 tiling(1.0f);
    float roughness = 0.8f, metallic = 0.0f, normalStrength = 1.0f, heightDepth = 0.0f;
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
        heightDepth = std::clamp(material->heightDepth, 0.0f, 0.2f) * (quality_.parallax ? 1.0f : 0.0f);
        emissive = material->emissive;
        detail = {material->detailTiling, std::clamp(material->detailStrength, 0.0f, 1.0f), std::max(material->detailDistance, 0.1f)};
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
    glUniform3fv(loc_.detail, 1, glm::value_ptr(detail));
    glUniform1f(loc_.heightDepth, heightDepth);
    for (int i = 0; i < kMapCount; ++i) {
        const auto kind = static_cast<MapKind>(i);
        const bool isDetail = kind == MapKind::DetailAlbedo || kind == MapKind::DetailNormal;
        const bool wanted = isDetail ? detail.y > 0.0f : (kind != MapKind::Height || heightDepth > 0.0f);
        const unsigned handle = maps[i] && maps[i]->valid() && wanted ? texture(*maps[i]) : 0u;
        const bool has = handle != 0;
        const unsigned unit = isDetail ? static_cast<unsigned>(22 + i - static_cast<int>(MapKind::DetailAlbedo))
                                       : (kind == MapKind::Height ? 30u : static_cast<unsigned>(i));
        if (has)
            glBindTextureUnit(unit, handle);
        glUniform1i(loc_.hasMap[i], has ? 1 : 0);
    }
    // Blend layers (vertex paint): units 24-25 albedo, 26-27 normal, 28-29 roughness.
    int blendBits = 0;
    if (material) {
        glm::vec3 layerColor[2] = {glm::vec3(1.0f), glm::vec3(1.0f)};
        glm::vec2 layerTiling[2] = {glm::vec2(1.0f), glm::vec2(1.0f)};
        glm::vec2 layerSurface[2] = {glm::vec2(0.8f, 0.0f), glm::vec2(0.8f, 0.0f)};
        float layerNormal[2] = {1.0f, 1.0f};
        for (int i = 0; i < Material::kBlendLayers; ++i) {
            const Material* layer = material->layers[static_cast<std::size_t>(i)].get();
            if (!layer)
                continue;
            blendBits |= 1 << i;
            layerColor[i] = layer->color;
            layerTiling[i] = layer->tiling;
            layerSurface[i] = {layer->roughness, layer->metallic};
            layerNormal[i] = layer->normalStrength;
            const MapKind kinds[3] = {MapKind::Albedo, MapKind::Normal, MapKind::Roughness};
            for (int k = 0; k < 3; ++k) {
                const auto& asset = layer->map(kinds[k]);
                const unsigned handle = asset && asset->texture && asset->texture->valid() ? texture(*asset->texture) : 0u;
                if (!handle)
                    continue;
                glBindTextureUnit(static_cast<unsigned>(24 + k * 2 + i), handle);
                blendBits |= 4 << (k * 2 + i);
            }
        }
        if (blendBits) {
            glUniform3fv(loc_.blendColor, 2, glm::value_ptr(layerColor[0]));
            glUniform2fv(loc_.blendTiling, 2, glm::value_ptr(layerTiling[0]));
            glUniform2fv(loc_.blendSurface, 2, glm::value_ptr(layerSurface[0]));
            glUniform1fv(loc_.blendNormalStrength, 2, layerNormal);
            glUniform1f(loc_.blendSharpness, std::clamp(material->blendSharpness, 1.0f, 32.0f));
        }
    }
    glUniform1i(loc_.blendLayers, blendBits);
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
    grid_.transforms.resize(n);
    grid_.cullDistance.resize(n);
    grid_.flags.resize(n);
    grid_.diagonal.resize(n);
    grid_.random.resize(n);
    grid_.detail.resize(n);
    grid_.cells.clear();
    // The packed arrays (structure of arrays) in parallel: transforms and bounds are the
    // costly part (rotations), each model is independent.
    JobSystem::global().parallelFor(n, 256, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            const Model& m = scene.models[i];
            const Aabb b = m.bounds();
            grid_.bounds[i] = b;
            grid_.transforms[i] = m.transform();
            grid_.cullDistance[i] = m.cullDistance;
            grid_.flags[i] = static_cast<std::uint8_t>((m.castShadows ? ModelGrid::kCastShadows : 0) | (m.skeleton ? ModelGrid::kAnimated : 0));
            grid_.diagonal[i] = glm::length(b.max - b.min);
            std::uint32_t r = static_cast<std::uint32_t>(i) * 2654435761u;
            r ^= r >> 15;
            r *= 2246822519u;
            r ^= r >> 13;
            grid_.random[i] = static_cast<float>(r & 0xffffff) / 16777216.0f;
            grid_.detail[i] = m.cullDistance > 0.0f && !m.collision && !m.skeleton && b.max.y - b.min.y < 2.0f; // low: grass, weeds, debris
        }
    });
    constexpr float kCell = 32.0f;
    std::unordered_map<std::uint64_t, std::size_t> index;
    for (std::size_t i = 0; i < n; ++i) {
        const Model& m = scene.models[i];
        const Aabb& b = grid_.bounds[i];
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
            releaseMesh(it->second);
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
    if (!occ_.testing || shadowCascade_ >= 0 || !hiddenByOcclusion(box))
        return false;
    ++const_cast<Renderer*>(this)->occ_.hidden;
    return true;
}

bool Renderer::hiddenByOcclusion(const Aabb& box) const
{
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
    return true;
}

} // namespace engine
