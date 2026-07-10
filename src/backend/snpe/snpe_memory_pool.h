#pragma once

#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/backend/snpe/snpe_aligned_buffer.h"

namespace atlas {
namespace backend {

// Thread-safe aligned memory pool for SNPE UserBuffer.
//
// Provides two usage modes:
//
//   Path A — Free-list reuse (Acquire / Release)
//     Preferred allocation path that reuses previously freed buffers,
//     eliminating repeated posix_memalign/free cycles across model
//     Load/Unload life-cycles.
//
//   Path B — Shared buffers (AcquireShared / ReleaseShared)
//     Key-based, reference-counted shared memory.  Multiple callers
//     with the same key share a single aligned allocation.  Useful
//     when multiple models read the same input data.
//
// Thread safety:
//   All public methods use std::mutex.  Multiple threads may
//   concurrently Acquire/Release from the same pool.
//
// Lifecycle:
//   SnpeBackendContext owns one SnpeMemoryPool.  It lives as long as
//   the context (one per manifest / process lifetime).
class SnpeMemoryPool {
 public:
    SnpeMemoryPool() = default;
    ~SnpeMemoryPool() { Clear(); }

    SnpeMemoryPool(const SnpeMemoryPool&) = delete;
    SnpeMemoryPool& operator=(const SnpeMemoryPool&) = delete;

    // ── Path A: Free-list reuse ────────────────────────────────────────

    // Acquires a buffer of at least |required_size| bytes.
    // Returns a best-fit free buffer if one exists, else allocates new.
    // If |required_size| is zero, returns a default (empty) AlignedBuffer.
    AlignedBuffer Acquire(size_t required_size, size_t alignment = 128);

    // Returns a previously Acquire()d buffer to the free list.
    // After this call the pool owns |buf|.  Caller MUST NOT free/use it.
    void Release(AlignedBuffer buf);

    // Pre-allocates buffers of the given sizes into the free list.
    void PreAllocate(const std::vector<size_t>& sizes,
                     size_t alignment = 128);

    // ── Path B: Shared buffers (key-based, reference-counted) ──────────

    // Acquires a shared buffer identified by |key|.
    //
    // First caller: allocates, stores internally, returns data pointer.
    // Subsequent callers (same key): increment refcount, return same ptr.
    //
    // Returns nullptr on allocation failure or if |required_size| is 0.
    //
    // IMPORTANT: Callers must pair AcquireShared() with ReleaseShared().
    // The returned pointer is owned by the pool — callers must NOT free it.
    void* AcquireShared(const std::string& key, size_t required_size,
                        size_t alignment = 128);

    // Releases one reference to a shared buffer.
    // When refcount reaches zero, the buffer is moved to the free list.
    void ReleaseShared(const std::string& key);

    // Frees all pooled and shared memory.  After Clear() the pool is empty
    // and can still be used for new Acquire/AcquireShared calls.
    void Clear();

    // Returns total bytes managed by the pool (for diagnostics).
    size_t TotalAllocatedBytes() const;

 private:
    struct SharedEntry {
        AlignedBuffer buffer;
        int refcount = 0;
    };

    mutable std::mutex mutex_;

    // Free list: buffers available for reuse.
    std::vector<AlignedBuffer> free_list_;

    // Shared buffers indexed by key.
    std::unordered_map<std::string, SharedEntry> shared_buffers_;

    size_t total_allocated_ = 0;

    // Allocates one aligned buffer (caller must hold mutex_).
    AlignedBuffer AllocateRaw(size_t size, size_t alignment);
};

// ── Inline implementation ─────────────────────────────────────────────

inline AlignedBuffer SnpeMemoryPool::Acquire(size_t required_size,
                                              size_t alignment) {
    if (required_size == 0) return {};

    std::lock_guard<std::mutex> lock(mutex_);

    // Best-fit: find the smallest free buffer with capacity >= required_size.
    auto best_it = free_list_.end();
    for (auto it = free_list_.begin(); it != free_list_.end(); ++it) {
        if (it->size >= required_size) {
            if (best_it == free_list_.end() || it->size < best_it->size) {
                best_it = it;
            }
        }
    }

    if (best_it != free_list_.end()) {
        AlignedBuffer result = std::move(*best_it);
        free_list_.erase(best_it);
        return result;
    }

    // No suitable free buffer — allocate new.
    return AllocateRaw(required_size, alignment);
}

inline void SnpeMemoryPool::Release(AlignedBuffer buf) {
    if (buf.data == nullptr) return;

    std::lock_guard<std::mutex> lock(mutex_);
    free_list_.push_back(std::move(buf));
}

inline void SnpeMemoryPool::PreAllocate(const std::vector<size_t>& sizes,
                                         size_t alignment) {
    std::lock_guard<std::mutex> lock(mutex_);
    free_list_.reserve(free_list_.size() + sizes.size());
    for (size_t s : sizes) {
        if (s == 0) continue;
        free_list_.push_back(AllocateRaw(s, alignment));
    }
}

inline void* SnpeMemoryPool::AcquireShared(const std::string& key,
                                            size_t required_size,
                                            size_t alignment) {
    if (required_size == 0) return nullptr;

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = shared_buffers_.find(key);
    if (it != shared_buffers_.end()) {
        // Existing shared buffer — increment refcount.
        ++it->second.refcount;
        return it->second.buffer.data;
    }

    // First access — allocate new buffer.
    AlignedBuffer buf = AllocateRaw(required_size, alignment);
    if (buf.data == nullptr) return nullptr;

    void* ptr = buf.data;
    shared_buffers_.emplace(key, SharedEntry{std::move(buf), 1});
    return ptr;
}

inline void SnpeMemoryPool::ReleaseShared(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = shared_buffers_.find(key);
    if (it == shared_buffers_.end()) return;

    --it->second.refcount;
    if (it->second.refcount <= 0) {
        // Move buffer to free list for reuse.
        free_list_.push_back(std::move(it->second.buffer));
        shared_buffers_.erase(it);
    }
}

inline void SnpeMemoryPool::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    free_list_.clear();
    shared_buffers_.clear();
    total_allocated_ = 0;
}

inline size_t SnpeMemoryPool::TotalAllocatedBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_allocated_;
}

inline AlignedBuffer SnpeMemoryPool::AllocateRaw(size_t size,
                                                  size_t alignment) {
    AlignedBuffer buf(size, alignment);
    if (buf.data != nullptr) {
        total_allocated_ += size;
    }
    return buf;
}

}  // namespace backend
}  // namespace atlas
