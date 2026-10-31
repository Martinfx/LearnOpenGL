// glTF 2.0 import via cgltf. Static geometry only: node transforms are baked
// into the vertices, each primitive becomes one MeshPart.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

#include "engine/Scene.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string_view>
#include <glm/common.hpp>
#include <utility>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <limits>
#include <map>

namespace engine {

namespace {

std::atomic<std::uint64_t> nextPartId{1};

// Decodes the base color image of a material (external file or embedded in the .glb).
std::shared_ptr<const Texture> loadImage(const cgltf_image* image, const std::filesystem::path& gltfDir,
                                         std::map<const cgltf_image*, std::shared_ptr<const Texture>>& cache)
{
    if (!image)
        return nullptr;
    if (auto it = cache.find(image); it != cache.end())
        return it->second;

    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = nullptr;
    if (image->buffer_view && image->buffer_view->buffer->data) {
        const auto* bytes = static_cast<const stbi_uc*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
        pixels = stbi_load_from_memory(bytes, static_cast<int>(image->buffer_view->size), &w, &h, &comp, 4);
    } else if (image->uri && std::string_view(image->uri).substr(0, 5) != "data:") {
        std::string uri = image->uri;
        cgltf_decode_uri(uri.data());
        uri.resize(std::char_traits<char>::length(uri.c_str()));
        const std::filesystem::path file = gltfDir / std::filesystem::path(std::u8string(uri.begin(), uri.end()));
        const std::u8string u8 = file.u8string();
        // stbi_load takes a UTF-8 path on Windows only with STBI_WINDOWS_UTF8; read the bytes ourselves.
        if (FILE* f = std::fopen(std::string(u8.begin(), u8.end()).c_str(), "rb")) {
            std::vector<stbi_uc> data;
            stbi_uc buf[65536];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
                data.insert(data.end(), buf, buf + n);
            std::fclose(f);
            pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &comp, 4);
        }
    }
    std::shared_ptr<const Texture> result;
    if (pixels) {
        auto tex = std::make_shared<Texture>();
        tex->id = nextPartId++;
        tex->width = w;
        tex->height = h;
        tex->rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        stbi_image_free(pixels);
        result = std::move(tex);
    }
    cache[image] = result;
    return result;
}

const char* resultText(cgltf_result r)
{
    switch (r) {
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "unknown format";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "glTF 1.0 is not supported";
    default: return "error";
    }
}

float linearToSrgb(float c)
{
    return std::pow(std::max(c, 0.0f), 1.0f / 2.2f);
}

const cgltf_accessor* findAttribute(const cgltf_primitive& prim, cgltf_attribute_type type)
{
    for (cgltf_size i = 0; i < prim.attributes_count; ++i)
        if (prim.attributes[i].type == type && prim.attributes[i].index == 0)
            return prim.attributes[i].data;
    return nullptr;
}

struct LoadContext {
    std::filesystem::path dir;
    std::map<const cgltf_image*, std::shared_ptr<const Texture>> images;
};

bool loadPrimitive(const cgltf_primitive& prim, const glm::mat4& world, MeshPart& part, LoadContext& ctx)
{
    if (prim.type != cgltf_primitive_type_triangles)
        return false; // lines/points are ignored
    const cgltf_accessor* pos = findAttribute(prim, cgltf_attribute_type_position);
    if (!pos || pos->count == 0)
        return false;
    const cgltf_accessor* nrm = findAttribute(prim, cgltf_attribute_type_normal);
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
    const bool flipWinding = glm::determinant(glm::mat3(world)) < 0.0f; // mirrored nodes

    part.positions.resize(pos->count);
    part.normals.assign(pos->count, glm::vec3(0.0f));
    for (cgltf_size i = 0; i < pos->count; ++i) {
        glm::vec3 p;
        cgltf_accessor_read_float(pos, i, glm::value_ptr(p), 3);
        part.positions[i] = glm::vec3(world * glm::vec4(p, 1.0f));
        if (nrm) {
            glm::vec3 n;
            cgltf_accessor_read_float(nrm, i, glm::value_ptr(n), 3);
            part.normals[i] = glm::normalize(normalMatrix * n);
        }
    }

    if (prim.indices) {
        part.indices.resize(prim.indices->count);
        for (cgltf_size i = 0; i < prim.indices->count; ++i)
            part.indices[i] = static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i));
    } else {
        part.indices.resize(pos->count);
        for (cgltf_size i = 0; i < pos->count; ++i)
            part.indices[i] = static_cast<std::uint32_t>(i);
    }
    part.indices.resize(part.indices.size() / 3 * 3);
    if (flipWinding)
        for (std::size_t i = 0; i < part.indices.size(); i += 3)
            std::swap(part.indices[i + 1], part.indices[i + 2]);

    if (!nrm) {
        // No normals exported: accumulate area-weighted face normals.
        for (std::size_t i = 0; i < part.indices.size(); i += 3) {
            const glm::vec3& a = part.positions[part.indices[i]];
            const glm::vec3& b = part.positions[part.indices[i + 1]];
            const glm::vec3& c = part.positions[part.indices[i + 2]];
            const glm::vec3 n = glm::cross(b - a, c - a);
            for (int k = 0; k < 3; ++k)
                part.normals[part.indices[i + k]] += n;
        }
        for (glm::vec3& n : part.normals)
            n = glm::dot(n, n) > 0.0f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    if (prim.material && prim.material->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& pbr = prim.material->pbr_metallic_roughness;
        const float* c = pbr.base_color_factor; // linear
        part.color = {linearToSrgb(c[0]), linearToSrgb(c[1]), linearToSrgb(c[2])};
        const cgltf_accessor* uv = findAttribute(prim, cgltf_attribute_type_texcoord);
        if (pbr.base_color_texture.texture && uv) {
            part.texture = loadImage(pbr.base_color_texture.texture->image, ctx.dir, ctx.images);
            if (part.texture) {
                part.texcoords.resize(uv->count);
                for (cgltf_size i = 0; i < uv->count; ++i)
                    cgltf_accessor_read_float(uv, i, &part.texcoords[i].x, 2);
            }
        }
    }
    part.id = nextPartId++;
    return !part.indices.empty();
}

void loadNode(const cgltf_node& node, Model& model, LoadContext& ctx)
{
    if (node.mesh) {
        glm::mat4 world;
        cgltf_node_transform_world(&node, glm::value_ptr(world));
        for (cgltf_size i = 0; i < node.mesh->primitives_count; ++i) {
            MeshPart part;
            if (loadPrimitive(node.mesh->primitives[i], world, part, ctx))
                model.parts.push_back(std::move(part));
        }
    }
    for (cgltf_size i = 0; i < node.children_count; ++i)
        loadNode(*node.children[i], model, ctx);
}

} // namespace

bool loadModelFile(Model& model, const std::filesystem::path& file, std::string* error)
{
    auto fail = [&](const std::string& message) {
        if (error)
            *error = file.string() + ": " + message;
        return false;
    };

    cgltf_options options{};
    cgltf_data* data = nullptr;
    const std::u8string u8 = file.u8string();
    const std::string path(u8.begin(), u8.end()); // cgltf expects UTF-8
    cgltf_result r = cgltf_parse_file(&options, path.c_str(), &data);
    if (r != cgltf_result_success)
        return fail(resultText(r));
    struct Guard {
        cgltf_data* d;
        ~Guard() { cgltf_free(d); }
    } guard{data};

    r = cgltf_load_buffers(&options, data, path.c_str());
    if (r != cgltf_result_success)
        return fail(std::string("buffers: ") + resultText(r));
    r = cgltf_validate(data);
    if (r != cgltf_result_success)
        return fail(std::string("validation: ") + resultText(r));

    model.parts.clear();
    LoadContext ctx;
    ctx.dir = file.parent_path();
    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if (scene) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
            loadNode(*scene->nodes[i], model, ctx);
    } else {
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
            if (!data->nodes[i].parent)
                loadNode(data->nodes[i], model, ctx);
    }
    if (model.parts.empty())
        return fail("no triangle meshes found");

    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (const MeshPart& part : model.parts)
        for (const glm::vec3& p : part.positions) {
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
    model.localBounds = {lo, hi};
    model.sourcePath = std::filesystem::absolute(file);
    return true;
}

} // namespace engine
