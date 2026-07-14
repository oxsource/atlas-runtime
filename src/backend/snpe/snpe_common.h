#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "src/utils/types.h"

// Shared SNPE backend utilities extracted from snpe_backend_v1.cc and
// snpe_backend_v2.cc to eliminate code duplication.
//
// These functions are used by both SDK v1.x and v2.x implementations.

namespace atlas {
namespace backend {
namespace snpe {

// Aligned memory alignment constant for DSP/HTP buffers.
constexpr size_t kBufferAlignment = 128;

// Computes per-dimension byte strides for a UserBuffer.
// Stride[d] = element_size * prod(shape[d+1], ..., shape[rank-1]).
// This formula works for any layout because the stride array order matches
// the shape array order.
inline std::vector<size_t> ComputeUserBufferStride(
    const std::vector<int>& shape, size_t element_size) {
    const size_t rank = shape.size();
    if (rank == 0) return {};
    std::vector<size_t> stride(rank, element_size);
    for (size_t i = rank; i > 1; --i) {
        stride[i - 2] = stride[i - 1] *
            static_cast<size_t>(std::max(shape[i - 1], 1));
    }
    return stride;
}

// Adapts uint8_t input data to int8_t (TF8) by subtracting 128.
// This is a simple range shift: [0, 255] -> [-128, 127].
inline void AdaptU8ToTf8(const void* src, void* dst, size_t count) {
    const auto* u8_src = static_cast<const uint8_t*>(src);
    auto* s8_dst = static_cast<int8_t*>(dst);
    for (size_t i = 0; i < count; ++i) {
        s8_dst[i] = static_cast<int8_t>(static_cast<int>(u8_src[i]) - 128);
    }
}

// Returns true if the input at |index| needs U8->TF8 conversion.
inline bool NeedsQuantizationAdaptation(
    const std::vector<utils::TensorInfo>& info,
    size_t index, utils::DataType input_dtype) {
    return (info[index].dtype == utils::DataType::kInt8 &&
            input_dtype == utils::DataType::kUInt8);
}

}  // namespace snpe
}  // namespace backend
}  // namespace atlas