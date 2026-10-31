#include "engine/Ui2D.h"

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <stb_image.h> // implementation lives in GltfLoader.cpp

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

namespace engine {

namespace {
constexpr int kAtlasSize = 2048;
constexpr float kBakedSize = 64.0f; // glyphs are baked at this height and scaled when drawn

const char* kVertex = R"(#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aMode;
uniform vec2 uScreen;
out vec2 vUv;
out vec4 vColor;
flat out float vMode;
void main() {
    vUv = aUv;
    vColor = aColor;
    vMode = aMode;
    gl_Position = vec4(aPos / uScreen * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
})";

const char* kFragment = R"(#version 450 core
layout(binding = 0) uniform sampler2D uTexture;
in vec2 vUv;
in vec4 vColor;
flat in float vMode;
out vec4 fragColor;
void main() {
    vec4 t = texture(uTexture, vUv);
    if (vMode > 1.5)
        fragColor = vec4(vColor.rgb, vColor.a * t.r); // font atlas: coverage in red
    else if (vMode > 0.5)
        fragColor = t * vColor;
    else
        fragColor = vColor;
})";

unsigned compile(GLenum type, const char* source, std::string* error)
{
    const unsigned shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        if (error)
            *error = std::string("UI shader: ") + log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Next code point of a UTF-8 string (invalid bytes come out as '?').
unsigned nextCodepoint(const std::string& s, std::size_t& i)
{
    const auto c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80)
        return c;
    int extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (extra < 0)
        return '?';
    unsigned cp = c & (0x3F >> extra);
    for (; extra > 0 && i < s.size(); --extra)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    return cp;
}
} // namespace

struct Ui2D::Font {
    std::vector<unsigned char> data;
    stbtt_packedchar ascii[95];     // 32..126
    stbtt_packedchar latin[224];    // 160..383 (Latin-1 + Latin Extended-A: Czech letters)
    unsigned texture = 0;
    float ascent = 0.0f;            // at kBakedSize

    const stbtt_packedchar* glyph(unsigned cp) const
    {
        if (cp >= 32 && cp < 127)
            return &ascii[cp - 32];
        if (cp >= 160 && cp < 384)
            return &latin[cp - 160];
        return &ascii['?' - 32];
    }
};

Ui2D::Ui2D() = default;
Ui2D::~Ui2D() { shutdown(); }

bool Ui2D::init(const std::filesystem::path& regular, const std::filesystem::path& bold, std::string* error)
{
    shutdown();
    const std::filesystem::path files[2] = {regular, bold};
    for (int f = 0; f < 2; ++f) {
        auto font = std::make_unique<Font>();
        std::ifstream in(files[f], std::ios::binary);
        font->data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        stbtt_fontinfo info;
        if (font->data.empty() || !stbtt_InitFont(&info, font->data.data(), stbtt_GetFontOffsetForIndex(font->data.data(), 0))) {
            if (error)
                *error = "Cannot read font " + files[f].string();
            return false;
        }
        int ascent = 0, descent = 0, gap = 0;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
        font->ascent = static_cast<float>(ascent) * stbtt_ScaleForPixelHeight(&info, kBakedSize);
        std::vector<unsigned char> atlas(static_cast<std::size_t>(kAtlasSize) * kAtlasSize);
        stbtt_pack_context pack;
        stbtt_PackBegin(&pack, atlas.data(), kAtlasSize, kAtlasSize, 0, 2, nullptr);
        stbtt_PackSetOversampling(&pack, 1, 1);
        stbtt_PackFontRange(&pack, font->data.data(), 0, kBakedSize, 32, 95, font->ascii);
        stbtt_PackFontRange(&pack, font->data.data(), 0, kBakedSize, 160, 224, font->latin);
        stbtt_PackEnd(&pack);
        glCreateTextures(GL_TEXTURE_2D, 1, &font->texture);
        glTextureStorage2D(font->texture, 1, GL_R8, kAtlasSize, kAtlasSize);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTextureSubImage2D(font->texture, 0, 0, 0, kAtlasSize, kAtlasSize, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTextureParameteri(font->texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(font->texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        fonts_[f] = std::move(font);
    }

    const unsigned vs = compile(GL_VERTEX_SHADER, kVertex, error);
    const unsigned fs = vs ? compile(GL_FRAGMENT_SHADER, kFragment, error) : 0;
    if (!fs)
        return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glCreateBuffers(1, &vbo_);
    glCreateVertexArrays(1, &vao_);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    const GLint sizes[4] = {2, 2, 4, 1};
    const GLuint offsets[4] = {offsetof(Vertex, pos), offsetof(Vertex, uv), offsetof(Vertex, color), offsetof(Vertex, mode)};
    for (GLuint i = 0; i < 4; ++i) {
        glEnableVertexArrayAttrib(vao_, i);
        glVertexArrayAttribFormat(vao_, i, sizes[i], GL_FLOAT, GL_FALSE, offsets[i]);
        glVertexArrayAttribBinding(vao_, i, 0);
    }
    const unsigned char white[4] = {255, 255, 255, 255};
    glCreateTextures(GL_TEXTURE_2D, 1, &white_);
    glTextureStorage2D(white_, 1, GL_RGBA8, 1, 1);
    glTextureSubImage2D(white_, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
    ready_ = true;
    return true;
}

void Ui2D::shutdown()
{
    if (!ready_)
        return;
    for (auto& f : fonts_)
        if (f && f->texture)
            glDeleteTextures(1, &f->texture);
    glDeleteTextures(1, &white_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(program_);
    ready_ = false;
}

void Ui2D::begin(int width, int height)
{
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);
    glUniform2f(glGetUniformLocation(program_, "uScreen"), static_cast<float>(width_), static_cast<float>(height_));
    glBindVertexArray(vao_);
    boundTexture_ = white_;
    glBindTextureUnit(0, white_);
}

void Ui2D::end()
{
    flush();
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glUseProgram(0);
}

void Ui2D::flush()
{
    if (vertices_.empty())
        return;
    glNamedBufferData(vbo_, static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)), vertices_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    vertices_.clear();
}

void Ui2D::quad(glm::vec2 p0, glm::vec2 p1, glm::vec2 uv0, glm::vec2 uv1, glm::vec4 color, float mode)
{
    const Vertex a{p0, uv0, color, mode}, b{{p1.x, p0.y}, {uv1.x, uv0.y}, color, mode}, c{p1, uv1, color, mode},
        d{{p0.x, p1.y}, {uv0.x, uv1.y}, color, mode};
    vertices_.insert(vertices_.end(), {a, b, c, a, c, d});
}

void Ui2D::rect(glm::vec2 pos, glm::vec2 size, glm::vec4 color)
{
    quad(pos, pos + size, {0, 0}, {1, 1}, color, 0.0f);
}

void Ui2D::image(unsigned texture, glm::vec2 pos, glm::vec2 size, glm::vec4 tint, glm::vec4 uv)
{
    if (!texture)
        return;
    if (texture != boundTexture_) {
        flush();
        glBindTextureUnit(0, texture);
        boundTexture_ = texture;
    }
    quad(pos, pos + size, {uv.x, uv.y}, {uv.z, uv.w}, tint, 1.0f);
}

void Ui2D::imageQuad(unsigned texture, const glm::vec2 corners[4], const glm::vec2 uvs[4], glm::vec4 tint)
{
    if (!texture)
        return;
    if (texture != boundTexture_) {
        flush();
        glBindTextureUnit(0, texture);
        boundTexture_ = texture;
    }
    const Vertex v[4] = {{corners[0], uvs[0], tint, 1.0f}, {corners[1], uvs[1], tint, 1.0f}, {corners[2], uvs[2], tint, 1.0f},
                         {corners[3], uvs[3], tint, 1.0f}};
    vertices_.insert(vertices_.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
}

void Ui2D::triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec4 color)
{
    vertices_.insert(vertices_.end(), {Vertex{a, {0, 0}, color, 0.0f}, Vertex{b, {0, 0}, color, 0.0f}, Vertex{c, {0, 0}, color, 0.0f}});
}

void Ui2D::text(const std::string& utf8, glm::vec2 pos, float size, glm::vec4 color, bool bold)
{
    const Font* font = fonts_[bold ? 1 : 0].get();
    if (!font)
        return;
    if (font->texture != boundTexture_) {
        flush();
        glBindTextureUnit(0, font->texture);
        boundTexture_ = font->texture;
    }
    const float scale = size / kBakedSize;
    float x = 0.0f;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned cp = nextCodepoint(utf8, i);
        const stbtt_packedchar* g = font->glyph(cp);
        // Glyph box relative to the baseline at the baked size.
        const glm::vec2 p0(x + g->xoff, g->yoff + font->ascent), p1(x + g->xoff2, g->yoff2 + font->ascent);
        quad(pos + p0 * scale, pos + p1 * scale, glm::vec2(g->x0, g->y0) / static_cast<float>(kAtlasSize),
             glm::vec2(g->x1, g->y1) / static_cast<float>(kAtlasSize), color, 2.0f);
        x += g->xadvance;
    }
}

float Ui2D::textWidth(const std::string& utf8, float size, bool bold) const
{
    const Font* font = fonts_[bold ? 1 : 0].get();
    if (!font)
        return 0.0f;
    float x = 0.0f;
    for (std::size_t i = 0; i < utf8.size();)
        x += font->glyph(nextCodepoint(utf8, i))->xadvance;
    return x * size / kBakedSize;
}

unsigned Ui2D::loadImage(const std::filesystem::path& file, int* width, int* height)
{
    std::ifstream in(file, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = bytes.empty() ? nullptr
                                          : stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!pixels)
        return 0;
    unsigned tex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    const int levels = 1 + static_cast<int>(std::floor(std::log2(std::max(w, h))));
    glTextureStorage2D(tex, levels, GL_RGBA8, w, h);
    glTextureSubImage2D(tex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateTextureMipmap(tex);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(pixels);
    if (width)
        *width = w;
    if (height)
        *height = h;
    return tex;
}

void Ui2D::deleteImage(unsigned texture)
{
    if (texture)
        glDeleteTextures(1, &texture);
}

} // namespace engine
