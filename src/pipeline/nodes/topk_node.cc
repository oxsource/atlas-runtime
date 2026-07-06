#include "src/pipeline/nodes/topk_node.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "src/pipeline/pipeline_node_factory.h"
#include "src/utils/strings.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

namespace {
constexpr std::string_view kNodeName = "TopK";
}  // namespace

TopKNode::TopKNode(int k) : k_(k) {}

std::string_view TopKNode::Name() const { return kNodeName; }

utils::ErrorCode TopKNode::Process(const utils::Tensor& input,
                                    utils::Tensor* output) {
    if (output == nullptr) return utils::ErrorCode::kInvalidArgument;
    if (input.info.dtype != utils::DataType::kFloat32) {
        return utils::ErrorCode::kInvalidArgument;
    }
    if (input.info.shape.size() != 1) return utils::ErrorCode::kInvalidArgument;
    if (k_ <= 0) return utils::ErrorCode::kInvalidArgument;

    const size_t n = input.byte_size / sizeof(float);
    if (static_cast<size_t>(k_) > n) return utils::ErrorCode::kInvalidArgument;

    const float* src = static_cast<const float*>(input.data);

    // Min-heap of (value, index) keyed by value; keeps the k largest.
    using Entry = std::pair<float, int>;
    std::priority_queue<
        Entry,
        std::vector<Entry>,
        std::greater<Entry>> heap;

    for (size_t i = 0; i < n; ++i) {
        if (heap.size() < static_cast<size_t>(k_)) {
            heap.emplace(src[i], static_cast<int>(i));
        } else if (src[i] > heap.top().first) {
            heap.pop();
            heap.emplace(src[i], static_cast<int>(i));
        }
    }

    // Extract values in descending order.
    std::vector<float> values;
    values.reserve(static_cast<size_t>(k_));
    while (!heap.empty()) {
        values.push_back(heap.top().first);
        heap.pop();
    }
    std::reverse(values.begin(), values.end());

    // Allocate output tensor of shape [k].
    const size_t out_bytes = static_cast<size_t>(k_) * sizeof(float);
    output->info           = input.info;
    output->info.shape     = {k_};
    output->byte_size      = out_bytes;
    output->data           = malloc(out_bytes);
    output->owns_data      = true;

    if (output->data == nullptr) return utils::ErrorCode::kInvalidArgument;

    float* dst = static_cast<float*>(output->data);
    std::memcpy(dst, values.data(), out_bytes);
    return utils::ErrorCode::kOk;
}

// static
std::unique_ptr<IPipelineNode> TopKNode::CreateFromParams(
    const std::unordered_map<std::string, std::string>& params) {
    auto it = params.find("k");
    if (it == params.end()) return nullptr;
    int k = 0;
    if (utils::Strings::ParseInt(it->second, &k) != utils::ErrorCode::kOk) {
        return nullptr;
    }
    if (k <= 0) return nullptr;
    return std::make_unique<TopKNode>(k);
}

}  // namespace pipeline
}  // namespace atlas

ATLAS_REGISTER_PIPELINE_NODE("topk", atlas::pipeline::TopKNode)
