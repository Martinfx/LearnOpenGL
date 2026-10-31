#include "engine/Renderer.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace engine {

namespace {

const char* kVertexShader = R"(#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(std140, binding = 0) uniform Joints { mat4 uJoints[256]; };
uniform bool uSkinned;
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
void main() {
    mat4 skin = mat4(1.0);
    if (uSkinned)
        skin = aWeights.x * uJoints[aJoints.x] + aWeights.y * uJoints[aJoints.y]
             + aWeights.z * uJoints[aJoints.z] + aWeights.w * uJoints[aJoints.w];
    vec4 world = uModel * skin * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(uModel) * mat3(skin) * aNormal; // rotation + scale; renormalized in the fragment shader
    vUV = aUV;
    gl_Position = uViewProj * world;
}
)";

// Physically based shading (metal/roughness, GGX) with one sun, a sky/ground
// ambient term, normal maps (screen-space tangent frame, so no vertex tangents
// are needed and world-projected textures work too), ACES tone mapping and fog.
// Plain surfaces without a material get a 1 m grid to judge distance and speed.
const char* kFragmentShader = R"(#version 450 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
layout(binding = 0) uniform sampler2D uAlbedoMap;
layout(binding = 1) uniform sampler2D uNormalMap;
layout(binding = 2) uniform sampler2D uRoughnessMap;
layout(binding = 3) uniform sampler2D uMetallicMap;
layout(binding = 4) uniform sampler2D uOcclusionMap;
layout(binding = 5) uniform sampler2D uEmissiveMap;
uniform bool uHasTexture;      // albedo
uniform bool uHasNormalMap;
uniform bool uHasRoughnessMap;
uniform bool uHasMetallicMap;
uniform bool uHasOcclusionMap;
uniform bool uHasEmissiveMap;
uniform int uRoughnessChannel; // 0 = R (separate map), 1 = G (glTF metal/rough)
uniform int uMetallicChannel;  // 0 = R, 2 = B (glTF)
uniform float uNormalStrength;
uniform float uRoughness;
uniform float uMetallic;
uniform bool uEmissive;        // unlit, full color (effects)
uniform float uFogDensity;
uniform vec3 uColor;           // sRGB
uniform vec3 uSunDir;
uniform vec3 uCamera;
uniform float uHighlight;
uniform float uFlash;
uniform vec2 uTiling;
uniform bool uWorldUv;     // project the texture in world space (1 repeat per metre * tiling)
uniform bool uGrid;
uniform vec3 uEmissiveColor; // sRGB
out vec4 fragColor;

const vec3 kSunColor = vec3(1.0, 0.96, 0.88) * 3.0;
const vec3 kSky = vec3(0.32, 0.38, 0.48);
const vec3 kGround = vec3(0.20, 0.18, 0.16);
const float PI = 3.14159265;

float gridLines(vec3 p, vec3 n) {
    vec2 uv = abs(n.y) > 0.5 ? p.xz : (abs(n.x) > 0.5 ? p.zy : p.xy);
    vec2 g = abs(fract(uv - 0.5) - 0.5) / fwidth(uv);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

// Tangent frame from screen-space derivatives (Schueler, "Normal mapping without precomputed tangents").
vec3 perturbNormal(vec3 n, vec3 p, vec2 uv, vec3 mapNormal) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(t, t), dot(b, b)));
    if (isinf(invmax) || isnan(invmax))
        return n;
    mat3 tbn = mat3(t * invmax, b * invmax, n);
    return normalize(tbn * mapNormal);
}

float distributionGGX(float nh, float a) {
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometrySmith(float nv, float nl, float rough) {
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    return nv / (nv * (1.0 - k) + k) * nl / (nl * (1.0 - k) + k);
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    if (uEmissive) {
        fragColor = vec4(uColor, 1.0);
        return;
    }
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing)
        n = -n; // two-sided foliage and thin parts
    vec2 uv = vUV;
    if (uWorldUv) {
        vec3 an = abs(n);
        uv = an.y > 0.5 ? vWorldPos.xz : (an.x > 0.5 ? vec2(vWorldPos.z, -vWorldPos.y) : vec2(vWorldPos.x, -vWorldPos.y));
    }
    uv *= uTiling;

    vec3 albedo = pow(uColor, vec3(2.2)); // colors are authored in sRGB
    if (uHasTexture)
        albedo *= texture(uAlbedoMap, uv).rgb; // sRGB textures are sampled as linear
    else if (uGrid)
        albedo *= 1.0 - 0.18 * gridLines(vWorldPos, n);
    if (uHasNormalMap) {
        vec3 m = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
        m.xy *= uNormalStrength;
        n = perturbNormal(n, vWorldPos, uv, normalize(m));
    }
    float rough = uRoughness;
    if (uHasRoughnessMap)
        rough *= texture(uRoughnessMap, uv)[uRoughnessChannel];
    rough = clamp(rough, 0.04, 1.0);
    float metal = uMetallic;
    if (uHasMetallicMap)
        metal *= texture(uMetallicMap, uv)[uMetallicChannel];
    float ao = uHasOcclusionMap ? texture(uOcclusionMap, uv).r : 1.0;

    vec3 v = normalize(uCamera - vWorldPos);
    vec3 l = normalize(-uSunDir);
    vec3 h = normalize(l + v);
    float nl = max(dot(n, l), 0.0);
    float nv = max(dot(n, v), 1e-4);
    float nh = max(dot(n, h), 0.0);
    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
    vec3 specular = distributionGGX(nh, rough * rough) * geometrySmith(nv, nl, rough) * fresnel / (4.0 * nv * max(nl, 1e-4));
    vec3 diffuse = (1.0 - fresnel) * (1.0 - metal) * albedo / PI;
    vec3 color = (diffuse + specular) * kSunColor * nl;

    // Ambient: sky above, ground below; a rough reflection of the same for the specular part.
    vec3 ambientDiffuse = mix(kGround, kSky, n.y * 0.5 + 0.5);
    vec3 r = reflect(-v, n);
    vec3 ambientSpecular = mix(kGround, kSky * 1.4, smoothstep(-0.2, 0.4, r.y));
    vec3 fAmbient = f0 + (max(vec3(1.0 - rough), f0) - f0) * pow(1.0 - nv, 5.0);
    color += ((1.0 - fAmbient) * (1.0 - metal) * albedo * ambientDiffuse
              + fAmbient * ambientSpecular * (1.0 - rough * 0.7)) * ao;

    vec3 emissive = pow(uEmissiveColor, vec3(2.2));
    if (uHasEmissiveMap)
        emissive *= texture(uEmissiveMap, uv).rgb;
    color += emissive;

    color = mix(color, vec3(1.0, 0.75, 0.2), uHighlight * 0.35);
    color = mix(color, vec3(1.0), uFlash); // hit feedback

    // Distance fog toward the sky color.
    float fog = 1.0 - exp(-length(uCamera - vWorldPos) * uFogDensity);
    color = mix(aces(color * 1.1), pow(vec3(0.55, 0.66, 0.80), vec3(2.2)), fog);
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
)";

unsigned compile(GLenum type, const char* source, std::string* error)
{
    const unsigned shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::array<char, 2048> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        if (error)
            *error = std::string("Shader compile failed: ") + log.data();
        glDeleteShader(shader);
        return 0;
    }
    return shader;
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

Renderer::~Renderer()
{
    // GL objects must be freed with a current context; owners call shutdown().
}

bool Renderer::init(GlLoader loader, std::string* error)
{
    const int version = gladLoadGL(reinterpret_cast<GLADloadfunc>(loader));
    if (version == 0 || GLAD_VERSION_MAJOR(version) * 10 + GLAD_VERSION_MINOR(version) < 45) {
        if (error)
            *error = "OpenGL 4.5 or newer is required";
        return false;
    }
    glInfo_ = std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))) + " / OpenGL "
            + reinterpret_cast<const char*>(glGetString(GL_VERSION));

    const unsigned vs = compile(GL_VERTEX_SHADER, kVertexShader, error);
    const unsigned fs = vs ? compile(GL_FRAGMENT_SHADER, kFragmentShader, error) : 0;
    if (!vs || !fs) {
        glDeleteShader(vs);
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) {
        std::array<char, 2048> log{};
        glGetProgramInfoLog(program_, static_cast<GLsizei>(log.size()), nullptr, log.data());
        if (error)
            *error = std::string("Shader link failed: ") + log.data();
        shutdown();
        return false;
    }
    locModel_ = glGetUniformLocation(program_, "uModel");
    locViewProj_ = glGetUniformLocation(program_, "uViewProj");
    locColor_ = glGetUniformLocation(program_, "uColor");
    locSun_ = glGetUniformLocation(program_, "uSunDir");
    locCamera_ = glGetUniformLocation(program_, "uCamera");
    locHighlight_ = glGetUniformLocation(program_, "uHighlight");
    locHasTexture_ = glGetUniformLocation(program_, "uHasTexture");
    locEmissive_ = glGetUniformLocation(program_, "uEmissive");
    locFog_ = glGetUniformLocation(program_, "uFogDensity");
    locSkinned_ = glGetUniformLocation(program_, "uSkinned");
    locFlash_ = glGetUniformLocation(program_, "uFlash");
    locTiling_ = glGetUniformLocation(program_, "uTiling");
    locWorldUv_ = glGetUniformLocation(program_, "uWorldUv");
    locGrid_ = glGetUniformLocation(program_, "uGrid");
    locRoughness_ = glGetUniformLocation(program_, "uRoughness");
    locMetallic_ = glGetUniformLocation(program_, "uMetallic");
    locNormalStrength_ = glGetUniformLocation(program_, "uNormalStrength");
    locRoughnessChannel_ = glGetUniformLocation(program_, "uRoughnessChannel");
    locMetallicChannel_ = glGetUniformLocation(program_, "uMetallicChannel");
    {
        const char* names[6] = {"uHasTexture", "uHasNormalMap", "uHasRoughnessMap", "uHasMetallicMap", "uHasOcclusionMap", "uHasEmissiveMap"};
        for (int i = 0; i < 6; ++i)
            locHasMap_[i] = glGetUniformLocation(program_, names[i]);
    }
    locEmissiveColor_ = glGetUniformLocation(program_, "uEmissiveColor");
    glCreateBuffers(1, &jointBuffer_);
    glNamedBufferData(jointBuffer_, kMaxJoints * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);

    const std::vector<Vertex> cube = unitCube();
    vertexCount_ = static_cast<int>(cube.size());
    glCreateBuffers(1, &vbo_);
    glNamedBufferStorage(vbo_, static_cast<GLsizeiptr>(cube.size() * sizeof(Vertex)), cube.data(), 0);
    glCreateVertexArrays(1, &vao_);
    setupVertexArray(vao_, vbo_);
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
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (program_) glDeleteProgram(program_);
    if (jointBuffer_) glDeleteBuffers(1, &jointBuffer_);
    vao_ = vbo_ = program_ = jointBuffer_ = 0;
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
    m.used = true;
    return meshes_.emplace(part.id, m).first->second;
}

unsigned Renderer::texture(const Texture& tex)
{
    auto it = textures_.find(tex.id);
    if (it != textures_.end()) {
        it->second.used = true;
        return it->second.handle;
    }
    GpuTexture t;
    glCreateTextures(GL_TEXTURE_2D, 1, &t.handle);
    const int levels = tex.mipmaps ? 1 + static_cast<int>(std::floor(std::log2(std::max(tex.width, tex.height)))) : 1;
    glTextureStorage2D(t.handle, levels, tex.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, tex.width, tex.height);
    glTextureSubImage2D(t.handle, 0, 0, 0, tex.width, tex.height, GL_RGBA, GL_UNSIGNED_BYTE, tex.rgba.data());
    if (tex.mipmaps)
        glGenerateTextureMipmap(t.handle);
    const GLint minFilter = tex.nearest ? (tex.mipmaps ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST)
                                        : (tex.mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTextureParameteri(t.handle, GL_TEXTURE_MIN_FILTER, minFilter);
    glTextureParameteri(t.handle, GL_TEXTURE_MAG_FILTER, tex.nearest ? GL_NEAREST : GL_LINEAR);
    const GLint wrap = tex.wrap == 1 ? GL_CLAMP_TO_EDGE : (tex.wrap == 2 ? GL_MIRRORED_REPEAT : GL_REPEAT);
    glTextureParameteri(t.handle, GL_TEXTURE_WRAP_S, wrap);
    glTextureParameteri(t.handle, GL_TEXTURE_WRAP_T, wrap);
    if (GLAD_GL_VERSION_4_6 || GLAD_GL_EXT_texture_filter_anisotropic)
        glTextureParameterf(t.handle, GL_TEXTURE_MAX_ANISOTROPY, 8.0f); // sharp floors at grazing angles
    t.used = true;
    textures_.emplace(tex.id, t);
    return t.handle;
}

void Renderer::drawModel(const Model& model, const glm::mat4& transform, float highlight, const Animator* animator)
{
    glUniform1f(locHighlight_, highlight);
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
        glUniform1i(locSkinned_, skinned ? 1 : 0);
        glUniformMatrix4fv(locModel_, 1, GL_FALSE, glm::value_ptr(partTransform));
        applySurface(model.materialFor(part), &part, part.color, false);
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    applySurface(nullptr, nullptr, glm::vec3(1.0f), false);
    glUniform1i(locSkinned_, 0);
}

void Renderer::applySurface(const Material* material, const MeshPart* part, glm::vec3 color, bool worldUv)
{
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
    glUniform3fv(locColor_, 1, glm::value_ptr(color));
    glUniform2fv(locTiling_, 1, glm::value_ptr(tiling));
    glUniform1f(locRoughness_, roughness);
    glUniform1f(locMetallic_, metallic);
    glUniform1f(locNormalStrength_, normalStrength);
    glUniform1i(locRoughnessChannel_, roughnessChannel);
    glUniform1i(locMetallicChannel_, metallicChannel);
    glUniform3fv(locEmissiveColor_, 1, glm::value_ptr(emissive));
    glUniform1i(locWorldUv_, worldUv ? 1 : 0);
    glUniform1i(locGrid_, grid ? 1 : 0);
    for (int i = 0; i < 6; ++i) {
        const bool has = maps[i] && !maps[i]->rgba.empty();
        if (has)
            glBindTextureUnit(static_cast<unsigned>(i), texture(*maps[i]));
        glUniform1i(locHasMap_[i], has ? 1 : 0);
    }
}

void Renderer::drawCube(const glm::mat4& transform, glm::vec3 color, float highlight, bool emissive)
{
    glBindVertexArray(vao_);
    glUniformMatrix4fv(locModel_, 1, GL_FALSE, glm::value_ptr(transform));
    applySurface(nullptr, nullptr, color, false);
    glUniform1f(locHighlight_, highlight);
    glUniform1i(locEmissive_, emissive ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    glUniform1i(locEmissive_, 0);
}

void Renderer::renderOverlay(const RenderView& view, const OverlayPass& pass)
{
    // First-person layer: own depth range and projection, so the weapon never clips into walls.
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(program_);
    const glm::mat4 viewProj = pass.projection; // geometry is already in view space
    glUniformMatrix4fv(locViewProj_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform3fv(locSun_, 1, glm::value_ptr(glm::normalize(pass.sunDirectionViewSpace)));
    glUniform3fv(locCamera_, 1, glm::value_ptr(glm::vec3(0.0f)));
    glUniform1f(locFog_, 0.0f);
    glUniform1f(locFlash_, 0.0f);
    glUniform1i(locSkinned_, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, jointBuffer_);
    if (pass.model)
        drawModel(*pass.model, pass.modelTransform, 0.0f, pass.animator);
    for (const OverlayBox& b : pass.boxes)
        drawCube(glm::scale(glm::translate(glm::mat4(1.0f), b.center), b.size), b.color, 0.0f, b.emissive);
    (void)view;
    glBindVertexArray(0);
    glUseProgram(0);
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

void Renderer::render(const Scene& scene, const RenderView& view)
{
    // Free GPU data nothing drew since the previous render() (removed models).
    releaseUnused();
    glViewport(0, 0, view.viewportWidth, view.viewportHeight);
    glClearColor(0.55f, 0.66f, 0.80f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glUseProgram(program_);
    const glm::mat4 viewProj = view.projection * view.view;
    glUniformMatrix4fv(locViewProj_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform3fv(locSun_, 1, glm::value_ptr(glm::normalize(scene.sunDirection)));
    glUniform3fv(locCamera_, 1, glm::value_ptr(view.cameraPosition));
    glUniform1f(locFog_, 0.012f);
    glUniform1i(locHasTexture_, 0);
    glUniform1i(locEmissive_, 0);
    glUniform1i(locSkinned_, 0);
    glUniform1f(locFlash_, 0.0f);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, jointBuffer_);
    glBindVertexArray(vao_);

    auto drawBox = [&](glm::vec3 center, glm::vec3 size, glm::vec3 color, float highlight, float yawDegrees = 0.0f,
                       const Material* material = nullptr) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
        if (yawDegrees != 0.0f)
            model = glm::rotate(model, -glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, size);
        glUniformMatrix4fv(locModel_, 1, GL_FALSE, glm::value_ptr(model));
        applySurface(material, nullptr, color, true); // boxes have no UVs: world-space mapping
        glUniform1f(locHighlight_, highlight);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    };

    for (std::size_t i = 0; i < scene.boxes.size(); ++i) {
        const Box& b = scene.boxes[i];
        drawBox(b.position, b.size, b.color, static_cast<int>(i) == view.selectedObject ? 1.0f : 0.0f, 0.0f, b.material.get());
    }
    for (std::size_t i = 0; i < scene.models.size(); ++i) {
        const Model& model = scene.models[i];
        const bool selected = static_cast<int>(scene.boxes.size() + i) == view.selectedObject;
        drawModel(model, model.transform(), selected ? 1.0f : 0.0f);
    }
    for (const OverlayBox& b : view.worldBoxes)
        drawCube(glm::scale(glm::translate(glm::mat4(1.0f), b.center), b.size), b.color, 0.0f, b.emissive);
    glBindVertexArray(vao_);
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const Entity& e = scene.entities[i];
        const float highlight = static_cast<int>(scene.boxes.size() + scene.models.size() + i) == view.selectedObject ? 1.0f : 0.0f;
        if (e.model) {
            // Animated character; stays as a corpse after death.
            glUniform1f(locFlash_, std::min(1.0f, e.flash * 8.0f));
            drawModel(*e.model, e.modelTransform(), highlight, &e.animator);
            glUniform1f(locFlash_, 0.0f);
            glBindVertexArray(vao_);
            continue;
        }
        if (!e.alive)
            continue;
        glUniform1f(locFlash_, std::min(1.0f, e.flash * 8.0f));
        drawBox(e.position + glm::vec3(0.0f, e.size.y * 0.5f, 0.0f), e.size, e.color, highlight, e.yawDegrees);
        glUniform1f(locFlash_, 0.0f);
        // A small "nose" shows which way the entity faces.
        const float yaw = glm::radians(e.yawDegrees);
        const glm::vec3 facing(std::cos(yaw), 0.0f, std::sin(yaw));
        const glm::vec3 nose = e.position + glm::vec3(0.0f, e.size.y * 0.8f, 0.0f) + facing * (e.size.x * 0.5f + 0.1f);
        drawBox(nose, glm::vec3(0.2f, 0.12f, 0.2f), glm::vec3(0.95f, 0.9f, 0.3f), highlight, e.yawDegrees);
    }
    if (view.drawPlayerStart) {
        // Player-sized marker so level designers see the spawn and its scale.
        drawBox(scene.playerStart + glm::vec3(0.0f, 0.9f, 0.0f), glm::vec3(0.6f, 1.8f, 0.6f),
                glm::vec3(0.2f, 0.8f, 0.3f), 0.0f);
    }
    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace engine
