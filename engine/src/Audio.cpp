#include "engine/Audio.h"

#include <miniaudio.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <type_traits>

namespace engine {

namespace {

void setError(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
}

std::string pathToUtf8(const std::filesystem::path& p)
{
    const auto u8 = p.generic_u8string();
    return std::string(u8.begin(), u8.end());
}

std::filesystem::path pathFromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

bool isSoundFile(const std::filesystem::path& file)
{
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".sound";
}

// miniaudio takes char paths everywhere and wchar_t paths on Windows (for non-ASCII names).
template <typename Narrow, typename Wide>
ma_result withPath(const std::filesystem::path& file, Narrow narrow, Wide wide)
{
    if constexpr (std::is_same_v<std::filesystem::path::value_type, wchar_t>)
        return wide(file.c_str());
    else
        return narrow(file.c_str());
}

} // namespace

const char* audioGroupName(AudioGroup group)
{
    switch (group) {
    case AudioGroup::Music: return "music";
    case AudioGroup::Ambient: return "ambient";
    case AudioGroup::Ui: return "ui";
    default: return "sfx";
    }
}

AudioGroup audioGroupFromName(const std::string& name)
{
    for (int g = 0; g < static_cast<int>(AudioGroup::Count); ++g)
        if (name == audioGroupName(static_cast<AudioGroup>(g)))
            return static_cast<AudioGroup>(g);
    return AudioGroup::Sfx;
}

float zoneGain(glm::vec3 boxMin, glm::vec3 boxMax, float fade, glm::vec3 p)
{
    const glm::vec3 outside = glm::max(glm::max(boxMin - p, p - boxMax), glm::vec3(0.0f));
    const float d = glm::length(outside);
    if (d <= 0.0f)
        return 1.0f;
    if (fade <= 0.0f || d >= fade)
        return 0.0f;
    const float t = 1.0f - d / fade;
    return t * t * (3.0f - 2.0f * t);
}

// ------------------------------------------------------------------ .sound files

bool saveSoundFile(const SoundDef& sound, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        setError(error, "Cannot write " + file.string());
        return false;
    }
    const std::filesystem::path dir = std::filesystem::absolute(file).parent_path();
    out << "# Sound: variations (one is picked at random per play) and playback settings\n";
    for (const auto& f : sound.files) {
        const std::filesystem::path rel = std::filesystem::absolute(f).lexically_relative(dir);
        out << "file " << std::quoted(pathToUtf8(rel.empty() ? std::filesystem::absolute(f) : rel)) << '\n';
    }
    out << "volume " << sound.volume << '\n'
        << "pitch " << sound.pitchMin << ' ' << sound.pitchMax << '\n'
        << "distance " << sound.minDistance << ' ' << sound.maxDistance << '\n'
        << "rolloff " << sound.rolloff << '\n'
        << "loop " << (sound.loop ? 1 : 0) << '\n'
        << "stream " << (sound.stream ? 1 : 0) << '\n'
        << "spatial " << (sound.spatial ? 1 : 0) << '\n'
        << "group " << audioGroupName(sound.group) << '\n'
        << "max_instances " << sound.maxInstances << '\n';
    if (!out) {
        setError(error, "Write failed: " + file.string());
        return false;
    }
    return true;
}

bool loadSoundFile(SoundDef& sound, const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        setError(error, "Cannot open " + file.string());
        return false;
    }
    const std::filesystem::path dir = std::filesystem::absolute(file).parent_path();
    SoundDef result;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#')
            continue;
        bool ok = true;
        if (key == "file") {
            std::string path;
            ok = static_cast<bool>(ls >> std::quoted(path));
            if (ok) {
                std::filesystem::path p = pathFromUtf8(path);
                if (p.is_relative())
                    p = dir / p;
                result.files.push_back(p.lexically_normal());
            }
        } else if (key == "volume") {
            ok = static_cast<bool>(ls >> result.volume);
        } else if (key == "pitch") {
            ok = static_cast<bool>(ls >> result.pitchMin >> result.pitchMax);
        } else if (key == "distance") {
            ok = static_cast<bool>(ls >> result.minDistance >> result.maxDistance);
        } else if (key == "rolloff") {
            ok = static_cast<bool>(ls >> result.rolloff);
        } else if (key == "loop" || key == "stream" || key == "spatial") {
            int v = 0;
            ok = static_cast<bool>(ls >> v);
            (key == "loop" ? result.loop : key == "stream" ? result.stream : result.spatial) = v != 0;
        } else if (key == "group") {
            std::string g;
            ok = static_cast<bool>(ls >> g);
            result.group = audioGroupFromName(g);
        } else if (key == "max_instances") {
            ok = static_cast<bool>(ls >> result.maxInstances);
        } // unknown keys are skipped (newer files stay readable)
        if (!ok) {
            setError(error, file.string() + ":" + std::to_string(lineNo) + ": invalid line: " + line);
            return false;
        }
    }
    result.pitchMin = std::max(0.05f, result.pitchMin);
    result.pitchMax = std::max(result.pitchMin, result.pitchMax);
    result.minDistance = std::max(0.01f, result.minDistance);
    result.maxDistance = std::max(result.minDistance, result.maxDistance);
    result.maxInstances = std::max(1, result.maxInstances);
    sound = std::move(result);
    return true;
}

// ------------------------------------------------------------------ playback

struct AudioSystem::Impl {
    struct Voice {
        ma_sound sound{};
        Handle id = 0;
        const SoundDef* def = nullptr;
        float baseVolume = 1.0f;     // def volume * play volume
        std::uint64_t order = 0;     // start order, for stealing the oldest
    };

    ma_engine engine{};
    std::array<ma_sound_group, static_cast<std::size_t>(AudioGroup::Count)> groups{};
    std::array<float, static_cast<std::size_t>(AudioGroup::Count)> groupVolumes{};
    bool initialized = false;
    bool device = false;
    Options options;
    std::vector<std::unique_ptr<Voice>> voices;
    Handle nextId = 1;
    std::uint64_t playCount = 0;
    std::set<std::filesystem::path> registered; // decoded files kept in memory
    std::map<std::filesystem::path, std::shared_ptr<const SoundDef>> cache;
    std::map<const SoundDef*, std::size_t> lastVariation;
    std::mt19937 rng{std::random_device{}()};
    glm::vec3 listener{0.0f};
    std::vector<float> scratch;

    ma_resource_manager* resources() { return ma_engine_get_resource_manager(&engine); }

    Voice* find(Handle h)
    {
        for (auto& v : voices)
            if (v->id == h)
                return v.get();
        return nullptr;
    }

    void release(std::size_t index)
    {
        ma_sound_uninit(&voices[index]->sound);
        voices.erase(voices.begin() + static_cast<std::ptrdiff_t>(index));
    }

    // Frees a slot for a new sound of `def`: the sound's own instance cap first,
    // then the global voice cap (oldest non-looping sound; looping only if all loop).
    void makeRoom(const SoundDef& def)
    {
        auto oldest = [&](auto pred) -> std::ptrdiff_t {
            std::ptrdiff_t best = -1;
            for (std::size_t i = 0; i < voices.size(); ++i)
                if (pred(*voices[i]) && (best < 0 || voices[i]->order < voices[static_cast<std::size_t>(best)]->order))
                    best = static_cast<std::ptrdiff_t>(i);
            return best;
        };
        const auto same = std::count_if(voices.begin(), voices.end(), [&](const auto& v) { return v->def == &def; });
        if (same >= def.maxInstances)
            if (const auto i = oldest([&](const Voice& v) { return v.def == &def; }); i >= 0)
                release(static_cast<std::size_t>(i));
        if (static_cast<int>(voices.size()) >= options.maxVoices) {
            auto i = oldest([](const Voice& v) { return !ma_sound_is_looping(&v.sound); });
            if (i < 0)
                i = oldest([](const Voice&) { return true; });
            if (i >= 0)
                release(static_cast<std::size_t>(i));
        }
    }

    void registerFile(const std::filesystem::path& file, bool stream)
    {
        if (stream || registered.count(file))
            return;
        const ma_uint32 flags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE | MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC;
        const ma_result r = withPath(
            file, [&](const char* p) { return ma_resource_manager_register_file(resources(), p, flags); },
            [&](const wchar_t* p) { return ma_resource_manager_register_file_w(resources(), p, flags); });
        if (r == MA_SUCCESS)
            registered.insert(file);
        else
            std::fprintf(stderr, "Audio: cannot load %s (%s)\n", file.string().c_str(), ma_result_description(r));
    }

    std::size_t pickVariation(const SoundDef& def)
    {
        const std::size_t n = def.files.size();
        if (n <= 1)
            return 0;
        auto it = lastVariation.find(&def);
        std::size_t pick = std::uniform_int_distribution<std::size_t>(0, n - 2)(rng);
        if (it != lastVariation.end() && pick >= it->second)
            ++pick; // skip the previous one
        else if (it == lastVariation.end())
            pick = std::uniform_int_distribution<std::size_t>(0, n - 1)(rng);
        lastVariation[&def] = pick;
        return pick;
    }

    Handle start(const SoundDef& def, const glm::vec3* position, float volume)
    {
        if (!initialized || def.files.empty())
            return 0;
        const std::filesystem::path& file = def.files[pickVariation(def)];
        registerFile(file, def.stream);
        makeRoom(def);

        auto voice = std::make_unique<Voice>();
        const ma_uint32 flags = MA_SOUND_FLAG_ASYNC | (def.stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE) |
                                (position && def.spatial ? 0u : static_cast<ma_uint32>(MA_SOUND_FLAG_NO_SPATIALIZATION));
        ma_sound_group* group = &groups[static_cast<std::size_t>(def.group)];
        const ma_result r = withPath(
            file, [&](const char* p) { return ma_sound_init_from_file(&engine, p, flags, group, nullptr, &voice->sound); },
            [&](const wchar_t* p) { return ma_sound_init_from_file_w(&engine, p, flags, group, nullptr, &voice->sound); });
        if (r != MA_SUCCESS) {
            std::fprintf(stderr, "Audio: cannot play %s (%s)\n", file.string().c_str(), ma_result_description(r));
            return 0;
        }
        ma_sound* s = &voice->sound;
        voice->baseVolume = def.volume * volume;
        ma_sound_set_volume(s, voice->baseVolume);
        const float pitch = def.pitchMax > def.pitchMin
                                ? std::uniform_real_distribution<float>(def.pitchMin, def.pitchMax)(rng)
                                : def.pitchMin;
        ma_sound_set_pitch(s, pitch);
        ma_sound_set_looping(s, def.loop ? MA_TRUE : MA_FALSE);
        if (position && def.spatial) {
            ma_sound_set_position(s, position->x, position->y, position->z);
            ma_sound_set_attenuation_model(s, ma_attenuation_model_inverse);
            ma_sound_set_min_distance(s, def.minDistance);
            ma_sound_set_max_distance(s, def.maxDistance);
            ma_sound_set_rolloff(s, def.rolloff);
            ma_sound_set_doppler_factor(s, 0.0f); // no velocities are tracked yet
        }
        ma_sound_start(s);
        voice->id = nextId++;
        if (nextId == 0)
            nextId = 1;
        voice->def = &def;
        voice->order = playCount++;
        const Handle id = voice->id;
        voices.push_back(std::move(voice));
        return id;
    }
};

AudioSystem::AudioSystem() : impl_(std::make_unique<Impl>())
{
    impl_->groupVolumes.fill(1.0f);
}

AudioSystem::~AudioSystem()
{
    shutdown();
}

bool AudioSystem::init(const Options& options, std::string* error)
{
    shutdown();
    Impl& m = *impl_;
    m.options = options;
    m.options.maxVoices = std::max(1, options.maxVoices);
    auto tryInit = [&](bool noDevice) {
        ma_engine_config config = ma_engine_config_init();
        config.channels = 2;
        config.sampleRate = options.sampleRate;
        config.noDevice = noDevice ? MA_TRUE : MA_FALSE;
        config.listenerCount = 1;
        return ma_engine_init(&config, &m.engine);
    };
    ma_result r = MA_ERROR;
    if (!options.noDevice) {
        r = tryInit(false);
        if (r != MA_SUCCESS)
            std::fprintf(stderr, "Audio: no output device (%s), running silent\n", ma_result_description(r));
    }
    m.device = r == MA_SUCCESS;
    if (!m.device)
        r = tryInit(true);
    if (r != MA_SUCCESS) {
        setError(error, std::string("Audio: ") + ma_result_description(r));
        return false;
    }
    for (std::size_t g = 0; g < m.groups.size(); ++g) {
        ma_sound_group_init(&m.engine, 0, nullptr, &m.groups[g]);
        ma_sound_group_set_volume(&m.groups[g], m.groupVolumes[g]);
    }
    ma_engine_listener_set_world_up(&m.engine, 0, 0.0f, 1.0f, 0.0f);
    m.initialized = true;
    return true;
}

void AudioSystem::shutdown()
{
    Impl& m = *impl_;
    if (!m.initialized)
        return;
    while (!m.voices.empty())
        m.release(m.voices.size() - 1);
    for (const auto& file : m.registered)
        withPath(
            file, [&](const char* p) { return ma_resource_manager_unregister_file(m.resources(), p); },
            [&](const wchar_t* p) { return ma_resource_manager_unregister_file_w(m.resources(), p); });
    m.registered.clear();
    for (auto& g : m.groups)
        ma_sound_group_uninit(&g);
    ma_engine_uninit(&m.engine);
    m.initialized = false;
    m.device = false;
}

bool AudioSystem::ready() const { return impl_->initialized; }
bool AudioSystem::hasDevice() const { return impl_->device; }

std::shared_ptr<const SoundDef> AudioSystem::loadSound(const std::filesystem::path& file, std::string* error)
{
    const std::filesystem::path key = std::filesystem::absolute(file).lexically_normal();
    if (auto it = impl_->cache.find(key); it != impl_->cache.end())
        return it->second;
    auto def = std::make_shared<SoundDef>();
    if (isSoundFile(key)) {
        if (!loadSoundFile(*def, key, error))
            return nullptr;
    } else {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(key, ec)) {
            setError(error, "Sound not found: " + key.string());
            return nullptr;
        }
        def->files.push_back(key);
    }
    impl_->cache.emplace(key, def);
    return def;
}

void AudioSystem::preload(const SoundDef& sound)
{
    if (!impl_->initialized)
        return;
    for (const auto& f : sound.files)
        impl_->registerFile(f, sound.stream);
}

AudioSystem::Handle AudioSystem::play(const SoundDef& sound, glm::vec3 position, float volume)
{
    return impl_->start(sound, &position, volume);
}

AudioSystem::Handle AudioSystem::play2D(const SoundDef& sound, float volume)
{
    return impl_->start(sound, nullptr, volume);
}

void AudioSystem::stop(Handle handle, float fadeSeconds)
{
    Impl& m = *impl_;
    for (std::size_t i = 0; i < m.voices.size(); ++i) {
        if (m.voices[i]->id != handle)
            continue;
        if (fadeSeconds > 0.0f)
            ma_sound_stop_with_fade_in_milliseconds(&m.voices[i]->sound, static_cast<ma_uint64>(fadeSeconds * 1000.0f));
        else
            m.release(i);
        return;
    }
}

void AudioSystem::stopAll()
{
    while (!impl_->voices.empty())
        impl_->release(impl_->voices.size() - 1);
}

bool AudioSystem::playing(Handle handle) const
{
    const Impl::Voice* v = impl_->find(handle);
    return v && ma_sound_is_playing(&v->sound);
}

void AudioSystem::setPosition(Handle handle, glm::vec3 p)
{
    if (Impl::Voice* v = impl_->find(handle))
        ma_sound_set_position(&v->sound, p.x, p.y, p.z);
}

void AudioSystem::setVolume(Handle handle, float volume, float fadeSeconds)
{
    Impl::Voice* v = impl_->find(handle);
    if (!v)
        return;
    // The sound's own volume stays at its base; the fader carries this relative volume.
    ma_sound_set_fade_in_milliseconds(&v->sound, -1.0f, std::max(0.0f, volume),
                                      static_cast<ma_uint64>(std::max(0.0f, fadeSeconds) * 1000.0f));
}

void AudioSystem::setListener(glm::vec3 position, glm::vec3 forward, glm::vec3 up)
{
    Impl& m = *impl_;
    m.listener = position;
    if (!m.initialized)
        return;
    ma_engine_listener_set_position(&m.engine, 0, position.x, position.y, position.z);
    if (glm::dot(forward, forward) > 1e-8f)
        ma_engine_listener_set_direction(&m.engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m.engine, 0, up.x, up.y, up.z);
}

glm::vec3 AudioSystem::listenerPosition() const { return impl_->listener; }

void AudioSystem::setMasterVolume(float volume)
{
    if (impl_->initialized)
        ma_engine_set_volume(&impl_->engine, std::max(0.0f, volume));
}

float AudioSystem::masterVolume() const
{
    return impl_->initialized ? ma_engine_get_volume(&impl_->engine) : 1.0f;
}

void AudioSystem::setGroupVolume(AudioGroup group, float volume)
{
    const auto g = static_cast<std::size_t>(group);
    if (g >= impl_->groups.size())
        return;
    impl_->groupVolumes[g] = std::max(0.0f, volume);
    if (impl_->initialized)
        ma_sound_group_set_volume(&impl_->groups[g], impl_->groupVolumes[g]);
}

float AudioSystem::groupVolume(AudioGroup group) const
{
    const auto g = static_cast<std::size_t>(group);
    return g < impl_->groupVolumes.size() ? impl_->groupVolumes[g] : 1.0f;
}

void AudioSystem::update(float dt)
{
    Impl& m = *impl_;
    if (!m.initialized)
        return;
    if (!m.device && dt > 0.0f) {
        const auto frames = static_cast<std::size_t>(std::min(dt, 0.25f) * static_cast<float>(sampleRate()));
        m.scratch.resize(frames * 2);
        render(m.scratch.data(), frames);
    }
    // A sound that ended (or finished its stop fade) is freed. One that is still
    // loading counts as playing, since it was started.
    for (std::size_t i = m.voices.size(); i-- > 0;)
        if (!ma_sound_is_playing(&m.voices[i]->sound))
            m.release(i);
}

std::size_t AudioSystem::render(float* out, std::size_t frames)
{
    Impl& m = *impl_;
    if (!m.initialized || m.device || frames == 0)
        return 0;
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&m.engine, out, frames, &read);
    return static_cast<std::size_t>(read);
}

int AudioSystem::activeVoices() const { return static_cast<int>(impl_->voices.size()); }

std::uint32_t AudioSystem::sampleRate() const
{
    return impl_->initialized ? ma_engine_get_sample_rate(&impl_->engine) : impl_->options.sampleRate;
}

} // namespace engine
