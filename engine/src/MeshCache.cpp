#include "engine/MeshCache.h"
#include "engine/TextureCook.h"
#include "engine/Vfs.h"

#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

namespace engine {

namespace {

constexpr char kMagic[8] = {'F', 'P', 'S', 'M', 'E', 'S', 'H', '4'};

std::uint64_t fnv1a(const std::string& s)
{
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s)
        h = (h ^ c) * 1099511628211ull;
    return h;
}

std::string utf8(const std::filesystem::path& p)
{
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}
std::filesystem::path fromUtf8(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

struct Writer {
    std::ofstream& out;
    template <class T>
    void put(const T& v) { out.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
    void str(const std::string& s)
    {
        put(static_cast<std::uint32_t>(s.size()));
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
    template <class T>
    void vec(const std::vector<T>& v)
    {
        put(static_cast<std::uint64_t>(v.size()));
        out.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size() * sizeof(T)));
    }
};

struct Reader {
    std::ifstream& in;
    bool ok = true;
    template <class T>
    void get(T& v) { ok = ok && static_cast<bool>(in.read(reinterpret_cast<char*>(&v), sizeof(T))); }
    void str(std::string& s)
    {
        std::uint32_t n = 0;
        get(n);
        if (!ok || n > (1u << 24))
            return void(ok = false);
        s.resize(n);
        ok = n == 0 || static_cast<bool>(in.read(s.data(), n));
    }
    template <class T>
    void vec(std::vector<T>& v)
    {
        std::uint64_t n = 0;
        get(n);
        if (!ok || n > (1ull << 32))
            return void(ok = false);
        v.resize(n);
        ok = n == 0 || static_cast<bool>(in.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(n * sizeof(T))));
    }
};

std::shared_ptr<const Texture> MeshPart::*const kTextureSlots[] = {
    &MeshPart::texture, &MeshPart::normalTexture, &MeshPart::metalRoughTexture, &MeshPart::occlusionTexture,
    &MeshPart::emissiveTexture};

} // namespace

std::filesystem::path meshCachePath(const std::filesystem::path& source)
{
    const TextureCookConfig& config = textureCookConfig();
    // Texture compression changes which cooked texture files the parts use.
    const std::string key = utf8(std::filesystem::absolute(source).lexically_normal()) + (config.compress ? "|bc" : "|rgba");
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.cmesh", static_cast<unsigned long long>(fnv1a(key)));
    return config.cacheDir.parent_path() / "meshes" / name;
}

bool writeMeshCache(const Model& model, const std::filesystem::path& source, const std::vector<std::filesystem::path>& inputs)
{
    if (!textureCookConfig().diskCache || model.skeleton || model.parts.empty())
        return false;
    // Every texture has to come from a cooked file, or the cache could not bring it back.
    for (const MeshPart& part : model.parts)
        for (auto slot : kTextureSlots)
            if (const auto& t = part.*slot; t && t->cookedFile.empty())
                return false;
    const std::filesystem::path file = meshCachePath(source);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path tmp = file;
    tmp += ".tmp" + std::to_string(newResourceId()); // readers never see half a file
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out)
            return false;
        Writer w{out};
        out.write(kMagic, sizeof(kMagic));
        w.put(static_cast<std::uint32_t>(inputs.size()));
        for (const std::filesystem::path& in : inputs) {
            w.str(utf8(std::filesystem::absolute(in)));
            w.str(vfs().identity(in)); // the layer it came from, size and time
        }
        // Textures once each (parts share them).
        std::map<const Texture*, std::uint32_t> textureIndex;
        std::vector<const Texture*> textures;
        for (const MeshPart& part : model.parts)
            for (auto slot : kTextureSlots)
                if (const Texture* t = (part.*slot).get(); t && !textureIndex.count(t)) {
                    textureIndex[t] = static_cast<std::uint32_t>(textures.size());
                    textures.push_back(t);
                }
        w.put(static_cast<std::uint32_t>(textures.size()));
        for (const Texture* t : textures) {
            w.str(utf8(t->cookedFile));
            w.put(static_cast<std::uint8_t>(t->srgb));
            w.put(static_cast<std::uint8_t>(t->nearest));
            w.put(static_cast<std::uint8_t>(t->mipmaps));
            w.put(static_cast<std::int32_t>(t->wrap));
        }
        w.put(static_cast<std::uint32_t>(model.parts.size()));
        for (const MeshPart& p : model.parts) {
            w.vec(p.positions);
            w.vec(p.normals);
            w.vec(p.texcoords);
            w.vec(p.indices);
            w.vec(p.joints);
            w.vec(p.weights);
            w.vec(p.foliage);
            for (auto slot : kTextureSlots) {
                const Texture* t = (p.*slot).get();
                w.put(t ? static_cast<std::int32_t>(textureIndex[t]) : -1);
            }
            w.put(p.metallic);
            w.put(p.roughness);
            w.put(p.normalScale);
            w.put(p.emissive);
            w.put(p.color);
            w.put(static_cast<std::int32_t>(p.alphaMode));
            w.put(p.alphaCutoff);
            w.put(p.opacity);
            w.put(static_cast<std::uint8_t>(p.doubleSided));
            w.str(p.materialName);
            w.put(static_cast<std::int32_t>(p.node));
            w.put(static_cast<std::int32_t>(p.skin));
        }
        w.put(model.localBounds);
        if (!out)
            return false;
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec)
        std::filesystem::remove(tmp, ec);
    return !ec;
}

bool readMeshCache(Model& model, const std::filesystem::path& source)
{
    if (!textureCookConfig().diskCache)
        return false;
    std::ifstream in(meshCachePath(source), std::ios::binary);
    if (!in)
        return false;
    char magic[8] = {};
    if (!in.read(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(magic)) != 0)
        return false;
    Reader r{in};
    std::uint32_t count = 0;
    r.get(count);
    for (std::uint32_t i = 0; r.ok && i < count; ++i) {
        std::string path, identity;
        r.str(path);
        r.str(identity);
        if (!r.ok || vfs().identity(fromUtf8(path)) != identity)
            return false; // an input changed (or a mod replaces it): cook again
    }
    r.get(count);
    std::vector<std::shared_ptr<const Texture>> textures;
    for (std::uint32_t i = 0; r.ok && i < count; ++i) {
        std::string path;
        std::uint8_t srgb = 0, nearest = 0, mipmaps = 0;
        std::int32_t wrap = 0;
        r.str(path);
        r.get(srgb);
        r.get(nearest);
        r.get(mipmaps);
        r.get(wrap);
        if (!r.ok)
            return false;
        // The same cooked texture in many models: one copy in memory and on the GPU.
        static std::mutex sharedMutex;
        static std::map<std::string, std::weak_ptr<const Texture>> shared;
        const std::string key = path + '|' + std::to_string(srgb) + std::to_string(nearest) + std::to_string(mipmaps) + '|' + std::to_string(wrap);
        {
            std::lock_guard<std::mutex> lock(sharedMutex);
            if (auto it = shared.find(key); it != shared.end())
                if (auto t = it->second.lock()) {
                    textures.push_back(std::move(t));
                    continue;
                }
        }
        CookedTexture info;
        if (!readCookedTextureInfo(fromUtf8(path), info))
            return false; // the texture cache was cleared
        auto tex = std::make_shared<Texture>();
        tex->id = newResourceId();
        tex->srgb = srgb != 0;
        tex->nearest = nearest != 0;
        tex->mipmaps = mipmaps != 0;
        tex->wrap = wrap;
        tex->setCooked(std::move(info), fromUtf8(path));
        {
            std::lock_guard<std::mutex> lock(sharedMutex);
            shared[key] = tex;
        }
        textures.push_back(std::move(tex));
    }
    r.get(count);
    PartList parts;
    for (std::uint32_t i = 0; r.ok && i < count; ++i) {
        MeshPart p;
        p.id = newResourceId();
        r.vec(p.positions);
        r.vec(p.normals);
        r.vec(p.texcoords);
        r.vec(p.indices);
        r.vec(p.joints);
        r.vec(p.weights);
        r.vec(p.foliage);
        for (auto slot : kTextureSlots) {
            std::int32_t t = -1;
            r.get(t);
            if (t >= static_cast<std::int32_t>(textures.size()))
                return false;
            if (t >= 0)
                p.*slot = textures[static_cast<std::size_t>(t)];
        }
        std::int32_t alpha = 0, node = -1, skin = -1;
        std::uint8_t doubleSided = 0;
        r.get(p.metallic);
        r.get(p.roughness);
        r.get(p.normalScale);
        r.get(p.emissive);
        r.get(p.color);
        r.get(alpha);
        r.get(p.alphaCutoff);
        r.get(p.opacity);
        r.get(doubleSided);
        r.str(p.materialName);
        r.get(node);
        r.get(skin);
        p.alphaMode = static_cast<AlphaMode>(alpha);
        p.doubleSided = doubleSided != 0;
        p.node = node;
        p.skin = skin;
        if (r.ok)
            parts.push_back(std::move(p));
    }
    Aabb bounds;
    r.get(bounds);
    if (!r.ok || parts.empty())
        return false;
    model.parts = std::move(parts);
    model.skeleton.reset();
    model.localBounds = bounds;
    model.sourcePath = std::filesystem::absolute(source);
    return true;
}

} // namespace engine
