#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
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

// Tracks how manifest config overrides affect a tensor's dtype and
// quantization parameters during BuildTensorInfos().
//
// Usage in v1.cc / v2.cc:
//   OverrideTracker tracker(name, info.dtype, qp.scale, qp.zero_point, qp.bandwidth);
//   for (const auto& mi : model_config_.inputs) {
//       if (mi.name != name) continue;
//       if (mi.dtype != utils::DataType::kUnknown) {
//           tracker.describe("override input.dtype");
//           info.dtype = mi.dtype;
//           qp = QuantParamsForDtype(mi.dtype);
//       } else {
//           tracker.describe("shape/layout only (no dtype)");
//       }
//       break;
//   }
//   ATLAS_LOGD("%s", tracker.ToString(info.dtype, qp.scale, qp.zero_point, qp.bandwidth).c_str());
//
// This class is header-only and does NOT depend on LOG_TAG or logger.h.
class OverrideTracker {
 public:
  OverrideTracker(const std::string& name,
                  utils::DataType model_dtype,
                  float model_scale, int32_t model_zp, uint32_t model_bw)
      : name_(name),
        model_dtype_(model_dtype),
        model_scale_(model_scale), model_zp_(model_zp), model_bw_(model_bw) {}

  void describe(const char* desc) { override_desc_ = desc ? desc : ""; }
  void describe(const std::string& desc) { override_desc_ = desc; }

  // Returns a formatted string describing the state transition, e.g.:
  // "input 'input.1': model_dtype=3 model_quant=(scale=0.007812 zp=128 bw=8) |
  //  override input.dtype -> final_dtype=4 final_quant=(scale=1.000000 zp=0 bw=8) |
  //  adapt_u8_path=NO(no adapt)"
  // Caller should prepend __func__ in the log output.
  std::string ToString(utils::DataType final_dtype,
                       float final_scale,
                       int32_t final_zp,
                       uint32_t final_bw) const {
    const char* adapt = (final_dtype == utils::DataType::kInt8)
                            ? "YES(AdaptU8ToTf8)"
                            : "NO(no adapt)";
    const std::string& desc = override_desc_.empty()
                                  ? "[no override]"
                                  : override_desc_;

    char buf[512];
    int n = std::snprintf(buf, sizeof(buf),
        "OverrideTracker{input '%s' model_dtype=%d "
        "model_quant=(scale=%f zp=%d bw=%u) | "
        "%s -> final_dtype=%d "
        "final_quant=(scale=%f zp=%d bw=%u) | "
        "adapt_u8_path=%s}",
        name_.c_str(),
        static_cast<int>(model_dtype_),
        model_scale_, model_zp_, model_bw_,
        desc.c_str(),
        static_cast<int>(final_dtype),
        final_scale, final_zp, final_bw,
        adapt);
    if (n < 0) return {};
    return std::string(buf, static_cast<size_t>(n));
  }

 private:
  std::string name_;
  utils::DataType model_dtype_;
  float model_scale_;
  int32_t model_zp_;
  uint32_t model_bw_;
  std::string override_desc_;
};

}  // namespace snpe
}  // namespace backend
}  // namespace atlas