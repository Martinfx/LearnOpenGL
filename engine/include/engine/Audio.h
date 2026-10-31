#pragma once
#include <glm/vec3.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
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
};

bool saveSoundFile(const SoundDef& sound, const std::filesystem::path& file, std::string* error = nullptr);
bool loadSoundFile(SoundDef& sound, const std::filesystem::path& file, std::string* error = nullptr);

struct AudioOptions {
    bool noDevice = false;    // mix in update() instead of on a device (tests, servers, no sound card)
    int maxVoices = 64;       // more sounds at once steal the oldest non-looping one
    std::uint32_t sampleRate = 48000;
};

// Plays sounds on the audio device (miniaudio). Mixing runs on miniaudio's own
// device thread and files decode on its job thread, so play() never blocks on
// disk or decoding: a sound that is not decoded yet starts as soon as it is.
// All member functions are called from one thread (the game or editor main thread).
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

    Handle play(const SoundDef& sound, glm::vec3 position, float volume = 1.0f);
    Handle play2D(const SoundDef& sound, float volume = 1.0f);
    void stop(Handle handle, float fadeSeconds = 0.0f);
    void stopAll();
    bool playing(Handle handle) const;
    void setPosition(Handle handle, glm::vec3 position);
    void setVolume(Handle handle, float volume, float fadeSeconds = 0.0f);

    void setListener(glm::vec3 position, glm::vec3 forward, glm::vec3 up = {0.0f, 1.0f, 0.0f});
    glm::vec3 listenerPosition() const;

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
    std::uint32_t sampleRate() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
