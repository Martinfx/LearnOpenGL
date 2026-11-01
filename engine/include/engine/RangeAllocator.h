#pragma once
#include <cstddef>
#include <iterator>
#include <map>
#include <optional>

namespace engine {

// Hands out ranges of a buffer (units are up to the caller: vertices, indices...):
// first fit from a list of free ranges, which merge again when released. Used by
// the renderer's mesh pool (all meshes in one vertex and one index buffer, so many
// meshes can be drawn by one glMultiDrawElementsIndirect).
class RangeAllocator {
public:
    explicit RangeAllocator(std::size_t capacity = 0) { grow(capacity); }

    std::size_t capacity() const { return capacity_; }
    std::size_t used() const { return used_; }

    // Start of a free range of `size`, or nothing when no free range is big enough.
    std::optional<std::size_t> allocate(std::size_t size)
    {
        if (size == 0)
            return 0;
        for (auto it = free_.begin(); it != free_.end(); ++it) {
            if (it->second < size)
                continue;
            const std::size_t offset = it->first, left = it->second - size;
            free_.erase(it);
            if (left > 0)
                free_.emplace(offset + size, left);
            used_ += size;
            return offset;
        }
        return std::nullopt;
    }

    void release(std::size_t offset, std::size_t size)
    {
        if (size == 0)
            return;
        used_ -= size;
        auto it = free_.emplace(offset, size).first;
        if (auto next = std::next(it); next != free_.end() && it->first + it->second == next->first) {
            it->second += next->second;
            free_.erase(next);
        }
        if (it != free_.begin())
            if (auto prev = std::prev(it); prev->first + prev->second == it->first) {
                prev->second += it->second;
                free_.erase(it);
            }
    }

    // More room at the end (the buffer was enlarged and the old content copied).
    void grow(std::size_t capacity)
    {
        if (capacity <= capacity_)
            return;
        const std::size_t added = capacity - capacity_;
        const std::size_t start = capacity_;
        capacity_ = capacity;
        used_ += added; // release() takes it off again
        release(start, added);
    }

    // Largest free range (fragmentation check).
    std::size_t largestFree() const
    {
        std::size_t best = 0;
        for (const auto& [offset, size] : free_)
            best = size > best ? size : best;
        return best;
    }

private:
    std::map<std::size_t, std::size_t> free_; // offset -> size
    std::size_t capacity_ = 0, used_ = 0;
};

} // namespace engine
