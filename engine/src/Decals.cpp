#include "engine/Decals.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace engine {

DecalPool::DecalPool(std::size_t capacity) : capacity_(std::max<std::size_t>(1, capacity)) { decals_.reserve(capacity_); }

std::size_t DecalPool::memoryBytes() const { return decals_.capacity() * sizeof(Decal); }

void DecalPool::setCapacity(std::size_t capacity)
{
    capacity_ = std::max<std::size_t>(1, capacity);
    if (decals_.size() > capacity_) {
        // Keep the newest.
        std::rotate(decals_.begin(), decals_.begin() + static_cast<std::ptrdiff_t>(next_ % decals_.size()), decals_.end());
        decals_.erase(decals_.begin(), decals_.begin() + static_cast<std::ptrdiff_t>(decals_.size() - capacity_));
        next_ = 0;
    }
}

void DecalPool::add(std::shared_ptr<TextureAsset> texture, int columns, int rows, int row, glm::vec3 position, glm::vec3 normal,
                    float size, float fadeSeconds)
{
    if (size <= 0.0f)
        return;
    columns = std::max(1, columns);
    rows = std::max(1, rows);
    if (row < 0 || row >= rows)
        row = std::uniform_int_distribution<int>(0, rows - 1)(rng_);
    const int column = std::uniform_int_distribution<int>(0, columns - 1)(rng_);
    const float angle = std::uniform_real_distribution<float>(0.0f, 6.2831853f)(rng_);
    const float scale = std::uniform_real_distribution<float>(0.85f, 1.15f)(rng_);
    const glm::mat3 basis = basisFromUp(normal);
    const glm::vec3 x = basis[0] * std::cos(angle) + basis[2] * std::sin(angle);
    const glm::vec3 y = glm::cross(basis[1], x); // x, y, normal right-handed: the quad faces out
    Decal d;
    d.texture = std::move(texture);
    d.columns = columns;
    d.rows = rows;
    d.fade = fadeSeconds;
    // 6 mm off the surface: hit points come from collision shapes, which can sit a few
    // millimetres inside the visible mesh (the rest is polygon offset, see Renderer::drawSprites).
    d.sprite.position = position + basis[1] * 0.006f;
    d.sprite.frame = static_cast<float>(row * columns + column);
    d.sprite.axisX = x * (0.5f * size * scale);
    d.sprite.axisY = y * (0.5f * size * scale);
    d.sprite.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.9f); // the scorch blends a little with the wall
    if (decals_.size() < capacity_) {
        decals_.push_back(std::move(d));
    } else {
        decals_[next_ % capacity_] = std::move(d);
        next_ = (next_ + 1) % capacity_;
    }
}

void DecalPool::clear()
{
    decals_.clear();
    next_ = 0;
}

void DecalPool::update(float dt)
{
    bool expired = false;
    for (Decal& d : decals_) {
        d.age += dt;
        if (d.fade > 0.0f) {
            d.sprite.color.a = std::clamp((d.fade - d.age) / std::min(1.0f, d.fade), 0.0f, 1.0f);
            expired |= d.age >= d.fade;
        }
    }
    if (expired) {
        std::erase_if(decals_, [](const Decal& d) { return d.fade > 0.0f && d.age >= d.fade; });
        next_ = 0;
    }
}

void DecalPool::appendSprites(glm::vec3 eye, float maxDistance, std::vector<SpriteBatch>& out) const
{
    std::map<const TextureAsset*, std::size_t> batchOf;
    const float max2 = maxDistance * maxDistance;
    for (const Decal& d : decals_) {
        const glm::vec3 to = d.sprite.position - eye;
        if (maxDistance > 0.0f && glm::dot(to, to) > max2)
            continue;
        auto [it, inserted] = batchOf.emplace(d.texture.get(), out.size());
        if (inserted) {
            SpriteBatch b;
            b.texture = d.texture;
            b.columns = d.columns;
            b.rows = d.rows;
            b.decal = true;
            b.lit = true;
            out.push_back(std::move(b));
        }
        out[it->second].instances.push_back(d.sprite);
    }
}

} // namespace engine
