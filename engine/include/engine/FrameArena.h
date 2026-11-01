#pragma once
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <vector>

namespace engine {

// Frame (bump) allocator: per-frame scratch arrays take memory from a block that
// reset() hands back all at once, so a frame does no heap allocations once the
// block is big enough. Use with std::pmr containers:
//     std::pmr::vector<Light> lights(&arena);
// Not thread-safe: one arena per thread. Memory is valid until the next reset().
class FrameArena : public std::pmr::memory_resource {
public:
    explicit FrameArena(std::size_t blockBytes = 256 * 1024);
    ~FrameArena() override;
    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;

    // Start of a frame: everything allocated since the last reset is dropped. When a
    // frame needed more than one block, the next frame gets one block of that size.
    void reset();
    std::size_t used() const { return used_; }        // bytes handed out since reset
    std::size_t peak() const { return peak_; }        // largest frame so far
    std::size_t capacity() const;                     // bytes held
    int overflowBlocks() const { return static_cast<int>(blocks_.size()) - 1; }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    void do_deallocate(void*, std::size_t, std::size_t) override {} // freed by reset()
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }

    struct Block {
        std::unique_ptr<std::byte[]> data;
        std::size_t size = 0;
    };
    std::vector<Block> blocks_;
    std::size_t offset_ = 0; // in the last block
    std::size_t used_ = 0, peak_ = 0, blockBytes_;
};

} // namespace engine
