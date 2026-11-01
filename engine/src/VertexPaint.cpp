#include "engine/VertexPaint.h"
#include "engine/Scene.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>

namespace engine {

std::uint64_t newVertexPaintVersion()
{
    static std::atomic<std::uint64_t> counter{0};
    return ++counter;
}

bool VertexPaint::empty() const
{
    for (const auto& part : parts)
        for (const glm::u8vec4& c : part)
            if (c.r || c.g || c.b || c.a)
                return false;
    return true;
}

void fitVertexPaint(VertexPaint& paint, const Model& model)
{
    paint.parts.resize(model.parts.size());
    for (std::size_t i = 0; i < model.parts.size(); ++i)
        paint.parts[i].resize(model.parts[i].positions.size(), glm::u8vec4(0));
}

bool paintVertices(VertexPaint& paint, const Model& model, glm::vec3 point, float radius, int channel, float amount, bool erase)
{
    if (radius <= 0.0f || channel < 0 || channel > 2 || amount <= 0.0f)
        return false;
    fitVertexPaint(paint, model);
    const glm::mat4 toWorld = model.transform();
    const glm::mat4 toLocal = glm::inverse(toWorld);
    const glm::vec3 local = glm::vec3(toLocal * glm::vec4(point, 1.0f));
    const float localRadius = radius / std::max(model.scale, 1e-4f);
    bool changed = false;
    for (std::size_t p = 0; p < model.parts.size(); ++p) {
        const MeshPart& part = model.parts[p];
        if (part.skin >= 0 || part.node >= 0)
            continue; // animated parts move away from their rest pose
        std::vector<glm::u8vec4>& colors = paint.parts[p];
        for (std::size_t v = 0; v < part.positions.size(); ++v) {
            const float d = glm::distance(part.positions[v], local);
            if (d >= localRadius)
                continue;
            const float x = d / localRadius;
            const float w = amount * (1.0f - x * x) * (1.0f - x * x); // smooth falloff to the edge
            std::uint8_t& c = colors[v][channel];
            const float target = erase ? 0.0f : 255.0f;
            const auto next = static_cast<std::uint8_t>(std::lround(c + (target - c) * std::min(w, 1.0f)));
            // Small steps would round back to the old value: move at least one unit.
            const std::uint8_t moved = next != c ? next : static_cast<std::uint8_t>(erase ? (c > 0 ? c - 1 : 0) : (c < 255 ? c + 1 : 255));
            if (moved != c && w > 0.02f) {
                c = moved;
                changed = true;
            }
        }
    }
    if (changed)
        paint.version = newVertexPaintVersion();
    return changed;
}

bool raycastModel(const Model& model, glm::vec3 origin, glm::vec3 dir, float maxDistance, float& distance)
{
    const glm::mat4 toLocal = glm::inverse(model.transform());
    const glm::vec3 o = glm::vec3(toLocal * glm::vec4(origin, 1.0f));
    const glm::vec3 d = glm::vec3(toLocal * glm::vec4(dir, 0.0f)); // length = 1 / scale: t stays in world units
    float best = maxDistance;
    bool hit = false;
    for (const MeshPart& part : model.parts) {
        const auto& pos = part.positions;
        for (std::size_t i = 0; i + 2 < part.indices.size(); i += 3) {
            const std::uint32_t a = part.indices[i], b = part.indices[i + 1], c = part.indices[i + 2];
            if (a >= pos.size() || b >= pos.size() || c >= pos.size())
                continue;
            // Möller-Trumbore, both faces.
            const glm::vec3 e1 = pos[b] - pos[a], e2 = pos[c] - pos[a];
            const glm::vec3 pv = glm::cross(d, e2);
            const float det = glm::dot(e1, pv);
            if (std::abs(det) < 1e-12f)
                continue;
            const float inv = 1.0f / det;
            const glm::vec3 tv = o - pos[a];
            const float u = glm::dot(tv, pv) * inv;
            if (u < 0.0f || u > 1.0f)
                continue;
            const glm::vec3 qv = glm::cross(tv, e1);
            const float v = glm::dot(d, qv) * inv;
            if (v < 0.0f || u + v > 1.0f)
                continue;
            const float t = glm::dot(e2, qv) * inv;
            if (t > 0.0f && t < best) {
                best = t;
                hit = true;
            }
        }
    }
    if (hit)
        distance = best;
    return hit;
}

namespace {
const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void putVarint(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    while (v >= 0x80) {
        out.push_back(static_cast<std::uint8_t>(v | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(v));
}
} // namespace

std::string encodeVertexPaint(const std::vector<glm::u8vec4>& colors)
{
    // Runs of equal colours: varint count, then 4 bytes.
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i < colors.size();) {
        std::size_t j = i + 1;
        while (j < colors.size() && colors[j] == colors[i])
            ++j;
        putVarint(bytes, static_cast<std::uint32_t>(j - i));
        bytes.insert(bytes.end(), {colors[i].r, colors[i].g, colors[i].b, colors[i].a});
        i = j;
    }
    std::string text;
    text.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t n = static_cast<std::uint32_t>(bytes[i]) << 16 |
                                (i + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0u) |
                                (i + 2 < bytes.size() ? bytes[i + 2] : 0u);
        text += kBase64[n >> 18 & 63];
        text += kBase64[n >> 12 & 63];
        text += i + 1 < bytes.size() ? kBase64[n >> 6 & 63] : '=';
        text += i + 2 < bytes.size() ? kBase64[n & 63] : '=';
    }
    return text;
}

bool decodeVertexPaint(const std::string& text, std::vector<glm::u8vec4>& colors)
{
    std::vector<std::uint8_t> bytes;
    std::uint32_t acc = 0;
    int bits = 0;
    for (char ch : text) {
        if (ch == '=')
            break;
        const char* at = std::strchr(kBase64, ch);
        if (!at || !ch)
            return false;
        acc = acc << 6 | static_cast<std::uint32_t>(at - kBase64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<std::uint8_t>(acc >> bits));
        }
    }
    colors.clear();
    for (std::size_t i = 0; i < bytes.size();) {
        std::uint32_t count = 0;
        int shift = 0;
        while (i < bytes.size()) {
            const std::uint8_t b = bytes[i++];
            count |= static_cast<std::uint32_t>(b & 0x7f) << shift;
            shift += 7;
            if (!(b & 0x80))
                break;
            if (shift > 28)
                return false;
        }
        if (i + 4 > bytes.size() || count == 0 || colors.size() + count > (1u << 26))
            return false;
        colors.insert(colors.end(), count, glm::u8vec4(bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]));
        i += 4;
    }
    return true;
}

} // namespace engine
