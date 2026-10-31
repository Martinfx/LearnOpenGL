#pragma once
#include "engine/Renderer.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace engine {

// Screen-space 2D drawing for menus and the loading screen: filled rectangles,
// images and UTF-8 text (TrueType font baked into an atlas, Latin + Czech).
// Coordinates are pixels, origin at the top left. Call with a current GL context.
class Ui2D {
public:
    Ui2D();
    Ui2D(const Ui2D&) = delete;
    Ui2D& operator=(const Ui2D&) = delete;
    ~Ui2D();

    // `regular` and `bold` are .ttf files; false + error when a font cannot be read.
    bool init(const std::filesystem::path& regular, const std::filesystem::path& bold, std::string* error = nullptr);
    void shutdown();

    void begin(int width, int height); // binds the default framebuffer, alpha blending on
    void end();

    void rect(glm::vec2 pos, glm::vec2 size, glm::vec4 color);
    // Image stretched over the rectangle; `uv` = (u0, v0, u1, v1).
    void image(unsigned texture, glm::vec2 pos, glm::vec2 size, glm::vec4 tint = glm::vec4(1.0f),
               glm::vec4 uv = glm::vec4(0, 0, 1, 1));
    // Any four-cornered image (corners and uvs in order: top-left, top-right, bottom-right, bottom-left).
    void imageQuad(unsigned texture, const glm::vec2 corners[4], const glm::vec2 uvs[4], glm::vec4 tint = glm::vec4(1.0f));
    void triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec4 color);
    // Text with its top-left corner at `pos`; `size` is the line height in pixels.
    void text(const std::string& utf8, glm::vec2 pos, float size, glm::vec4 color, bool bold = false);
    float textWidth(const std::string& utf8, float size, bool bold = false) const;

    // Loads an image file into a texture (sRGB, mipmapped); 0 on failure. Width and height are optional.
    static unsigned loadImage(const std::filesystem::path& file, int* width = nullptr, int* height = nullptr);
    static void deleteImage(unsigned texture);

private:
    struct Font;
    void flush();
    void quad(glm::vec2 p0, glm::vec2 p1, glm::vec2 uv0, glm::vec2 uv1, glm::vec4 color, float mode);

    struct Vertex {
        glm::vec2 pos, uv;
        glm::vec4 color;
        float mode; // 0 solid, 1 image, 2 font atlas (alpha)
    };
    std::unique_ptr<Font> fonts_[2];
    std::vector<Vertex> vertices_;
    unsigned program_ = 0, vao_ = 0, vbo_ = 0, white_ = 0;
    unsigned boundTexture_ = 0;
    int width_ = 1, height_ = 1;
    bool ready_ = false;
};

} // namespace engine
