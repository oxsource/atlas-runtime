#include "src/pipeline/nodes/bgr_to_rgb_node.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

namespace {
constexpr std::string_view kNodeName      = "BGRToRGB";
constexpr int              kRequiredChans = 3;
}  // namespace

std::string_view BGRToRGBNode::Name() const { return kNodeName; }

utils::ErrorCode BGRToRGBNode::Process(const utils::Tensor& input,
                                        utils::Tensor* output) {
    if (output == nullptr) return utils::ErrorCode::kInvalidArgument;
    if (input.info.shape.size() != 3) return utils::ErrorCode::kInvalidArgument;
    if (input.info.shape[2] != kRequiredChans) {
        return utils::ErrorCode::kInvalidArgument;
    }

    const size_t elem_size = utils::ElementByteSize(input.info.dtype);
    if (elem_size == 0) return utils::ErrorCode::kInvalidArgument;

    // Allocate output buffer (same size as input).
    output->info      = input.info;
    output->byte_size = input.byte_size;
    output->data      = malloc(input.byte_size);
    output->owns_data = true;

    if (output->data == nullptr) return utils::ErrorCode::kInvalidArgument;

    const int pixels = input.info.shape[0] * input.info.shape[1];

    if (input.info.dtype == utils::DataType::kFloat32) {
        const float* src = static_cast<const float*>(input.data);
        float*       dst = static_cast<float*>(output->data);
        for (int i = 0; i < pixels; ++i) {
            dst[i * kRequiredChans + 0] = src[i * kRequiredChans + 2];
            dst[i * kRequiredChans + 1] = src[i * kRequiredChans + 1];
            dst[i * kRequiredChans + 2] = src[i * kRequiredChans + 0];
        }
    } else if (input.info.dtype == utils::DataType::kUInt8) {
        const uint8_t* src = static_cast<const uint8_t*>(input.data);
        uint8_t*       dst = static_cast<uint8_t*>(output->data);
        for (int i = 0; i < pixels; ++i) {
            dst[i * kRequiredChans + 0] = src[i * kRequiredChans + 2];
            dst[i * kRequiredChans + 1] = src[i * kRequiredChans + 1];
            dst[i * kRequiredChans + 2] = src[i * kRequiredChans + 0];
        }
    } else {
        free(output->data);
        output->data = nullptr;
        return utils::ErrorCode::kInvalidArgument;
    }
    return utils::ErrorCode::kOk;
}

}  // namespace pipeline
}  // namespace atlas
