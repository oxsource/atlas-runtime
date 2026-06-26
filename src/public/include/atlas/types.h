#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "atlas/atlas_export.h"

namespace atlas {
namespace utils {

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
    DataType dtype = DataType::kFloat32;
    std::string layout = "NCHW";
    bool has_normalize = false;
    NormalizeParams normalize;
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

// Runtime tensor that either owns its buffer (owns_data == true) or
// borrows memory managed by the caller (owns_data == false).
// Copy is disabled; only move semantics are supported.
struct Tensor {
    TensorInfo info;
    void* data = nullptr;
    size_t byte_size = 0;
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
          owns_data(other.owns_data) {
        other.data = nullptr;
        other.owns_data = false;
    }
    Tensor& operator=(Tensor&& other) noexcept {
        if (this != &other) {
            if (owns_data && data != nullptr) free(data);
            info = std::move(other.info);
            data = other.data;
            byte_size = other.byte_size;
            owns_data = other.owns_data;
            other.data = nullptr;
            other.owns_data = false;
        }
        return *this;
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

}  // namespace utils
}  // namespace atlas
