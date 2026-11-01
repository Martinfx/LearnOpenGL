#include "engine/FrameArena.h"

#include <algorithm>
#include <cstdint>

namespace engine {

FrameArena::FrameArena(std::size_t blockBytes) : blockBytes_(std::max<std::size_t>(blockBytes, 4096))
{
    blocks_.push_back({std::make_unique<std::byte[]>(blockBytes_), blockBytes_});
}

FrameArena::~FrameArena() = default;

std::size_t FrameArena::capacity() const
{
    std::size_t total = 0;
    for (const Block& b : blocks_)
        total += b.size;
    return total;
}

void FrameArena::reset()
{
    peak_ = std::max(peak_, used_);
    if (blocks_.size() > 1) {
        // Grow to what the frame needed (with headroom) in one block.
        const std::size_t size = std::max(capacity(), used_ + used_ / 4);
        blocks_.clear();
        blocks_.push_back({std::make_unique<std::byte[]>(size), size});
    }
    offset_ = 0;
    used_ = 0;
}

void* FrameArena::do_allocate(std::size_t bytes, std::size_t alignment)
{
    Block* b = &blocks_.back();
    auto aligned = [&](std::size_t offset) {
        const auto base = reinterpret_cast<std::uintptr_t>(b->data.get());
        return static_cast<std::size_t>(((base + offset + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1)) - base);
    };
    std::size_t at = aligned(offset_);
    if (at + bytes > b->size) {
        const std::size_t size = std::max(blockBytes_, bytes + alignment);
        blocks_.push_back({std::make_unique<std::byte[]>(size), size});
        b = &blocks_.back();
        offset_ = 0;
        at = aligned(0);
    }
    offset_ = at + bytes;
    used_ += bytes;
    return b->data.get() + at;
}

} // namespace engine
