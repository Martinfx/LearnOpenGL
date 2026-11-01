#include "engine/HotReload.h"

#include "engine/Audio.h"
#include "engine/Material.h"
#include "engine/Particles.h"
#include "engine/Physics.h"
#include "engine/Scene.h"
#include "engine/Scripting.h"

#include <algorithm>
#include <cctype>

namespace engine {

namespace fs = std::filesystem;

std::vector<std::string> hotReload(const std::vector<fs::path>& changed, const HotReloadTargets& t)
{
    std::vector<std::string> log;
    for (const fs::path& file : changed) {
        const fs::path key = fs::absolute(file).lexically_normal();
        std::string ext = key.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::string name = key.filename().string();
        std::string error;
        if (ext == ".material") {
            if (const auto m = findMaterial(key)) {
                if (reloadMaterial(*m, &error))
                    log.push_back("material " + name);
                else
                    log.push_back("material " + name + ": " + error);
            }
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr") {
            if (reloadTexture(key, &error))
                log.push_back("texture " + name);
            else if (!error.empty())
                log.push_back("texture " + name + ": " + error);
        } else if (ext == ".particle") {
            if (reloadParticleEffect(key, &error))
                log.push_back("particle effect " + name);
            else if (!error.empty())
                log.push_back("particle effect " + name + ": " + error);
        } else if (ext == ".sound" || ext == ".ogg" || ext == ".wav" || ext == ".mp3" || ext == ".flac") {
            if (t.audio && t.audio->reloadFile(key))
                log.push_back("sound " + name);
        } else if (ext == ".lua") {
            if (t.scripts)
                if (const int n = t.scripts->reloadScript(key); n > 0)
                    log.push_back("script " + name + " (" + std::to_string(n) + " entities)");
        } else if ((ext == ".glb" || ext == ".gltf") && t.scene) {
            Scene& scene = *t.scene;
            std::vector<std::size_t> users;
            for (std::size_t i = 0; i < scene.models.size(); ++i) {
                const Model& m = scene.models[i];
                if (!m.deferred && !m.skeleton && !m.sourcePath.empty() && fs::absolute(m.sourcePath).lexically_normal() == key)
                    users.push_back(i);
            }
            if (users.empty())
                continue;
            Model fresh;
            if (!loadModelFile(fresh, key, &error)) {
                log.push_back("model " + name + ": " + error);
                continue;
            }
            for (std::size_t i : users) {
                Model& m = scene.models[i];
                m.parts = fresh.parts; // shared by every copy
                m.localBounds = fresh.localBounds;
                if (t.physics && !m.dynamic) {
                    t.physics->removeStaticModel(i);
                    t.physics->addStaticModel(scene, i);
                }
            }
            log.push_back("model " + name + " (" + std::to_string(users.size()) + " in the level)");
        }
    }
    return log;
}

} // namespace engine
