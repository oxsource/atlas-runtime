#include "src/pipeline/nodes/hwc_to_chw_node.h"

#include <cstdlib>

#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

namespace {
constexpr std::string_view kNodeName = "HWCToCHW";
}  // namespace

std::string_view HWCToCHWNode::Name() const { return kNodeName; }

utils::ErrorCode HWCToCHWNode::Process(const utils::Tensor& input,
                                        utils::Tensor* output) {
    if (output == nullptr) return utils::ErrorCode::kInvalidArgument;
    if (input.info.shape.size() != 3) return utils::ErrorCode::kInvalidArgument;

    const int h    = input.info.shape[0];
    const int w    = input.info.shape[1];
    const int c    = input.info.shape[2];
    const size_t elem_size = utils::ElementByteSize(input.info.dtype);
    if (elem_size == 0) return utils::ErrorCode::kInvalidArgument;

    output->info           = input.info;
    output->info.shape     = {c, h, w};
    output->info.layout    = "CHW";
    output->byte_size      = input.byte_size;
    output->data           = malloc(input.byte_size);
    output->owns_data      = true;

    if (output->data == nullptr) return utils::ErrorCode::kInvalidArgument;

    // Reorder: src[h][w][c_idx] → dst[c_idx][h][w]
    const size_t hw = static_cast<size_t>(h) * static_cast<size_t>(w);
    const uint8_t* src = static_cast<const uint8_t*>(input.data);
    uint8_t*       dst = static_cast<uint8_t*>(output->data);

    for (int ci = 0; ci < c; ++ci) {
        for (size_t px = 0; px < hw; ++px) {
            const size_t src_off = (px * static_cast<size_t>(c) +
                                    static_cast<size_t>(ci)) * elem_size;
            const size_t dst_off = (static_cast<size_t>(ci) * hw + px) * elem_size;
            for (size_t b = 0; b < elem_size; ++b) {
                dst[dst_off + b] = src[src_off + b];
            }
        }
    }
    return utils::ErrorCode::kOk;
}

}  // namespace pipeline
}  // namespace atlas
