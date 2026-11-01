#pragma once
#include "engine/Scene.h"

#include <filesystem>
#include <vector>

namespace engine {

// Cooked static meshes: the parts of a loaded glTF (vertices, indices, materials and the
// cooked texture files they use) in one binary file, read without parsing JSON, decoding
// accessors or flattening the node tree. Next to the texture cache (textureCookConfig):
// <cache>/meshes/<hash>.cmesh. A cache file lists the files it was made from (glTF, .bin
// buffers, images) with their size and time, and is ignored as soon as one changed.
std::filesystem::path meshCachePath(const std::filesystem::path& source);
bool writeMeshCache(const Model& model, const std::filesystem::path& source, const std::vector<std::filesystem::path>& inputs);
bool readMeshCache(Model& model, const std::filesystem::path& source);

} // namespace engine
