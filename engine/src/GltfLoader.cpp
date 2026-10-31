// glTF 2.0 import via cgltf. Static models get node transforms baked into the
// vertices; animated models keep the node tree, skins and clips (Animation.h).
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

#include "engine/Scene.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <string_view>
#include <glm/common.hpp>
#include <utility>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <limits>
#include <map>

namespace engine {

namespace {

std::atomic<std::uint64_t> nextPartId{1};

// Decodes the base color image of a material (external file or embedded in the .glb).
std::shared_ptr<const Texture> loadImage(const cgltf_image* image, const std::filesystem::path& gltfDir,
                                         std::map<std::pair<const cgltf_image*, bool>, std::shared_ptr<const Texture>>& cache,
                                         bool srgb = true)
{
    if (!image)
        return nullptr;
    if (auto it = cache.find({image, srgb}); it != cache.end())
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
        tex->srgb = srgb;
        tex->rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        stbi_image_free(pixels);
        result = std::move(tex);
    }
    cache[{image, srgb}] = result;
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
    std::map<std::pair<const cgltf_image*, bool>, std::shared_ptr<const Texture>> images;
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

    if (prim.material && prim.material->name)
        part.materialName = prim.material->name;
    const cgltf_accessor* uv = findAttribute(prim, cgltf_attribute_type_texcoord);
    auto textureOf = [&](const cgltf_texture_view& view, bool srgb) -> std::shared_ptr<const Texture> {
        if (!view.texture || !uv)
            return nullptr;
        return loadImage(view.texture->image, ctx.dir, ctx.images, srgb);
    };
    if (const cgltf_material* mat = prim.material) {
        if (mat->has_pbr_metallic_roughness) {
            const cgltf_pbr_metallic_roughness& pbr = mat->pbr_metallic_roughness;
            const float* c = pbr.base_color_factor; // linear
            part.color = {linearToSrgb(c[0]), linearToSrgb(c[1]), linearToSrgb(c[2])};
            part.texture = textureOf(pbr.base_color_texture, true);
            part.metalRoughTexture = textureOf(pbr.metallic_roughness_texture, false);
            part.metallic = pbr.metallic_factor;
            part.roughness = pbr.roughness_factor;
        }
        part.normalTexture = textureOf(mat->normal_texture, false);
        part.normalScale = mat->normal_texture.texture ? mat->normal_texture.scale : 1.0f;
        part.occlusionTexture = textureOf(mat->occlusion_texture, false);
        part.emissiveTexture = textureOf(mat->emissive_texture, true);
        const float* e = mat->emissive_factor;
        part.emissive = {linearToSrgb(e[0]), linearToSrgb(e[1]), linearToSrgb(e[2])};
    }
    if (uv && (part.texture || part.normalTexture || part.metalRoughTexture || part.occlusionTexture || part.emissiveTexture)) {
        part.texcoords.resize(uv->count);
        for (cgltf_size i = 0; i < uv->count; ++i)
            cgltf_accessor_read_float(uv, i, &part.texcoords[i].x, 2);
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

namespace {

struct GltfFile {
    cgltf_data* data = nullptr;
    GltfFile() = default;
    GltfFile(const GltfFile&) = delete;
    GltfFile& operator=(const GltfFile&) = delete;
    ~GltfFile() { cgltf_free(data); }
};

bool openGltf(GltfFile& gltf, const std::filesystem::path& file, std::string* error)
{
    auto fail = [&](const std::string& message) {
        if (error)
            *error = file.string() + ": " + message;
        return false;
    };
    cgltf_options options{};
    const std::u8string u8 = file.u8string();
    const std::string path(u8.begin(), u8.end()); // cgltf expects UTF-8
    cgltf_result r = cgltf_parse_file(&options, path.c_str(), &gltf.data);
    if (r != cgltf_result_success)
        return fail(resultText(r));
    r = cgltf_load_buffers(&options, gltf.data, path.c_str());
    if (r != cgltf_result_success)
        return fail(std::string("buffers: ") + resultText(r));
    r = cgltf_validate(gltf.data);
    if (r != cgltf_result_success)
        return fail(std::string("validation: ") + resultText(r));
    return true;
}

const cgltf_scene* mainScene(const cgltf_data* data)
{
    return data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
}

} // namespace

std::uint64_t newResourceId()
{
    return nextPartId++;
}

std::vector<std::string> Model::materialSlots() const
{
    std::vector<std::string> slots;
    for (const MeshPart& part : parts)
        if (!part.materialName.empty() && std::find(slots.begin(), slots.end(), part.materialName) == slots.end())
            slots.push_back(part.materialName);
    return slots;
}

bool loadModelFile(Model& model, const std::filesystem::path& file, std::string* error)
{
    auto fail = [&](const std::string& message) {
        if (error)
            *error = file.string() + ": " + message;
        return false;
    };

    GltfFile gltf;
    if (!openGltf(gltf, file, error))
        return false;
    const cgltf_data* data = gltf.data;

    model.parts.clear();
    model.skeleton.reset();
    LoadContext ctx;
    ctx.dir = file.parent_path();
    const cgltf_scene* scene = mainScene(data);
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

namespace {

void collectNodes(const cgltf_node* node, int parent, std::vector<const cgltf_node*>& order,
                  std::vector<int>& parents, std::map<const cgltf_node*, int>& index)
{
    if (index.count(node))
        return; // malformed file with a node shared by two parents
    const int self = static_cast<int>(order.size());
    index[node] = self;
    order.push_back(node);
    parents.push_back(parent);
    for (cgltf_size i = 0; i < node->children_count; ++i)
        collectNodes(node->children[i], self, order, parents, index);
}

} // namespace

bool loadAnimatedModelFile(Model& model, const std::filesystem::path& file, std::string* error)
{
    auto fail = [&](const std::string& message) {
        if (error)
            *error = file.string() + ": " + message;
        return false;
    };
    GltfFile gltf;
    if (!openGltf(gltf, file, error))
        return false;
    const cgltf_data* data = gltf.data;

    // Node tree in parent-before-child order.
    std::vector<const cgltf_node*> order;
    std::vector<int> parents;
    std::map<const cgltf_node*, int> index;
    if (const cgltf_scene* scene = mainScene(data)) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
            collectNodes(scene->nodes[i], -1, order, parents, index);
    } else {
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
            if (!data->nodes[i].parent)
                collectNodes(&data->nodes[i], -1, order, parents, index);
    }

    auto skeleton = std::make_shared<Skeleton>();
    skeleton->nodes.resize(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        const cgltf_node& n = *order[i];
        Skeleton::Node& out = skeleton->nodes[i];
        out.name = n.name ? n.name : "";
        out.parent = parents[i];
        if (n.has_matrix) {
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(glm::make_mat4(n.matrix), out.scale, out.rotation, out.translation, skew, perspective);
        } else {
            if (n.has_translation)
                out.translation = glm::make_vec3(n.translation);
            if (n.has_rotation)
                out.rotation = glm::quat(n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2]);
            if (n.has_scale)
                out.scale = glm::make_vec3(n.scale);
        }
    }

    for (cgltf_size s = 0; s < data->skins_count; ++s) {
        const cgltf_skin& skin = data->skins[s];
        Skeleton::Skin out;
        out.joints.resize(skin.joints_count, 0);
        out.inverseBind.assign(skin.joints_count, glm::mat4(1.0f));
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            if (auto it = index.find(skin.joints[j]); it != index.end())
                out.joints[j] = it->second;
            if (skin.inverse_bind_matrices)
                cgltf_accessor_read_float(skin.inverse_bind_matrices, j, glm::value_ptr(out.inverseBind[j]), 16);
        }
        skeleton->skins.push_back(std::move(out));
    }

    for (cgltf_size a = 0; a < data->animations_count; ++a) {
        const cgltf_animation& anim = data->animations[a];
        Skeleton::Clip clip;
        clip.name = anim.name ? anim.name : "Clip " + std::to_string(a + 1);
        for (cgltf_size c = 0; c < anim.channels_count; ++c) {
            const cgltf_animation_channel& ch = anim.channels[c];
            const auto it = index.find(ch.target_node);
            if (it == index.end() || !ch.sampler)
                continue;
            Skeleton::Channel out;
            out.node = it->second;
            int components = 3;
            switch (ch.target_path) {
            case cgltf_animation_path_type_translation: out.path = Skeleton::Path::Translation; break;
            case cgltf_animation_path_type_rotation: out.path = Skeleton::Path::Rotation; components = 4; break;
            case cgltf_animation_path_type_scale: out.path = Skeleton::Path::Scale; break;
            default: continue; // morph target weights are not supported
            }
            const cgltf_animation_sampler& sampler = *ch.sampler;
            out.step = sampler.interpolation == cgltf_interpolation_type_step;
            const bool cubic = sampler.interpolation == cgltf_interpolation_type_cubic_spline;
            const cgltf_size keys = sampler.input->count;
            out.times.resize(keys);
            out.values.resize(keys, glm::vec4(0.0f));
            for (cgltf_size k = 0; k < keys; ++k) {
                cgltf_accessor_read_float(sampler.input, k, &out.times[k], 1);
                // Cubic splines store (in-tangent, value, out-tangent); use the values linearly.
                const cgltf_size element = cubic ? k * 3 + 1 : k;
                cgltf_accessor_read_float(sampler.output, element, glm::value_ptr(out.values[k]), static_cast<cgltf_size>(components));
            }
            if (!out.times.empty())
                clip.duration = std::max(clip.duration, out.times.back());
            clip.channels.push_back(std::move(out));
        }
        skeleton->clips.push_back(std::move(clip));
    }

    // Rest pose, used for mirrored-node winding and the bounds.
    std::vector<glm::mat4> rest;
    evaluatePose(*skeleton, nullptr, rest);

    model.parts.clear();
    LoadContext ctx;
    ctx.dir = file.parent_path();
    for (std::size_t i = 0; i < order.size(); ++i) {
        const cgltf_node& n = *order[i];
        if (!n.mesh)
            continue;
        int skinIndex = -1;
        if (n.skin)
            skinIndex = static_cast<int>(n.skin - data->skins);
        for (cgltf_size p = 0; p < n.mesh->primitives_count; ++p) {
            const cgltf_primitive& prim = n.mesh->primitives[p];
            // Skinned vertices are posed by the joints alone (glTF ignores the mesh node's transform);
            // rigid parts follow their node, so a mirrored node needs flipped winding.
            const bool mirrored = skinIndex < 0 && glm::determinant(glm::mat3(rest[i])) < 0.0f;
            const glm::mat4 winding = glm::scale(glm::mat4(1.0f), glm::vec3(mirrored ? -1.0f : 1.0f, 1.0f, 1.0f));
            MeshPart part;
            if (!loadPrimitive(prim, winding, part, ctx))
                continue;
            if (skinIndex < 0) {
                // A mirror made loadPrimitive flip the triangles; undo the mirror itself.
                if (mirrored) {
                    for (glm::vec3& v : part.positions)
                        v.x = -v.x;
                    for (glm::vec3& v : part.normals)
                        v.x = -v.x;
                }
                part.node = static_cast<int>(i);
            } else {
                const cgltf_accessor* joints = findAttribute(prim, cgltf_attribute_type_joints);
                const cgltf_accessor* weights = findAttribute(prim, cgltf_attribute_type_weights);
                if (!joints || !weights)
                    return fail("skinned mesh without JOINTS_0/WEIGHTS_0");
                const std::size_t count = part.positions.size();
                part.joints.resize(count);
                part.weights.resize(count);
                const std::size_t jointCount = skeleton->skins[static_cast<std::size_t>(skinIndex)].joints.size();
                for (std::size_t v = 0; v < count; ++v) {
                    cgltf_uint j[4] = {};
                    cgltf_accessor_read_uint(joints, v, j, 4);
                    glm::vec4 w(0.0f);
                    cgltf_accessor_read_float(weights, v, glm::value_ptr(w), 4);
                    for (int k = 0; k < 4; ++k)
                        if (j[k] >= jointCount)
                            w[k] = 0.0f, j[k] = 0;
                    const float sum = w.x + w.y + w.z + w.w;
                    part.weights[v] = sum > 0.0f ? w / sum : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                    part.joints[v] = glm::u16vec4(j[0], j[1], j[2], j[3]);
                }
                part.skin = skinIndex;
            }
            model.parts.push_back(std::move(part));
        }
    }
    if (model.parts.empty())
        return fail("no triangle meshes found");

    // Rest-pose bounds.
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    std::vector<std::vector<glm::mat4>> skinning(skeleton->skins.size());
    for (std::size_t s = 0; s < skeleton->skins.size(); ++s)
        skinMatrices(*skeleton, static_cast<int>(s), rest, skinning[s]);
    for (const MeshPart& part : model.parts) {
        for (std::size_t v = 0; v < part.positions.size(); ++v) {
            glm::mat4 m(1.0f);
            if (part.skin >= 0) {
                const auto& jm = skinning[static_cast<std::size_t>(part.skin)];
                const glm::u16vec4 j = part.joints[v];
                const glm::vec4 w = part.weights[v];
                m = jm[j.x] * w.x + jm[j.y] * w.y + jm[j.z] * w.z + jm[j.w] * w.w;
            } else {
                m = rest[static_cast<std::size_t>(part.node)];
            }
            const glm::vec3 p(m * glm::vec4(part.positions[v], 1.0f));
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
    }
    model.localBounds = {lo, hi};
    model.skeleton = std::move(skeleton);
    model.sourcePath = std::filesystem::absolute(file);
    if (model.name.empty() || model.name == "Model")
        model.name = file.stem().string();
    return true;
}

std::shared_ptr<const Model> loadCharacterModel(const std::filesystem::path& file, std::string* error)
{
    static std::mutex mutex; // shared with the editor's background loaders
    const std::lock_guard lock(mutex);
    static std::map<std::filesystem::path, std::weak_ptr<const Model>> cache;
    const std::filesystem::path key = std::filesystem::absolute(file).lexically_normal();
    if (auto it = cache.find(key); it != cache.end())
        if (auto shared = it->second.lock())
            return shared;
    auto model = std::make_shared<Model>();
    if (!loadAnimatedModelFile(*model, key, error))
        return nullptr;
    cache[key] = model;
    return model;
}

} // namespace engine
