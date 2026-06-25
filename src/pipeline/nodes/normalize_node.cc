#include "src/pipeline/nodes/normalize_node.h"

#include <cstdlib>
#include <cstring>

#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

namespace {
constexpr std::string_view kNodeName     = "Normalize";
constexpr float            kInvScale255 = 1.0f / 255.0f;
}  // namespace

NormalizeNode::NormalizeNode(const std::vector<float>& mean,
                               const std::vector<float>& std)
    : mean_(mean), std_(std) {}

std::string_view NormalizeNode::Name() const { return kNodeName; }

utils::ErrorCode NormalizeNode::Process(const utils::Tensor& input,
                                         utils::Tensor* output) {
    if (output == nullptr) return utils::ErrorCode::kInvalidArgument;
    if (input.info.dtype != utils::DataType::kFloat32) {
        return utils::ErrorCode::kInvalidArgument;
    }
    // Expects CHW layout: shape = {C, H, W}.
    if (input.info.shape.size() != 3) return utils::ErrorCode::kInvalidArgument;

    const int c  = input.info.shape[0];
    const int hw = input.info.shape[1] * input.info.shape[2];

    if (static_cast<int>(mean_.size()) != c ||
        static_cast<int>(std_.size())  != c) {
        return utils::ErrorCode::kInvalidArgument;
    }

    // Allocate output (same shape and size as input).
    output->info      = input.info;
    output->byte_size = input.byte_size;
    output->data      = malloc(input.byte_size);
    output->owns_data = true;

    if (output->data == nullptr) return utils::ErrorCode::kInvalidArgument;

    const float* src = static_cast<const float*>(input.data);
    float*       dst = static_cast<float*>(output->data);

    for (int ci = 0; ci < c; ++ci) {
        const float inv_std = 1.0f / std_[ci];
        for (int i = 0; i < hw; ++i) {
            const int idx = ci * hw + i;
            dst[idx] = (src[idx] * kInvScale255 - mean_[ci]) * inv_std;
        }
    }
    return utils::ErrorCode::kOk;
}

}  // namespace pipeline
}  // namespace atlas
