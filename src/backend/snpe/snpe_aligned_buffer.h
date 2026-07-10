#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>

// macOS aligned_alloc polyfill: C11 aligned_alloc requires size to be a
// multiple of alignment, but macOS's aligned_alloc does not enforce this
// (it follows the older POSIX semantics).  We use posix_memalign instead
// for portability across macOS and Linux.
// C++17 std::aligned_alloc is available on both platforms but the
// size-multiple requirement differs between glibc (Linux) and Apple
// libc (macOS).  posix_memalign is consistent everywhere.
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
#include <cerrno>
#endif

namespace atlas {
namespace backend {

// RAII wrapper for aligned memory allocation required by DSP/HTP runtimes.
//
// Allocates memory aligned to a user-specified boundary (default 128 bytes)
// using posix_memalign.  The allocation happens once in Load() — never on
// the inference hot path — so the alignment overhead is negligible.
//
// AlignedBuffer is move-only (copy deleted) so it can be stored in a
// std::vector for per-tensor management.
struct AlignedBuffer {
    void*  data = nullptr;
    size_t size = 0;

    AlignedBuffer() = default;

    // Allocates |byte_size| bytes aligned to |alignment|.
    // If allocation fails, |data| is set to nullptr and |size| is 0.
    // |alignment| must be a power of two and a multiple of sizeof(void*).
    explicit AlignedBuffer(size_t byte_size, size_t alignment = 128) {
        if (byte_size == 0) return;
        void* ptr = nullptr;
        if (posix_memalign(&ptr, alignment, byte_size) != 0) {
            // Allocation failed — leave data as nullptr.
            return;
        }
        data = ptr;
        size = byte_size;
    }

    ~AlignedBuffer() {
        if (data != nullptr) {
            std::free(data);
        }
    }

    // Move semantics
    AlignedBuffer(AlignedBuffer&& other) noexcept
        : data(other.data), size(other.size) {
        other.data = nullptr;
        other.size = 0;
    }

    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            if (data != nullptr) std::free(data);
            data = other.data;
            size = other.size;
            other.data = nullptr;
            other.size = 0;
        }
        return *this;
    }

    // No copy
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    // Convenience cast.
    uint8_t* AsU8() { return static_cast<uint8_t*>(data); }
    const uint8_t* AsU8() const { return static_cast<const uint8_t*>(data); }
};

}  // namespace backend
}  // namespace atlas
