#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

// Suppress -Wunused-const-variable / -Wunused-function warnings for
// symbols that are only referenced under conditional compilation
// (e.g. backend-specific constants inside #ifdef blocks).
#if defined(__cplusplus) && (__cplusplus >= 201703L)
#define ATLAS_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
#define ATLAS_MAYBE_UNUSED __attribute__((unused))
#else
#define ATLAS_MAYBE_UNUSED
#endif

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

// Supported tensor element data types.
enum class DataType {
    kUnknown = 0,
    kFloat32,
    kFloat16,
    kInt8,
    kUInt8,
    kInt32,
};

// Error codes returned by all public API functions.
// Exceptions are never thrown; callers must check return values.
enum class ErrorCode {
    kOk = 0,
    kInvalidArgument,
    kFileNotFound,
    kParseError,
    kVersionMismatch,
    kBackendNotFound,
    kInferFailed,
    kNotInitialized,
};

// Returns a human-readable string for the given error code.
inline const char* ErrorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::kOk:               return "Ok";
        case ErrorCode::kInvalidArgument:  return "InvalidArgument";
        case ErrorCode::kFileNotFound:     return "FileNotFound";
        case ErrorCode::kParseError:       return "ParseError";
        case ErrorCode::kVersionMismatch:  return "VersionMismatch";
        case ErrorCode::kBackendNotFound:  return "BackendNotFound";
        case ErrorCode::kInferFailed:      return "InferFailed";
        case ErrorCode::kNotInitialized:   return "NotInitialized";
        default:                           return "Unknown";
    }
}

// Per-tensor normalization parameters (mean / std per channel).
struct NormalizeParams {
    std::vector<float> mean;
    std::vector<float> std;
};

// Metadata describing one model input or output tensor.
// Shape values of -1 denote dynamic (unknown-at-parse-time) dimensions.
struct TensorInfo {
    std::string name;
    std::vector<int> shape;
    DataType dtype = DataType::kUnknown;
    std::string layout = "NCHW";
    bool has_normalize = false;
    NormalizeParams normalize;
};

// Runtime tensor that either owns its buffer (owns_data == true) or
// borrows memory managed by the caller (owns_data == false).
// Copy is disabled; only move semantics are supported.
struct Tensor {
    TensorInfo info;
    void* data = nullptr;
    size_t byte_size = 0;
    size_t capacity = 0;       // Allocated buffer capacity; 0 = no allocation
    bool owns_data = false;

    Tensor() = default;
    ~Tensor() {
        if (owns_data && data != nullptr) {
            free(data);
            data = nullptr;
        }
    }

    Tensor(const Tensor&) = delete;
    Tensor& operator=(const Tensor&) = delete;
    Tensor(Tensor&& other) noexcept
        : info(std::move(other.info)),
          data(other.data),
          byte_size(other.byte_size),
          capacity(other.capacity),
          owns_data(other.owns_data) {
        other.data = nullptr;
        other.capacity = 0;
        other.owns_data = false;
    }
    Tensor& operator=(Tensor&& other) noexcept {
        if (this != &other) {
            if (owns_data && data != nullptr) free(data);
            info = std::move(other.info);
            data = other.data;
            byte_size = other.byte_size;
            capacity = other.capacity;
            owns_data = other.owns_data;
            other.data = nullptr;
            other.capacity = 0;
            other.owns_data = false;
        }
        return *this;
    }

    // Ensures at least |size| bytes of writable space, reallocating if needed.
    // Does NOT zero the buffer; the caller must fully overwrite [0, size).
    // On allocation failure, all members remain unchanged.
    void EnsureCapacity(size_t size) {
        if (capacity >= size) {
            byte_size = size;
            return;
        }
        void* new_data = malloc(size);
        if (!new_data) return;  // allocation failed — preserve existing state
        if (owns_data && data) free(data);
        data = new_data;
        capacity = size;
        byte_size = size;
        owns_data = true;
    }
};

// Returns the byte size of one element for the given data type.
// Returns 0 for DataType::kUnknown.
inline size_t ElementByteSize(DataType dtype) {
    switch (dtype) {
        case DataType::kFloat32: return 4;
        case DataType::kFloat16: return 2;
        case DataType::kInt8:    return 1;
        case DataType::kUInt8:   return 1;
        case DataType::kInt32:   return 4;
        default:                 return 0;
    }
}

// Returns the total number of elements described by |shape|.
// Returns 0 if any dimension is 0, and treats -1 (dynamic) as 1.
inline size_t ElementCount(const std::vector<int>& shape) {
    size_t count = 1;
    for (int dim : shape) {
        count *= static_cast<size_t>(dim > 0 ? dim : 1);
    }
    return count;
}

struct UserData {
    void*   data  = nullptr;
    size_t  size  = 0;
    uint8_t flags = 0;
};

}  // namespace utils
}  // namespace atlas