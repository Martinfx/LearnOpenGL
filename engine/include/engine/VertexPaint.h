#pragma once
#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/gtc/type_precision.hpp>
#include <string>
#include <vector>

namespace engine {

struct Model;

// Colours painted on the vertices of one placed model in the editor (Model::paint): red blends
// in the material's layer 1, green layer 2, blue makes the surface wet (darker, glossy).
// One list per mesh part, one entry per vertex; a list that does not match the part's vertex
// count (the model file changed) is ignored.
std::uint64_t newVertexPaintVersion(); // unique across all paints

struct VertexPaint {
    std::vector<std::vector<glm::u8vec4>> parts;
    std::uint64_t version = newVertexPaintVersion(); // new with every edit (the renderer uploads again)
    bool empty() const;        // nothing painted
};

// Fits the lists to the model's parts (new vertices unpainted).
void fitVertexPaint(VertexPaint& paint, const Model& model);
// One brush dab at a world point: vertices within `radius` move towards 255 (or 0 when
// erasing) in `channel` (0..2) by up to `amount` (0..1) with a smooth falloff. True if any changed.
bool paintVertices(VertexPaint& paint, const Model& model, glm::vec3 point, float radius, int channel, float amount, bool erase);
// Nearest hit of a ray with the model's triangles (world space), false if none.
bool raycastModel(const Model& model, glm::vec3 origin, glm::vec3 dir, float maxDistance, float& distance);
// Level file text for one part: run-length encoded RGBA, base64. decode returns false on bad text.
std::string encodeVertexPaint(const std::vector<glm::u8vec4>& colors);
bool decodeVertexPaint(const std::string& text, std::vector<glm::u8vec4>& colors);

} // namespace engine
