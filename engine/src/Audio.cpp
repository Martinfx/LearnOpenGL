#include "engine/Audio.h"

#include "engine/JobSystem.h"

#include <miniaudio.h>

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx-presets.h>
#include <AL/efx.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
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

// ------------------------------------------------------------------ decoding

bool decodeAudioFile(const std::filesystem::path& file, AudioClipData& out, std::string* error)
{
    auto open = [&](ma_decoder& dec, ma_uint32 channels) {
        ma_decoder_config config = ma_decoder_config_init(ma_format_s16, channels, 0);
        return withPath(
            file, [&](const char* p) { return ma_decoder_init_file(p, &config, &dec); },
            [&](const wchar_t* p) { return ma_decoder_init_file_w(p, &config, &dec); });
    };
    ma_decoder dec;
    ma_result r = open(dec, 0);
    if (r == MA_SUCCESS && dec.outputChannels > 2) { // surround files are mixed down to stereo
        ma_decoder_uninit(&dec);
        r = open(dec, 2);
    }
    if (r != MA_SUCCESS) {
        setError(error, "Cannot decode " + file.string() + " (" + ma_result_description(r) + ")");
        return false;
    }
    out.channels = static_cast<int>(dec.outputChannels);
    out.sampleRate = static_cast<int>(dec.outputSampleRate);
    out.samples.clear();
    ma_uint64 length = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &length) == MA_SUCCESS && length > 0)
        out.samples.reserve(static_cast<std::size_t>(length) * static_cast<std::size_t>(out.channels));
    std::vector<std::int16_t> chunk(4096 * static_cast<std::size_t>(out.channels));
    for (;;) {
        ma_uint64 read = 0;
        ma_decoder_read_pcm_frames(&dec, chunk.data(), 4096, &read);
        if (read == 0)
            break;
        out.samples.insert(out.samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(read * static_cast<ma_uint64>(out.channels)));
    }
    ma_decoder_uninit(&dec);
    if (out.samples.empty()) {
        setError(error, "No audio in " + file.string());
        return false;
    }
    return true;
}

namespace {

struct ReverbPreset {
    const char* name;
    EFXEAXREVERBPROPERTIES props;
};

const std::vector<ReverbPreset>& presets()
{
    static const std::vector<ReverbPreset> table = {
        {"generic", EFX_REVERB_PRESET_GENERIC},
        {"room", EFX_REVERB_PRESET_ROOM},
        {"bathroom", EFX_REVERB_PRESET_BATHROOM},
        {"livingroom", EFX_REVERB_PRESET_LIVINGROOM},
        {"stoneroom", EFX_REVERB_PRESET_STONEROOM},
        {"auditorium", EFX_REVERB_PRESET_AUDITORIUM},
        {"cave", EFX_REVERB_PRESET_CAVE},
        {"arena", EFX_REVERB_PRESET_ARENA},
        {"hangar", EFX_REVERB_PRESET_HANGAR},
        {"hallway", EFX_REVERB_PRESET_HALLWAY},
        {"stonecorridor", EFX_REVERB_PRESET_STONECORRIDOR},
        {"alley", EFX_REVERB_PRESET_ALLEY},
        {"forest", EFX_REVERB_PRESET_FOREST},
        {"city", EFX_REVERB_PRESET_CITY},
        {"mountains", EFX_REVERB_PRESET_MOUNTAINS},
        {"quarry", EFX_REVERB_PRESET_QUARRY},
        {"plain", EFX_REVERB_PRESET_PLAIN},
        {"parkinglot", EFX_REVERB_PRESET_PARKINGLOT},
        {"sewerpipe", EFX_REVERB_PRESET_SEWERPIPE},
        {"underwater", EFX_REVERB_PRESET_UNDERWATER},
        {"factory_smallroom", EFX_REVERB_PRESET_FACTORY_SMALLROOM},
        {"factory_hall", EFX_REVERB_PRESET_FACTORY_HALL},
        {"factory_longpassage", EFX_REVERB_PRESET_FACTORY_LONGPASSAGE},
        {"factory_courtyard", EFX_REVERB_PRESET_FACTORY_COURTYARD},
        {"prefab_workshop", EFX_REVERB_PRESET_PREFAB_WORKSHOP},
        {"prefab_schoolroom", EFX_REVERB_PRESET_PREFAB_SCHOOLROOM},
        {"pipe_large", EFX_REVERB_PRESET_PIPE_LARGE},
        {"outdoors_backyard", EFX_REVERB_PRESET_OUTDOORS_BACKYARD},
        {"outdoors_rollingplains", EFX_REVERB_PRESET_OUTDOORS_ROLLINGPLAINS},
        {"outdoors_deepcanyon", EFX_REVERB_PRESET_OUTDOORS_DEEPCANYON},
        {"outdoors_valley", EFX_REVERB_PRESET_OUTDOORS_VALLEY},
        {"drugged", EFX_REVERB_PRESET_DRUGGED},
    };
    return table;
}

// EFX entry points (loaded at runtime: they are an extension).
struct Efx {
    LPALGENEFFECTS genEffects = nullptr;
    LPALDELETEEFFECTS deleteEffects = nullptr;
    LPALEFFECTI effecti = nullptr;
    LPALEFFECTF effectf = nullptr;
    LPALEFFECTFV effectfv = nullptr;
    LPALGENAUXILIARYEFFECTSLOTS genSlots = nullptr;
    LPALDELETEAUXILIARYEFFECTSLOTS deleteSlots = nullptr;
    LPALAUXILIARYEFFECTSLOTI sloti = nullptr;
    LPALAUXILIARYEFFECTSLOTF slotf = nullptr;
    bool ok = false;

    void load(ALCdevice* device)
    {
        ok = false;
        if (!alcIsExtensionPresent(device, "ALC_EXT_EFX"))
            return;
        auto get = [](const char* name) { return alGetProcAddress(name); };
        genEffects = reinterpret_cast<LPALGENEFFECTS>(get("alGenEffects"));
        deleteEffects = reinterpret_cast<LPALDELETEEFFECTS>(get("alDeleteEffects"));
        effecti = reinterpret_cast<LPALEFFECTI>(get("alEffecti"));
        effectf = reinterpret_cast<LPALEFFECTF>(get("alEffectf"));
        effectfv = reinterpret_cast<LPALEFFECTFV>(get("alEffectfv"));
        genSlots = reinterpret_cast<LPALGENAUXILIARYEFFECTSLOTS>(get("alGenAuxiliaryEffectSlots"));
        deleteSlots = reinterpret_cast<LPALDELETEAUXILIARYEFFECTSLOTS>(get("alDeleteAuxiliaryEffectSlots"));
        sloti = reinterpret_cast<LPALAUXILIARYEFFECTSLOTI>(get("alAuxiliaryEffectSloti"));
        slotf = reinterpret_cast<LPALAUXILIARYEFFECTSLOTF>(get("alAuxiliaryEffectSlotf"));
        ok = genEffects && deleteEffects && effecti && effectf && effectfv && genSlots && deleteSlots && sloti && slotf;
    }
};

ALenum formatOf(int channels) { return channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16; }

} // namespace

const std::vector<std::string>& reverbPresetNames()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n{"none"};
        for (const ReverbPreset& p : presets())
            n.emplace_back(p.name);
        return n;
    }();
    return names;
}

// ------------------------------------------------------------------ playback

struct AudioSystem::Impl {
    enum ClipState { Loading, Decoded, Ready, Failed };
    struct Clip {
        std::atomic<int> state{Loading};
        AudioClipData data; // filled by the decode job, freed after the upload
        ALuint buffer = 0;
    };
    struct Stream {
        ma_decoder decoder{};
        bool open = false;
        bool ended = false;      // decoder reached the end (not looping)
        std::array<ALuint, 4> buffers{};
        ALenum format = AL_FORMAT_MONO16;
        int rate = 48000;
        int channels = 1;
    };
    struct Voice {
        ALuint source = 0;
        Handle id = 0;
        const SoundDef* def = nullptr;
        AudioGroup group = AudioGroup::Sfx;
        float base = 1.0f;         // def volume * play volume
        float fader = 1.0f, fadeTarget = 1.0f, fadeRate = 0.0f;
        bool stopAfterFade = false;
        bool started = false;
        bool loop = false;
        bool mono = false;         // positioned: OpenAL only places mono buffers in 3D
        std::shared_ptr<Clip> clip;
        std::unique_ptr<Stream> stream;
        std::filesystem::path file;
        std::uint64_t order = 0;
    };

    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;
    bool initialized = false;
    bool hasDevice = false;
    bool loopback = false;
    Options options;
    Efx efx;
    ALuint effect = 0, slot = 0;
    std::string reverbName = "none";
    float reverbAmount = 1.0f;
    LPALCRENDERSAMPLESSOFT renderSamples = nullptr;

    std::mutex mutex; // voices (the stream thread refills stream voices)
    std::vector<std::unique_ptr<Voice>> voices;
    std::unordered_map<std::string, std::shared_ptr<Clip>> clips;
    std::array<float, static_cast<std::size_t>(AudioGroup::Count)> groupVolumes{};
    float master = 1.0f;
    Handle nextId = 1;
    std::uint64_t playCount = 0;
    std::map<std::filesystem::path, std::shared_ptr<const SoundDef>> cache;
    std::map<const SoundDef*, std::size_t> lastVariation;
    std::mt19937 rng{std::random_device{}()};
    glm::vec3 listener{0.0f};
    std::vector<float> scratch;

    std::thread streamer;
    std::atomic<bool> stopping{false};

    // ---- clips

    // `mono` mixes stereo files down: OpenAL plays stereo buffers un-positioned,
    // so 3D sounds get their own mono copy (2D uses of the file keep stereo).
    std::shared_ptr<Clip> clipFor(const std::filesystem::path& file, bool mono)
    {
        const std::string key = file.string() + (mono ? "|mono" : "");
        if (auto it = clips.find(key); it != clips.end())
            return it->second;
        auto clip = std::make_shared<Clip>();
        clips.emplace(key, clip);
        JobSystem::global().run([clip, file, mono] {
            std::string error;
            if (decodeAudioFile(file, clip->data, &error)) {
                AudioClipData& d = clip->data;
                if (mono && d.channels == 2) {
                    const std::size_t frames = d.frames();
                    for (std::size_t i = 0; i < frames; ++i)
                        d.samples[i] = static_cast<std::int16_t>((int(d.samples[2 * i]) + int(d.samples[2 * i + 1])) / 2);
                    d.samples.resize(frames);
                    d.channels = 1;
                }
                clip->state = Decoded;
            } else {
                std::fprintf(stderr, "Audio: %s\n", error.c_str());
                clip->state = Failed;
            }
        });
        return clip;
    }

    void uploadClips()
    {
        for (auto& [key, clip] : clips) {
            if (clip->state != Decoded)
                continue;
            alGenBuffers(1, &clip->buffer);
            const AudioClipData& d = clip->data;
            alBufferData(clip->buffer, formatOf(d.channels), d.samples.data(),
                         static_cast<ALsizei>(d.samples.size() * sizeof(std::int16_t)), d.sampleRate);
            clip->data = {}; // the buffer holds it now
            clip->state = alGetError() == AL_NO_ERROR ? Ready : Failed;
        }
    }

    // ---- voices

    Voice* find(Handle h)
    {
        for (auto& v : voices)
            if (v->id == h)
                return v.get();
        return nullptr;
    }

    void release(std::size_t index)
    {
        Voice& v = *voices[index];
        alSourceStop(v.source);
        alSourcei(v.source, AL_BUFFER, 0);
        alDeleteSources(1, &v.source);
        if (v.stream) {
            alDeleteBuffers(static_cast<ALsizei>(v.stream->buffers.size()), v.stream->buffers.data());
            if (v.stream->open)
                ma_decoder_uninit(&v.stream->decoder);
        }
        voices.erase(voices.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void applyGain(Voice& v)
    {
        alSourcef(v.source, AL_GAIN, std::max(0.0f, v.base * v.fader * groupVolumes[static_cast<std::size_t>(v.group)]));
    }

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
            auto i = oldest([](const Voice& v) { return !v.loop; });
            if (i < 0)
                i = oldest([](const Voice&) { return true; });
            if (i >= 0)
                release(static_cast<std::size_t>(i));
        }
    }

    std::size_t pickVariation(const SoundDef& def)
    {
        const std::size_t n = def.files.size();
        if (n <= 1)
            return 0;
        auto it = lastVariation.find(&def);
        std::size_t pick;
        if (it == lastVariation.end()) {
            pick = std::uniform_int_distribution<std::size_t>(0, n - 1)(rng);
        } else {
            pick = std::uniform_int_distribution<std::size_t>(0, n - 2)(rng);
            if (pick >= it->second)
                ++pick; // never the same one twice in a row
        }
        lastVariation[&def] = pick;
        return pick;
    }

    void setSend(ALuint source)
    {
        if (efx.ok && slot)
            alSource3i(source, AL_AUXILIARY_SEND_FILTER, static_cast<ALint>(slot), 0, AL_FILTER_NULL);
    }

    Handle start(const SoundDef& def, const glm::vec3* position, float volume)
    {
        if (!initialized || def.files.empty())
            return 0;
        std::lock_guard lock(mutex);
        const std::filesystem::path& file = def.files[pickVariation(def)];
        makeRoom(def);
        auto voice = std::make_unique<Voice>();
        alGetError();
        alGenSources(1, &voice->source);
        if (alGetError() != AL_NO_ERROR)
            return 0;
        const ALuint s = voice->source;
        voice->def = &def;
        voice->group = def.group;
        voice->base = def.volume * volume;
        voice->loop = def.loop;
        voice->file = file;
        const float pitch = def.pitchMax > def.pitchMin
                                ? std::uniform_real_distribution<float>(def.pitchMin, def.pitchMax)(rng)
                                : def.pitchMin;
        alSourcef(s, AL_PITCH, std::max(0.05f, pitch));
        voice->mono = position && def.spatial;
        if (voice->mono) {
            alSourcei(s, AL_SOURCE_RELATIVE, AL_FALSE);
            alSource3f(s, AL_POSITION, position->x, position->y, position->z);
            alSourcef(s, AL_REFERENCE_DISTANCE, def.minDistance);
            alSourcef(s, AL_MAX_DISTANCE, def.maxDistance);
            alSourcef(s, AL_ROLLOFF_FACTOR, def.rolloff);
        } else { // in the listener's head
            alSourcei(s, AL_SOURCE_RELATIVE, AL_TRUE);
            alSource3f(s, AL_POSITION, 0.0f, 0.0f, 0.0f);
            alSourcef(s, AL_ROLLOFF_FACTOR, 0.0f);
        }
        setSend(s);
        applyGain(*voice);
        if (def.stream) {
            voice->stream = std::make_unique<Stream>();
            alGenBuffers(static_cast<ALsizei>(voice->stream->buffers.size()), voice->stream->buffers.data());
            // The audio thread opens the file and starts it.
        } else {
            voice->clip = clipFor(file, voice->mono);
            alSourcei(s, AL_LOOPING, def.loop ? AL_TRUE : AL_FALSE);
            startIfReady(*voice);
        }
        voice->id = nextId++;
        if (nextId == 0)
            nextId = 1;
        voice->order = playCount++;
        const Handle id = voice->id;
        voices.push_back(std::move(voice));
        return id;
    }

    void startIfReady(Voice& v)
    {
        if (v.started || !v.clip || v.clip->state != Ready)
            return;
        alSourcei(v.source, AL_BUFFER, static_cast<ALint>(v.clip->buffer));
        alSourcePlay(v.source);
        v.started = true;
    }

    // ---- streaming (audio thread)

    // Fills one buffer with ~0.25 s; false at the end of a non-looping stream.
    bool fill(Voice& v, ALuint buffer)
    {
        Stream& st = *v.stream;
        const std::size_t frames = static_cast<std::size_t>(st.rate) / 4;
        std::vector<std::int16_t> pcm(frames * static_cast<std::size_t>(st.channels));
        std::size_t got = 0;
        while (got < frames) {
            ma_uint64 read = 0;
            ma_decoder_read_pcm_frames(&st.decoder, pcm.data() + got * static_cast<std::size_t>(st.channels), frames - got, &read);
            got += static_cast<std::size_t>(read);
            if (read == 0) {
                if (!v.loop)
                    break;
                ma_decoder_seek_to_pcm_frame(&st.decoder, 0);
                ma_uint64 probe = 0;
                if (ma_decoder_get_cursor_in_pcm_frames(&st.decoder, &probe) != MA_SUCCESS)
                    break;
            }
        }
        if (got == 0)
            return false;
        alBufferData(buffer, st.format, pcm.data(), static_cast<ALsizei>(got * static_cast<std::size_t>(st.channels) * sizeof(std::int16_t)),
                     st.rate);
        return true;
    }

    void serviceStreams()
    {
        std::lock_guard lock(mutex);
        for (auto& vp : voices) {
            Voice& v = *vp;
            if (!v.stream || v.stream->ended)
                continue;
            Stream& st = *v.stream;
            if (!st.open) {
                ma_decoder_config config = ma_decoder_config_init(ma_format_s16, v.mono ? 1 : 0, 0);
                ma_result r = withPath(
                    v.file, [&](const char* p) { return ma_decoder_init_file(p, &config, &st.decoder); },
                    [&](const wchar_t* p) { return ma_decoder_init_file_w(p, &config, &st.decoder); });
                if (r == MA_SUCCESS && st.decoder.outputChannels > 2) {
                    ma_decoder_uninit(&st.decoder);
                    config.channels = 2;
                    r = withPath(
                        v.file, [&](const char* p) { return ma_decoder_init_file(p, &config, &st.decoder); },
                        [&](const wchar_t* p) { return ma_decoder_init_file_w(p, &config, &st.decoder); });
                }
                if (r != MA_SUCCESS) {
                    std::fprintf(stderr, "Audio: cannot stream %s (%s)\n", v.file.string().c_str(), ma_result_description(r));
                    st.ended = true;
                    v.started = true; // freed by update() as a stopped voice
                    continue;
                }
                st.open = true;
                st.channels = static_cast<int>(st.decoder.outputChannels);
                st.rate = static_cast<int>(st.decoder.outputSampleRate);
                st.format = formatOf(st.channels);
                int queued = 0;
                for (ALuint b : st.buffers)
                    if (fill(v, b)) {
                        alSourceQueueBuffers(v.source, 1, &b);
                        ++queued;
                    }
                if (queued == 0)
                    st.ended = true;
                alSourcePlay(v.source);
                v.started = true;
                continue;
            }
            ALint processed = 0;
            alGetSourcei(v.source, AL_BUFFERS_PROCESSED, &processed);
            while (processed-- > 0) {
                ALuint b = 0;
                alSourceUnqueueBuffers(v.source, 1, &b);
                if (fill(v, b))
                    alSourceQueueBuffers(v.source, 1, &b);
                else
                    st.ended = true;
            }
            ALint state = 0;
            alGetSourcei(v.source, AL_SOURCE_STATE, &state);
            ALint queued = 0;
            alGetSourcei(v.source, AL_BUFFERS_QUEUED, &queued);
            if (state != AL_PLAYING && state != AL_PAUSED && queued > 0 && !st.ended)
                alSourcePlay(v.source); // ran dry (a slow disk): carry on
        }
    }

    void streamLoop()
    {
        while (!stopping) {
            serviceStreams();
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    }

    // ---- reverb

    void applyReverb()
    {
        if (!efx.ok || !slot)
            return;
        const auto& table = presets();
        const auto it = std::find_if(table.begin(), table.end(), [&](const ReverbPreset& p) { return reverbName == p.name; });
        if (it == table.end()) {
            efx.sloti(slot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
            return;
        }
        const EFXEAXREVERBPROPERTIES& r = it->props;
        efx.effecti(effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
        if (alGetError() == AL_NO_ERROR) {
            efx.effectf(effect, AL_EAXREVERB_DENSITY, r.flDensity);
            efx.effectf(effect, AL_EAXREVERB_DIFFUSION, r.flDiffusion);
            efx.effectf(effect, AL_EAXREVERB_GAIN, r.flGain);
            efx.effectf(effect, AL_EAXREVERB_GAINHF, r.flGainHF);
            efx.effectf(effect, AL_EAXREVERB_GAINLF, r.flGainLF);
            efx.effectf(effect, AL_EAXREVERB_DECAY_TIME, r.flDecayTime);
            efx.effectf(effect, AL_EAXREVERB_DECAY_HFRATIO, r.flDecayHFRatio);
            efx.effectf(effect, AL_EAXREVERB_DECAY_LFRATIO, r.flDecayLFRatio);
            efx.effectf(effect, AL_EAXREVERB_REFLECTIONS_GAIN, r.flReflectionsGain);
            efx.effectf(effect, AL_EAXREVERB_REFLECTIONS_DELAY, r.flReflectionsDelay);
            efx.effectfv(effect, AL_EAXREVERB_REFLECTIONS_PAN, r.flReflectionsPan);
            efx.effectf(effect, AL_EAXREVERB_LATE_REVERB_GAIN, r.flLateReverbGain);
            efx.effectf(effect, AL_EAXREVERB_LATE_REVERB_DELAY, r.flLateReverbDelay);
            efx.effectfv(effect, AL_EAXREVERB_LATE_REVERB_PAN, r.flLateReverbPan);
            efx.effectf(effect, AL_EAXREVERB_ECHO_TIME, r.flEchoTime);
            efx.effectf(effect, AL_EAXREVERB_ECHO_DEPTH, r.flEchoDepth);
            efx.effectf(effect, AL_EAXREVERB_MODULATION_TIME, r.flModulationTime);
            efx.effectf(effect, AL_EAXREVERB_MODULATION_DEPTH, r.flModulationDepth);
            efx.effectf(effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, r.flAirAbsorptionGainHF);
            efx.effectf(effect, AL_EAXREVERB_HFREFERENCE, r.flHFReference);
            efx.effectf(effect, AL_EAXREVERB_LFREFERENCE, r.flLFReference);
            efx.effectf(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, r.flRoomRolloffFactor);
            efx.effecti(effect, AL_EAXREVERB_DECAY_HFLIMIT, r.iDecayHFLimit);
        } else { // standard reverb only
            efx.effecti(effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
            efx.effectf(effect, AL_REVERB_DENSITY, r.flDensity);
            efx.effectf(effect, AL_REVERB_DIFFUSION, r.flDiffusion);
            efx.effectf(effect, AL_REVERB_GAIN, r.flGain);
            efx.effectf(effect, AL_REVERB_GAINHF, r.flGainHF);
            efx.effectf(effect, AL_REVERB_DECAY_TIME, r.flDecayTime);
            efx.effectf(effect, AL_REVERB_DECAY_HFRATIO, r.flDecayHFRatio);
            efx.effectf(effect, AL_REVERB_REFLECTIONS_GAIN, r.flReflectionsGain);
            efx.effectf(effect, AL_REVERB_REFLECTIONS_DELAY, r.flReflectionsDelay);
            efx.effectf(effect, AL_REVERB_LATE_REVERB_GAIN, r.flLateReverbGain);
            efx.effectf(effect, AL_REVERB_LATE_REVERB_DELAY, r.flLateReverbDelay);
            efx.effectf(effect, AL_REVERB_AIR_ABSORPTION_GAINHF, r.flAirAbsorptionGainHF);
            efx.effectf(effect, AL_REVERB_ROOM_ROLLOFF_FACTOR, r.flRoomRolloffFactor);
            efx.effecti(effect, AL_REVERB_DECAY_HFLIMIT, r.iDecayHFLimit);
        }
        efx.sloti(slot, AL_EFFECTSLOT_EFFECT, static_cast<ALint>(effect));
        efx.slotf(slot, AL_EFFECTSLOT_GAIN, std::clamp(reverbAmount, 0.0f, 1.0f));
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
    std::vector<ALCint> attrs;
    if (options.hrtf >= 0) {
        attrs.push_back(ALC_HRTF_SOFT);
        attrs.push_back(options.hrtf ? ALC_TRUE : ALC_FALSE);
    }
    attrs.push_back(ALC_MAX_AUXILIARY_SENDS);
    attrs.push_back(2);
    if (!options.noDevice) {
        m.device = alcOpenDevice(nullptr);
        if (!m.device)
            std::fprintf(stderr, "Audio: no output device, running silent\n");
    }
    m.hasDevice = m.device != nullptr;
    if (!m.device) {
        // Loopback device: OpenAL mixes when asked (update() / render()), nothing is heard.
        auto openLoopback = reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>(alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT"));
        m.renderSamples = reinterpret_cast<LPALCRENDERSAMPLESSOFT>(alcGetProcAddress(nullptr, "alcRenderSamplesSOFT"));
        m.device = openLoopback ? openLoopback(nullptr) : nullptr;
        if (!m.device || !m.renderSamples) {
            setError(error, "Audio: OpenAL has neither an output device nor loopback rendering");
            return false;
        }
        m.loopback = true;
        attrs.insert(attrs.end(), {ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT, ALC_FREQUENCY,
                                   static_cast<ALCint>(options.sampleRate)});
    }
    attrs.push_back(0);
    m.context = alcCreateContext(m.device, attrs.data());
    if (!m.context || !alcMakeContextCurrent(m.context)) {
        setError(error, "Audio: cannot create the OpenAL context");
        if (m.context)
            alcDestroyContext(m.context);
        alcCloseDevice(m.device);
        m.context = nullptr;
        m.device = nullptr;
        return false;
    }
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    alListenerf(AL_GAIN, m.master);
    m.efx.load(m.device);
    if (m.efx.ok) {
        alGetError();
        m.efx.genEffects(1, &m.effect);
        m.efx.genSlots(1, &m.slot);
        if (alGetError() != AL_NO_ERROR)
            m.efx.ok = false;
        else
            m.applyReverb();
    }
    m.initialized = true;
    m.stopping = false;
    m.streamer = std::thread([&m] { m.streamLoop(); });
    return true;
}

void AudioSystem::shutdown()
{
    Impl& m = *impl_;
    if (!m.initialized)
        return;
    m.stopping = true;
    if (m.streamer.joinable())
        m.streamer.join();
    {
        std::lock_guard lock(m.mutex);
        while (!m.voices.empty())
            m.release(m.voices.size() - 1);
    }
    // Decode jobs may still run: wait for them before the buffers go.
    for (auto& [key, clip] : m.clips) {
        while (clip->state == Impl::Loading)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (clip->buffer)
            alDeleteBuffers(1, &clip->buffer);
    }
    m.clips.clear();
    if (m.efx.ok) {
        m.efx.deleteSlots(1, &m.slot);
        m.efx.deleteEffects(1, &m.effect);
    }
    m.slot = m.effect = 0;
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(m.context);
    alcCloseDevice(m.device);
    m.context = nullptr;
    m.device = nullptr;
    m.initialized = false;
    m.hasDevice = false;
    m.loopback = false;
}

bool AudioSystem::ready() const { return impl_->initialized; }
bool AudioSystem::hasDevice() const { return impl_->hasDevice; }

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
    if (!impl_->initialized || sound.stream)
        return;
    for (const auto& f : sound.files)
        impl_->clipFor(f, sound.spatial);
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
    std::lock_guard lock(m.mutex);
    for (std::size_t i = 0; i < m.voices.size(); ++i) {
        Impl::Voice& v = *m.voices[i];
        if (v.id != handle)
            continue;
        if (fadeSeconds > 0.0f && v.started) {
            v.fadeTarget = 0.0f;
            v.fadeRate = v.fader / fadeSeconds;
            v.stopAfterFade = true;
        } else {
            m.release(i);
        }
        return;
    }
}

void AudioSystem::stopAll()
{
    std::lock_guard lock(impl_->mutex);
    while (!impl_->voices.empty())
        impl_->release(impl_->voices.size() - 1);
}

bool AudioSystem::playing(Handle handle) const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->find(handle) != nullptr; // finished voices are freed by update()
}

void AudioSystem::setPosition(Handle handle, glm::vec3 p)
{
    std::lock_guard lock(impl_->mutex);
    if (Impl::Voice* v = impl_->find(handle))
        alSource3f(v->source, AL_POSITION, p.x, p.y, p.z);
}

void AudioSystem::setVolume(Handle handle, float volume, float fadeSeconds)
{
    std::lock_guard lock(impl_->mutex);
    Impl::Voice* v = impl_->find(handle);
    if (!v || v->stopAfterFade)
        return;
    v->fadeTarget = std::max(0.0f, volume);
    if (fadeSeconds > 0.0f) {
        v->fadeRate = std::abs(v->fadeTarget - v->fader) / fadeSeconds;
    } else {
        v->fader = v->fadeTarget;
        v->fadeRate = 0.0f;
        impl_->applyGain(*v);
    }
}

void AudioSystem::setListener(glm::vec3 position, glm::vec3 forward, glm::vec3 up)
{
    Impl& m = *impl_;
    m.listener = position;
    if (!m.initialized)
        return;
    alListener3f(AL_POSITION, position.x, position.y, position.z);
    if (glm::dot(forward, forward) > 1e-8f) {
        const ALfloat orientation[6] = {forward.x, forward.y, forward.z, up.x, up.y, up.z};
        alListenerfv(AL_ORIENTATION, orientation);
    }
}

glm::vec3 AudioSystem::listenerPosition() const { return impl_->listener; }

void AudioSystem::setReverb(const std::string& preset, float amount)
{
    Impl& m = *impl_;
    if (m.reverbName == preset && m.reverbAmount == amount)
        return;
    m.reverbName = preset;
    m.reverbAmount = amount;
    if (m.initialized)
        m.applyReverb();
}

std::string AudioSystem::reverb() const { return impl_->reverbName; }

bool AudioSystem::hrtf() const
{
    if (!impl_->initialized || !alcIsExtensionPresent(impl_->device, "ALC_SOFT_HRTF"))
        return false;
    ALCint status = ALC_FALSE;
    alcGetIntegerv(impl_->device, ALC_HRTF_SOFT, 1, &status);
    return status == ALC_TRUE;
}

std::string AudioSystem::deviceName() const
{
    if (!impl_->initialized)
        return {};
    if (impl_->loopback)
        return "silent (no output device)";
    const ALCchar* name = nullptr;
    if (alcIsExtensionPresent(impl_->device, "ALC_ENUMERATE_ALL_EXT"))
        name = alcGetString(impl_->device, ALC_ALL_DEVICES_SPECIFIER);
    if (!name || !*name)
        name = alcGetString(impl_->device, ALC_DEVICE_SPECIFIER);
    return name ? name : "";
}

void AudioSystem::setMasterVolume(float volume)
{
    impl_->master = std::max(0.0f, volume);
    if (impl_->initialized)
        alListenerf(AL_GAIN, impl_->master);
}

float AudioSystem::masterVolume() const { return impl_->master; }

void AudioSystem::setGroupVolume(AudioGroup group, float volume)
{
    const auto g = static_cast<std::size_t>(group);
    if (g >= impl_->groupVolumes.size())
        return;
    impl_->groupVolumes[g] = std::max(0.0f, volume);
    std::lock_guard lock(impl_->mutex);
    for (auto& v : impl_->voices)
        if (v->group == group)
            impl_->applyGain(*v);
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
    m.uploadClips();
    {
        std::lock_guard lock(m.mutex);
        for (std::size_t i = m.voices.size(); i-- > 0;) {
            Impl::Voice& v = *m.voices[i];
            if (!v.stream) {
                if (v.clip && v.clip->state == Impl::Failed) {
                    m.release(i);
                    continue;
                }
                m.startIfReady(v);
            }
            if (v.fadeRate > 0.0f && v.fader != v.fadeTarget) {
                const float step = v.fadeRate * dt;
                v.fader = v.fader < v.fadeTarget ? std::min(v.fadeTarget, v.fader + step) : std::max(v.fadeTarget, v.fader - step);
                m.applyGain(v);
            }
            if (v.stopAfterFade && v.fader <= 0.0f) {
                m.release(i);
                continue;
            }
            if (!v.started)
                continue;
            ALint state = 0;
            alGetSourcei(v.source, AL_SOURCE_STATE, &state);
            const bool done = state == AL_STOPPED && (!v.stream || v.stream->ended);
            if (done)
                m.release(i);
        }
    }
    if (m.loopback && dt > 0.0f) {
        const auto frames = static_cast<std::size_t>(std::min(dt, 0.25f) * static_cast<float>(sampleRate()));
        m.scratch.resize(frames * 2);
        render(m.scratch.data(), frames);
    }
}

std::size_t AudioSystem::render(float* out, std::size_t frames)
{
    Impl& m = *impl_;
    if (!m.initialized || !m.loopback || frames == 0)
        return 0;
    m.uploadClips();
    {
        std::lock_guard lock(m.mutex);
        for (auto& v : m.voices)
            if (!v->stream)
                m.startIfReady(*v);
    }
    m.renderSamples(m.device, out, static_cast<ALCsizei>(frames));
    return frames;
}

int AudioSystem::activeVoices() const
{
    std::lock_guard lock(impl_->mutex);
    return static_cast<int>(impl_->voices.size());
}

std::uint32_t AudioSystem::sampleRate() const
{
    if (!impl_->initialized)
        return impl_->options.sampleRate;
    ALCint rate = 0;
    alcGetIntegerv(impl_->device, ALC_FREQUENCY, 1, &rate);
    return rate > 0 ? static_cast<std::uint32_t>(rate) : impl_->options.sampleRate;
}

} // namespace engine
