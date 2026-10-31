// glTF 2.0 import via cgltf. Static geometry only: node transforms are baked
// into the vertices, each primitive becomes one MeshPart.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "engine/Scene.h"

#include <algorithm>
#include <atomic>
#include <glm/common.hpp>
#include <utility>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <limits>

namespace engine {

namespace {

std::atomic<std::uint64_t> nextPartId{1};

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

bool loadPrimitive(const cgltf_primitive& prim, const glm::mat4& world, MeshPart& part)
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
        const float* c = prim.material->pbr_metallic_roughness.base_color_factor; // linear
        part.color = {linearToSrgb(c[0]), linearToSrgb(c[1]), linearToSrgb(c[2])};
    }
    part.id = nextPartId++;
    return !part.indices.empty();
}

void loadNode(const cgltf_node& node, Model& model)
{
    if (node.mesh) {
        glm::mat4 world;
        cgltf_node_transform_world(&node, glm::value_ptr(world));
        for (cgltf_size i = 0; i < node.mesh->primitives_count; ++i) {
            MeshPart part;
            if (loadPrimitive(node.mesh->primitives[i], world, part))
                model.parts.push_back(std::move(part));
        }
    }
    for (cgltf_size i = 0; i < node.children_count; ++i)
        loadNode(*node.children[i], model);
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
    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if (scene) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
            loadNode(*scene->nodes[i], model);
    } else {
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
            if (!data->nodes[i].parent)
                loadNode(data->nodes[i], model);
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
