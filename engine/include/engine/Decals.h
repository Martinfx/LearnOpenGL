#pragma once
#include "engine/Particles.h"

#include <glm/vec3.hpp>
#include <memory>
#include <random>
#include <vector>

namespace engine {

struct TextureAsset;

// Bullet holes and other marks on level geometry: a fixed-size ring of quads
// (the oldest is reused), drawn as decal sprite batches. Textures are atlases of
// `columns` x `rows` cells; each surface uses one row and a random column
// (assets/textures/decals/README.md).
class DecalPool {
public:
    explicit DecalPool(std::size_t capacity = 256);

    void setCapacity(std::size_t capacity);
    std::size_t capacity() const { return capacity_; }
    // `row` < 0 picks a random row. Size in metres (diameter).
    void add(std::shared_ptr<TextureAsset> texture, int columns, int rows, int row, glm::vec3 position, glm::vec3 normal,
             float size, float fadeSeconds = 0.0f);
    void clear();
    void update(float dt);
    std::size_t count() const { return decals_.size(); }
    std::size_t memoryBytes() const; // the ring of decals (fixed by the capacity)
    void appendSprites(glm::vec3 cameraPosition, float maxDistance, std::vector<SpriteBatch>& out) const;

private:
    struct Decal {
        std::shared_ptr<TextureAsset> texture;
        int columns = 1, rows = 1;
        SpriteInstance sprite;
        float age = 0.0f, fade = 0.0f; // fade > 0: disappears after that many seconds
    };
    std::size_t capacity_;
    std::size_t next_ = 0;
    std::vector<Decal> decals_;
    std::mt19937 rng_{7};
};

} // namespace engine
