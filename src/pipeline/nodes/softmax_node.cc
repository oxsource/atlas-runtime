#include "src/pipeline/nodes/softmax_node.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "src/pipeline/pipeline_node_factory.h"
#include "src/utils/strings.h"
#include "src/utils/types.h"

namespace {
constexpr std::string_view kNodeName = "atlas::softmax";
}  // namespace

namespace atlas {
namespace pipeline {

SoftmaxNode::SoftmaxNode(int axis) : axis_(axis) {}

std::string_view SoftmaxNode::Name() const { return kNodeName; }

utils::ErrorCode SoftmaxNode::Process(const Context& ctx,
                                     const utils::Tensor& input, utils::Tensor* output) {
    if (output == nullptr) return utils::ErrorCode::kInvalidArgument;
    if (input.info.dtype != utils::DataType::kFloat32) {
        return utils::ErrorCode::kInvalidArgument;
    }
    if (input.info.shape.empty()) return utils::ErrorCode::kInvalidArgument;

    const int rank = static_cast<int>(input.info.shape.size());

    // Resolve axis (negative counts from the end).
    int axis = axis_;
    if (axis < 0) axis += rank;
    if (axis < 0 || axis >= rank) return utils::ErrorCode::kInvalidArgument;

    // Compute outer size (product of dims before axis) and
    // inner size (product of dims from axis onward).
    size_t outer = 1;
    for (int i = 0; i < axis; ++i) {
        outer *= static_cast<size_t>(input.info.shape[i] > 0
                                         ? input.info.shape[i] : 1);
    }
    size_t axis_size = static_cast<size_t>(input.info.shape[axis] > 0
                                               ? input.info.shape[axis] : 1);
    size_t inner = 1;
    for (int i = axis + 1; i < rank; ++i) {
        inner *= static_cast<size_t>(input.info.shape[i] > 0
                                         ? input.info.shape[i] : 1);
    }

    // Allocate / reuse output buffer (same shape and size as input).
    output->info = input.info;
    output->EnsureCapacity(input.byte_size);

    if (output->data == nullptr) return utils::ErrorCode::kInvalidArgument;

    const float* src = static_cast<const float*>(input.data);
    float*       dst = static_cast<float*>(output->data);

    const size_t row_len = axis_size * inner;

    for (size_t o = 0; o < outer; ++o) {
        const float* src_row = src + o * row_len;
        float*       dst_row = dst + o * row_len;

        // Softmax is computed independently along |axis_size| groups,
        // each of length |inner|.
        for (size_t g = 0; g < inner; ++g) {
            // 1. Find max for numerical stability.
            float max_val = -std::numeric_limits<float>::infinity();
            for (size_t a = 0; a < axis_size; ++a) {
                const float v = src_row[a * inner + g];
                if (v > max_val) max_val = v;
            }

            // 2. Compute exponentials and sum.
            float sum = 0.0f;
            for (size_t a = 0; a < axis_size; ++a) {
                const float e = std::exp(src_row[a * inner + g] - max_val);
                dst_row[a * inner + g] = e;
                sum += e;
            }

            // 3. Normalize.
            if (sum <= 0.0f) {
                free(output->data);
                output->data = nullptr;
                return utils::ErrorCode::kInvalidArgument;
            }
            const float inv_sum = 1.0f / sum;
            for (size_t a = 0; a < axis_size; ++a) {
                dst_row[a * inner + g] *= inv_sum;
            }
        }
    }
    return utils::ErrorCode::kOk;
}

// static
std::unique_ptr<IPipelineNode> SoftmaxNode::CreateFromParams(
    const std::unordered_map<std::string, std::string>& params) {
    auto it = params.find("axis");
    if (it == params.end()) {
        return std::make_unique<SoftmaxNode>(-1);
    }
    int axis = -1;
    if (utils::Strings::ParseInt(it->second, &axis) != utils::ErrorCode::kOk) {
        return nullptr;
    }
    return std::make_unique<SoftmaxNode>(axis);
}

}  // namespace pipeline
}  // namespace atlas

ATLAS_REGISTER_PIPELINE_NODE(kNodeName, atlas::pipeline::SoftmaxNode)

