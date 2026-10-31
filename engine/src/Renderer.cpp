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

// Blinn-Phong with one directional light, a sky/ground ambient term and a
// world-space 1 m grid so the player can judge distance and speed.
const char* kFragmentShader = R"(#version 450 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
uniform sampler2D uTexture;
uniform bool uHasTexture;
uniform bool uEmissive;
uniform float uFogDensity;
uniform vec3 uColor;
uniform vec3 uSunDir;
uniform vec3 uCamera;
uniform float uHighlight;
uniform float uFlash;
out vec4 fragColor;

float gridLines(vec3 p, vec3 n) {
    vec2 uv = abs(n.y) > 0.5 ? p.xz : (abs(n.x) > 0.5 ? p.zy : p.xy);
    vec2 g = abs(fract(uv - 0.5) - 0.5) / fwidth(uv);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main() {
    if (uEmissive) {
        fragColor = vec4(uColor, 1.0);
        return;
    }
    vec3 n = normalize(vNormal);
    vec3 l = normalize(-uSunDir);
    vec3 v = normalize(uCamera - vWorldPos);
    vec3 h = normalize(l + v);

    vec3 albedo = pow(uColor, vec3(2.2)); // colors are authored in sRGB
    vec3 base;
    if (uHasTexture)
        base = albedo * texture(uTexture, vUV).rgb; // sRGB texture, sampled as linear
    else
        base = albedo * (1.0 - 0.18 * gridLines(vWorldPos, n));
    float diffuse = max(dot(n, l), 0.0);
    float specular = pow(max(dot(n, h), 0.0), 48.0) * 0.25 * step(0.0, diffuse);
    vec3 ambient = mix(vec3(0.20, 0.18, 0.16), vec3(0.32, 0.38, 0.48), n.y * 0.5 + 0.5);

    vec3 color = base * (ambient + diffuse * vec3(1.0, 0.96, 0.88)) + specular;
    color = mix(color, vec3(1.0, 0.75, 0.2), uHighlight * 0.35);
    color = mix(color, vec3(1.0), uFlash); // hit feedback

    // Distance fog toward the sky color.
    float fog = 1.0 - exp(-length(uCamera - vWorldPos) * uFogDensity);
    color = mix(color, vec3(0.55, 0.66, 0.80), fog);
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
    glCreateBuffers(1, &jointBuffer_);
    glNamedBufferData(jointBuffer_, kMaxJoints * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "uTexture"), 0);
    glUseProgram(0);

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
    const int levels = 1 + static_cast<int>(std::floor(std::log2(std::max(tex.width, tex.height))));
    glTextureStorage2D(t.handle, levels, GL_SRGB8_ALPHA8, tex.width, tex.height);
    glTextureSubImage2D(t.handle, 0, 0, 0, tex.width, tex.height, GL_RGBA, GL_UNSIGNED_BYTE, tex.rgba.data());
    glGenerateTextureMipmap(t.handle);
    glTextureParameteri(t.handle, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(t.handle, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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
        glUniform3fv(locColor_, 1, glm::value_ptr(part.texture ? glm::vec3(1.0f) : part.color));
        if (part.texture) {
            glBindTextureUnit(0, texture(*part.texture));
            glUniform1i(locHasTexture_, 1);
        } else {
            glUniform1i(locHasTexture_, 0);
        }
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glUniform1i(locHasTexture_, 0);
    glUniform1i(locSkinned_, 0);
}

void Renderer::drawCube(const glm::mat4& transform, glm::vec3 color, float highlight, bool emissive)
{
    glBindVertexArray(vao_);
    glUniformMatrix4fv(locModel_, 1, GL_FALSE, glm::value_ptr(transform));
    glUniform3fv(locColor_, 1, glm::value_ptr(color));
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
        drawModel(*pass.model, pass.modelTransform, 0.0f);
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

    auto drawBox = [&](glm::vec3 center, glm::vec3 size, glm::vec3 color, float highlight, float yawDegrees = 0.0f) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
        if (yawDegrees != 0.0f)
            model = glm::rotate(model, -glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, size);
        glUniformMatrix4fv(locModel_, 1, GL_FALSE, glm::value_ptr(model));
        glUniform3fv(locColor_, 1, glm::value_ptr(color));
        glUniform1f(locHighlight_, highlight);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    };

    for (std::size_t i = 0; i < scene.boxes.size(); ++i) {
        const Box& b = scene.boxes[i];
        drawBox(b.position, b.size, b.color, static_cast<int>(i) == view.selectedObject ? 1.0f : 0.0f);
    }
    for (std::size_t i = 0; i < scene.models.size(); ++i) {
        const Model& model = scene.models[i];
        const bool selected = static_cast<int>(scene.boxes.size() + i) == view.selectedObject;
        drawModel(model, glm::translate(glm::mat4(1.0f), model.position), selected ? 1.0f : 0.0f);
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
