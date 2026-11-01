#pragma once
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat3x3.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace engine {

struct TextureAsset;

// ------------------------------------------------------------------ drawing

// One camera-facing or oriented quad: corners are position ± axisX ± axisY.
struct SpriteInstance {
    glm::vec3 position{0.0f};
    float frame = 0.0f;          // flipbook cell (row-major from the top-left), fractions blend
    glm::vec3 axisX{0.5f, 0.0f, 0.0f};
    float pad0 = 0.0f;
    glm::vec3 axisY{0.0f, 0.5f, 0.0f};
    float pad1 = 0.0f;
    glm::vec4 color{1.0f};       // linear rgb (may exceed 1 for glow) and alpha
};

enum class SpriteBlend : std::uint8_t { Alpha, Additive };

// Quads sharing a texture and blend mode, drawn after the opaque scene (the
// renderer sorts batches back to front; instances are drawn in the given order).
struct SpriteBatch {
    std::shared_ptr<TextureAsset> texture; // null = soft round dot
    int columns = 1, rows = 1;             // flipbook layout
    SpriteBlend blend = SpriteBlend::Alpha;
    bool lit = true;                       // shaded by sun and sky (smoke, dust); off for fire and sparks
    bool decal = false;                    // lies on a surface: drawn first, pulled toward the camera against z-fighting
    bool sheltered = false;                // hidden under roofs (rain occlusion map)
    float distance = 0.0f;                 // to the camera, for sorting batches
    std::vector<SpriteInstance> instances;
};

// ------------------------------------------------------------------ effects

struct ParticleKey {
    float t = 0.0f;             // 0..1 of the particle's life
    glm::vec4 value{1.0f};      // size curves use x
};

enum class EmitterShape : std::uint8_t { Point, Sphere, Box, Disc };
const char* emitterShapeName(EmitterShape shape); // "point", "sphere", "box", "disc"

// One stream of particles. Directions are in the effect's space (+Y up, -Z forward);
// spawn() places and rotates the effect in the world.
struct ParticleEmitter {
    std::string name = "Emitter";
    bool enabled = true;
    // Look
    std::filesystem::path texture;          // absolute image, empty = soft round dot
    int columns = 1, rows = 1;              // flipbook cells
    float frameRate = 0.0f;                 // cells per second, 0 = once over the particle's life
    bool randomFrame = false;               // start on a random cell (debris, variants)
    SpriteBlend blend = SpriteBlend::Alpha;
    bool lit = true;
    bool stretch = false;                   // stretched along the velocity (sparks, rain)
    bool sheltered = false;                 // not drawn under roofs (rain, snow)
    float stretchFactor = 0.05f;            // seconds of travel the streak shows
    // Emission
    float rate = 10.0f;                     // particles per second
    int burst = 0;                          // particles at the start
    float duration = 0.0f;                  // seconds the emitter runs, 0 = until stopped
    float delay = 0.0f;                     // seconds before it starts
    int maxParticles = 256;
    EmitterShape shape = EmitterShape::Point;
    glm::vec3 shapeSize{0.1f};              // sphere/disc: x = radius; box: half extents
    // Motion
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float spread = 15.0f;                   // degrees around the direction (180 = all around)
    float speedMin = 1.0f, speedMax = 2.0f; // m/s
    float gravity = 0.0f;                   // multiples of 9.81 m/s² down (smoke: slightly negative = rises)
    float drag = 0.0f;                      // 1/s, velocity lost per second
    glm::vec3 wind{0.0f};                   // m/s added drift
    float attract = 0.0f;                   // m/s² toward the effect's vertical axis (negative = away): vortices, anomalies
    float orbit = 0.0f;                     // m/s² around that axis (swirl), positive = counter-clockwise seen from above
    bool worldSpace = true;                 // particles stay where they were born when the effect moves
    // Appearance over life
    float lifeMin = 1.0f, lifeMax = 2.0f;   // seconds
    float sizeMin = 0.2f, sizeMax = 0.4f;   // m, diameter at birth
    std::vector<ParticleKey> sizeCurve;     // multiplier (x), empty = constant
    std::vector<ParticleKey> colorCurve;    // rgba multiplier, empty = white
    glm::vec3 color{1.0f};                  // sRGB tint
    float intensity = 1.0f;                 // brightness (fire, sparks > 1 glow with bloom)
    float rotationMin = 0.0f, rotationMax = 0.0f;       // degrees at birth
    float spinMin = 0.0f, spinMax = 0.0f;               // degrees per second
};

// A .particle file: several emitters played together (e.g. fire = flames + smoke + embers).
// Edited in the editor's Particle editor.
struct ParticleEffect {
    std::filesystem::path sourcePath;
    std::vector<ParticleEmitter> emitters;
    // Runtime (loaded from the emitters' textures).
    std::vector<std::shared_ptr<TextureAsset>> textures;
    bool looping() const; // some emitter runs until stopped
};

bool loadParticleFile(ParticleEffect& effect, const std::filesystem::path& file, std::string* error = nullptr);
bool saveParticleFile(const ParticleEffect& effect, const std::filesystem::path& file, std::string* error = nullptr);
// Cached per path (textures loaded). Null on error.
std::shared_ptr<ParticleEffect> loadParticleEffect(const std::filesystem::path& file, std::string* error = nullptr);
// Re-reads the emitter textures after an edit (editor).
void loadParticleTextures(ParticleEffect& effect);

float evalCurve(const std::vector<ParticleKey>& curve, float t, float fallback = 1.0f);    // x
glm::vec4 evalColorCurve(const std::vector<ParticleKey>& curve, float t);                 // rgba

// Runs effects: spawn, move, stop. update() simulates (instances in parallel on
// the job system when there are many); appendSprites() fills render batches.
class ParticleSystem {
public:
    using Handle = std::uint32_t; // 0 = none

    ParticleSystem();
    ~ParticleSystem();
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    // `rotation` turns the effect's +Y/-Z into the world (e.g. a surface normal or the muzzle).
    Handle spawn(std::shared_ptr<const ParticleEffect> effect, glm::vec3 position, const glm::mat3& rotation = glm::mat3(1.0f));
    void setTransform(Handle h, glm::vec3 position, const glm::mat3& rotation = glm::mat3(1.0f));
    void setRateScale(Handle h, float scale); // multiplies every emitter's rate (rain by weather)
    void stop(Handle h);   // stops emitting; live particles finish
    void kill(Handle h);   // removes at once
    void clear();
    bool alive(Handle h) const;

    void update(float dt);
    void appendSprites(glm::vec3 cameraPosition, const glm::mat3& cameraBasis, std::vector<struct SpriteBatch>& out) const;

    int instanceCount() const;
    int particleCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Rotation whose +Y points along `up` (effects spawned on a surface face out of it).
glm::mat3 basisFromUp(glm::vec3 up);

} // namespace engine
