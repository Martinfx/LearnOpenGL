#include "engine/Renderer.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <cstddef>
#include <vector>

namespace engine {

namespace {

const char* kVertexShader = R"(#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vWorldPos;
out vec3 vNormal;
void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = aNormal; // boxes are only translated and scaled, normals stay axis aligned
    gl_Position = uViewProj * world;
}
)";

// Blinn-Phong with one directional light, a sky/ground ambient term and a
// world-space 1 m grid so the player can judge distance and speed.
const char* kFragmentShader = R"(#version 450 core
in vec3 vWorldPos;
in vec3 vNormal;
uniform vec3 uColor;
uniform vec3 uSunDir;
uniform vec3 uCamera;
uniform float uHighlight;
out vec4 fragColor;

float gridLines(vec3 p, vec3 n) {
    vec2 uv = abs(n.y) > 0.5 ? p.xz : (abs(n.x) > 0.5 ? p.zy : p.xy);
    vec2 g = abs(fract(uv - 0.5) - 0.5) / fwidth(uv);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main() {
    vec3 n = normalize(vNormal);
    vec3 l = normalize(-uSunDir);
    vec3 v = normalize(uCamera - vWorldPos);
    vec3 h = normalize(l + v);

    vec3 albedo = pow(uColor, vec3(2.2)); // colors are authored in sRGB
    vec3 base = albedo * (1.0 - 0.18 * gridLines(vWorldPos, n));
    float diffuse = max(dot(n, l), 0.0);
    float specular = pow(max(dot(n, h), 0.0), 48.0) * 0.25 * step(0.0, diffuse);
    vec3 ambient = mix(vec3(0.20, 0.18, 0.16), vec3(0.32, 0.38, 0.48), n.y * 0.5 + 0.5);

    vec3 color = base * (ambient + diffuse * vec3(1.0, 0.96, 0.88)) + specular;
    color = mix(color, vec3(1.0, 0.75, 0.2), uHighlight * 0.35);

    // Distance fog toward the sky color.
    float fog = 1.0 - exp(-length(uCamera - vWorldPos) * 0.012);
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
    glm::vec3 position;
    glm::vec3 normal;
};

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
            v.insert(v.end(), {{a, normal}, {b, normal}, {d, normal}, {a, normal}, {d, normal}, {e, normal}});
        } else {
            v.insert(v.end(), {{a, normal}, {d, normal}, {b, normal}, {a, normal}, {e, normal}, {d, normal}});
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

    const std::vector<Vertex> cube = unitCube();
    vertexCount_ = static_cast<int>(cube.size());
    glCreateBuffers(1, &vbo_);
    glNamedBufferStorage(vbo_, static_cast<GLsizeiptr>(cube.size() * sizeof(Vertex)), cube.data(), 0);
    glCreateVertexArrays(1, &vao_);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glEnableVertexArrayAttrib(vao_, 0);
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribFormat(vao_, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribBinding(vao_, 0, 0);
    glVertexArrayAttribBinding(vao_, 1, 0);
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
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (program_) glDeleteProgram(program_);
    vao_ = vbo_ = program_ = 0;
}

const Renderer::GpuMesh& Renderer::upload(const MeshPart& part)
{
    auto it = meshes_.find(part.id);
    if (it != meshes_.end()) {
        it->second.used = true;
        return it->second;
    }
    std::vector<Vertex> vertices(part.positions.size());
    for (std::size_t i = 0; i < vertices.size(); ++i)
        vertices[i] = {part.positions[i], part.normals[i]};

    GpuMesh m;
    glCreateBuffers(1, &m.vbo);
    glNamedBufferStorage(m.vbo, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), 0);
    glCreateBuffers(1, &m.ibo);
    glNamedBufferStorage(m.ibo, static_cast<GLsizeiptr>(part.indices.size() * sizeof(std::uint32_t)), part.indices.data(), 0);
    glCreateVertexArrays(1, &m.vao);
    glVertexArrayVertexBuffer(m.vao, 0, m.vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(m.vao, m.ibo);
    glEnableVertexArrayAttrib(m.vao, 0);
    glEnableVertexArrayAttrib(m.vao, 1);
    glVertexArrayAttribFormat(m.vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribFormat(m.vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribBinding(m.vao, 0, 0);
    glVertexArrayAttribBinding(m.vao, 1, 0);
    m.indexCount = static_cast<int>(part.indices.size());
    m.used = true;
    return meshes_.emplace(part.id, m).first->second;
}

void Renderer::releaseUnused()
{
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
    glBindVertexArray(vao_);

    auto drawBox = [&](glm::vec3 center, glm::vec3 size, glm::vec3 color, float highlight) {
        const glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f), center), size);
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
        const glm::mat4 transform = glm::translate(glm::mat4(1.0f), model.position);
        glUniformMatrix4fv(locModel_, 1, GL_FALSE, glm::value_ptr(transform));
        glUniform1f(locHighlight_, selected ? 1.0f : 0.0f);
        for (const MeshPart& part : model.parts) {
            const GpuMesh& mesh = upload(part);
            glUniform3fv(locColor_, 1, glm::value_ptr(part.color));
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        }
    }
    glBindVertexArray(vao_);
    releaseUnused();
    if (view.drawPlayerStart) {
        // Player-sized marker so level designers see the spawn and its scale.
        drawBox(scene.playerStart + glm::vec3(0.0f, 0.9f, 0.0f), glm::vec3(0.6f, 1.8f, 0.6f),
                glm::vec3(0.2f, 0.8f, 0.3f), 0.0f);
    }
    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace engine
