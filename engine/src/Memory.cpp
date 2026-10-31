#include "engine/Memory.h"
#include "engine/Scene.h"

#include <set>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#elif defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <unistd.h>
#else
#include <fstream>
#include <string>
#include <unistd.h>
#endif

namespace engine {

namespace {
std::size_t partBytes(const MeshPart& p)
{
    return p.positions.size() * sizeof(glm::vec3) + p.normals.size() * sizeof(glm::vec3) + p.texcoords.size() * sizeof(glm::vec2) +
           p.indices.size() * sizeof(std::uint32_t) + p.joints.size() * sizeof(glm::u16vec4) + p.weights.size() * sizeof(glm::vec4);
}
} // namespace

SceneMemory sceneMemory(const Scene& scene)
{
    SceneMemory m;
    std::set<std::uint64_t> parts, textures;
    auto addTexture = [&](const Texture* t) {
        if (t && textures.insert(t->id).second) {
            m.textureBytes += t->rgba.capacity() + t->cooked.data.capacity();
            ++m.textures;
        }
    };
    auto addModel = [&](const Model& model) {
        for (const MeshPart& p : model.parts) {
            const std::size_t bytes = partBytes(p);
            if (parts.insert(p.id).second) {
                m.meshBytes += bytes;
                ++m.meshParts;
            } else {
                m.sharedMeshBytes += bytes;
            }
            for (const Texture* t : {p.texture.get(), p.normalTexture.get(), p.metalRoughTexture.get(), p.occlusionTexture.get(),
                                     p.emissiveTexture.get()})
                addTexture(t);
            if (const Material* mat = model.materialFor(p))
                for (const auto& a : mat->maps)
                    if (a)
                        addTexture(a->texture.get());
        }
    };
    for (const Model& model : scene.models) {
        addModel(model);
        if (model.lod)
            addModel(*model.lod);
        for (const Model::FarLod& far : model.farLods)
            if (far.model)
                addModel(*far.model);
    }
    for (const Entity& e : scene.entities)
        if (e.model)
            addModel(*e.model);
    for (const Box& b : scene.boxes)
        if (b.material)
            for (const auto& a : b.material->maps)
                if (a) {
                    addTexture(a->texture.get());
                    m.textureBytes += a->source.capacity(); // editor keeps decoded originals
                }
    return m;
}

std::size_t processMemoryBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc))
        return pmc.WorkingSetSize;
    return 0;
#elif defined(__FreeBSD__)
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(getpid())};
    kinfo_proc info{};
    size_t len = sizeof info;
    if (sysctl(mib, 4, &info, &len, nullptr, 0) == 0)
        return static_cast<std::size_t>(info.ki_rssize) * static_cast<std::size_t>(getpagesize());
    return 0;
#else
    std::ifstream statm("/proc/self/statm");
    std::size_t pages = 0, resident = 0;
    if (statm >> pages >> resident)
        return resident * static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    return 0;
#endif
}

} // namespace engine
