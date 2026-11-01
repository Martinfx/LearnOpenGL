#pragma once
#include <glm/vec3.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine {

// Mixer groups. Every sound plays through one; Master scales them all.
enum class AudioGroup { Sfx, Music, Ambient, Ui, Count };
const char* audioGroupName(AudioGroup group);
AudioGroup audioGroupFromName(const std::string& name); // unknown = Sfx

// Sound asset, stored as a text .sound file (see assets/sounds/). Several files
// are variations: each play picks one at random, never the same one twice in a row.
struct SoundDef {
    std::vector<std::filesystem::path> files; // absolute paths of .wav / .ogg / .mp3 / .flac files
    float volume = 1.0f;          // linear gain
    float pitchMin = 1.0f;        // random pitch range per play (1 = unchanged)
    float pitchMax = 1.0f;
    float minDistance = 1.0f;     // m: full volume inside this distance
    float maxDistance = 60.0f;    // m: no further attenuation beyond
    float rolloff = 1.0f;         // inverse-distance rolloff factor
    bool loop = false;
    bool stream = false;          // decode while playing (music, long ambience) instead of in memory
    bool spatial = true;          // 3D positioned; false = plays in the listener's head (UI, music)
    AudioGroup group = AudioGroup::Sfx;
    int maxInstances = 8;         // more plays at once steal the oldest one
    std::string caption;          // subtitle shown when it plays ("@key" = localized); empty = none
};

// Volume of an ambient zone (a box) heard at `p`: 1 inside, falling smoothly to 0
// at `fade` metres outside it.
float zoneGain(glm::vec3 boxMin, glm::vec3 boxMax, float fade, glm::vec3 p);

bool saveSoundFile(const SoundDef& sound, const std::filesystem::path& file, std::string* error = nullptr);
bool loadSoundFile(SoundDef& sound, const std::filesystem::path& file, std::string* error = nullptr);

struct AudioOptions {
    bool noDevice = false;    // mix in update() instead of on a device (tests, servers, no sound card)
    int maxVoices = 64;       // more sounds at once steal the oldest non-looping one
    std::uint32_t sampleRate = 48000;
    int hrtf = -1;            // -1 = OpenAL decides (on for headphones), 0 = off, 1 = on
};

// Decoded audio file (16-bit, interleaved), e.g. for waveform views.
struct AudioClipData {
    std::vector<std::int16_t> samples;
    int channels = 0;
    int sampleRate = 0;
    std::size_t frames() const { return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0; }
};
// Decodes WAV, Ogg Vorbis, MP3 or FLAC (miniaudio decoders).
bool decodeAudioFile(const std::filesystem::path& file, AudioClipData& out, std::string* error = nullptr);

// Environmental reverb presets (OpenAL EFX), by name: "none", "generic", "room", "hallway",
// "stonecorridor", "alley", "forest", "city", "mountains", "plain", "parkinglot", "sewerpipe",
// "factory_hall", "cave", "hangar", "outdoors_*"... See reverbPresetNames().
const std::vector<std::string>& reverbPresetNames();

// Plays sounds with OpenAL Soft: 3D sources, HRTF, EFX reverb. OpenAL mixes on
// its own device thread; files decode on the job system and long sounds stream
// from the audio thread, so play() never waits for disk or decoding: a sound that
// is not decoded yet starts as soon as it is. Member functions are called from one
// thread (the game or editor main thread).
class AudioSystem {
public:
    using Handle = std::uint32_t; // 0 = no sound

    using Options = AudioOptions;

    AudioSystem();
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // Opens the default output device. Without one (or with noDevice) audio still
    // works and is mixed in update(), so the game runs the same, just silently.
    bool init(const AudioOptions& options, std::string* error = nullptr);
    bool init(std::string* error = nullptr) { return init(AudioOptions{}, error); }
    void shutdown();
    bool ready() const;
    bool hasDevice() const;       // false = silent (no device or noDevice)

    // .sound files are cached by path; a plain audio file gives a default SoundDef
    // (3D, Sfx). Returns null and sets `error` when the file cannot be read.
    std::shared_ptr<const SoundDef> loadSound(const std::filesystem::path& file, std::string* error = nullptr);
    // Starts decoding every variation in the background so the first play has no delay.
    void preload(const SoundDef& sound);
    // Hot reload of a changed .sound (in place, playing voices follow) or audio file
    // (decoded again; voices already playing finish the old one). False if not loaded.
    bool reloadFile(const std::filesystem::path& file);

    Handle play(const SoundDef& sound, glm::vec3 position, float volume = 1.0f);
    Handle play2D(const SoundDef& sound, float volume = 1.0f);
    // Subtitles: called on every play of a sound that has a caption, with its text
    // (unlocalized) and position (null for 2D sounds). Main thread.
    using CaptionListener = std::function<void(const std::string& caption, const glm::vec3* position, const SoundDef& sound)>;
    void setCaptionListener(CaptionListener listener);
    void stop(Handle handle, float fadeSeconds = 0.0f);
    void stopAll();
    bool playing(Handle handle) const;
    void setPosition(Handle handle, glm::vec3 position);
    void setVolume(Handle handle, float volume, float fadeSeconds = 0.0f);

    void setListener(glm::vec3 position, glm::vec3 forward, glm::vec3 up = {0.0f, 1.0f, 0.0f});
    glm::vec3 listenerPosition() const;

    // Room reverb applied to every 3D and 2D sound (EFX). `amount` 0..1 blends it in.
    void setReverb(const std::string& preset, float amount = 1.0f);
    // Two presets at once (crossing from one reverb zone into another, or a zone and the level's).
    void setReverbMix(const std::string& a, float amountA, const std::string& b, float amountB);
    std::string reverb() const;
    std::string reverb(int slot) const; // 0 or 1
    float reverbAmount(int slot) const;

    // Ducking: turns a group down to `gain` within 50 ms, holds it for `holdSeconds`
    // and brings it back over `releaseSeconds` (ambience under gunfire). The deepest wins.
    void duck(AudioGroup group, float gain, float holdSeconds, float releaseSeconds = 1.0f);
    float duckGain(AudioGroup group) const;

    // Occlusion: how much level geometry blocks the way from the listener to a 3D source,
    // 0 = clear .. 1 = behind a wall. Asked when a sound starts and a few times per second;
    // occluded sounds get quieter and duller (low-pass). Unset = no occlusion.
    using OcclusionTest = std::function<float(glm::vec3 listener, glm::vec3 source)>;
    void setOcclusionTest(OcclusionTest test);
    float occlusion(Handle handle) const; // current (smoothed) value of a playing sound
    bool hrtf() const;              // HRTF active (binaural 3D for headphones)
    std::string deviceName() const;

    void setMasterVolume(float volume);
    float masterVolume() const;
    void setGroupVolume(AudioGroup group, float volume);
    float groupVolume(AudioGroup group) const;

    // Frees finished voices; without a device it also mixes `dt` seconds so
    // sounds advance and end as they would on a device. Call once per frame.
    void update(float dt);
    // Mixes `frames` stereo frames into `out` (interleaved float). Only without a
    // device; used by tests to check what would be heard.
    std::size_t render(float* out, std::size_t frames);

    int activeVoices() const;
    std::size_t memoryBytes() const; // decoded sound clips (in OpenAL buffers); call on the main thread
    std::uint32_t sampleRate() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
