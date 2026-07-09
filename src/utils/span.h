#pragma once

#include <cstddef>

namespace atlas {
namespace utils {

// Lightweight non-owning view over a contiguous sequence of T elements.
//
// This is a C++17-compatible subset of std::span (C++20).  It provides
// the same data+size semantics without requiring C++20 or external deps.
//
// Usage:
//   Span<float>     data = ...;          // mutable
//   Span<const int> view = ...;          // read-only
//   Span<void> raw  = ...;               // byte buffer
//
// Zero overhead — layout is exactly {T*, size_t}.
template <typename T>
struct Span {
    T*     data = nullptr;
    size_t size = 0;

    Span() = default;
    Span(T* d, size_t s) : data(d), size(s) {}

    bool empty() const noexcept { return size == 0; }
    T& operator[](size_t i) noexcept { return data[i]; }
    const T& operator[](size_t i) const noexcept { return data[i]; }
};

// Span<void> specialization — same layout as Span<T> but without operator[]
// (void& is not a valid type). Use for byte buffers and opaque memory views.
template <>
struct Span<void> {
    void*  data = nullptr;
    size_t size = 0;

    Span() = default;
    Span(void* d, size_t s) : data(d), size(s) {}

    bool empty() const noexcept { return size == 0; }
};

}  // namespace utils
}  // namespace atlas
