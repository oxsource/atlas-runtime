#include "src/pipeline/nodes/chw_to_hwc_node.h"

#include <cstdlib>
#include <memory>

#include "src/pipeline/pipeline_node_factory.h"
#include "src/utils/types.h"

namespace {
constexpr std::string_view kNodeName = "atlas::chw_to_hwc";
}  // namespace

namespace atlas {
namespace pipeline {

std::string_view CHWToHWCNode::Name() const { return kNodeName; }

utils::ErrorCode CHWToHWCNode::Process(const Context& ctx,
                                      const utils::Tensor& input, utils::Tensor* output) {
    if (output == nullptr) return utils::ErrorCode::kInvalidArgument;
    if (input.info.shape.size() != 3) return utils::ErrorCode::kInvalidArgument;

    const int c    = input.info.shape[0];
    const int h    = input.info.shape[1];
    const int w    = input.info.shape[2];
    const size_t elem_size = utils::ElementByteSize(input.info.dtype);
    if (elem_size == 0) return utils::ErrorCode::kInvalidArgument;

    output->info           = input.info;
    output->info.shape     = {h, w, c};
    output->info.layout    = "HWC";
    output->EnsureCapacity(input.byte_size);

    if (output->data == nullptr) return utils::ErrorCode::kInvalidArgument;

    // Reorder: src[c_idx][h][w] → dst[h][w][c_idx]
    const size_t hw = static_cast<size_t>(h) * static_cast<size_t>(w);
    const uint8_t* src = static_cast<const uint8_t*>(input.data);
    uint8_t*       dst = static_cast<uint8_t*>(output->data);

    for (int ci = 0; ci < c; ++ci) {
        for (size_t px = 0; px < hw; ++px) {
            const size_t src_off = (static_cast<size_t>(ci) * hw + px) * elem_size;
            const size_t dst_off = (px * static_cast<size_t>(c) +
                                    static_cast<size_t>(ci)) * elem_size;
            for (size_t b = 0; b < elem_size; ++b) {
                dst[dst_off + b] = src[src_off + b];
            }
        }
    }
    return utils::ErrorCode::kOk;
}

// static
std::unique_ptr<IPipelineNode> CHWToHWCNode::CreateFromParams(
    const std::unordered_map<std::string, std::string>& params) {
    return std::make_unique<CHWToHWCNode>();
}

}  // namespace pipeline
}  // namespace atlas

ATLAS_REGISTER_PIPELINE_NODE(kNodeName, atlas::pipeline::CHWToHWCNode)

