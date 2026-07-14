#include "src/pipeline/nodes/resize_node.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "src/pipeline/pipeline_node_factory.h"
#include "src/utils/strings.h"
#include "src/utils/types.h"

namespace {
constexpr std::string_view kNodeName = "atlas::resize";
}  // namespace

namespace atlas {
namespace pipeline {

namespace {

// Bilinear resize for float32 HWC tensors.
void BilinearResizeFloat(const float* src, int src_h, int src_w,
                          float* dst, int dst_h, int dst_w, int channels) {
    const float scale_h = static_cast<float>(src_h) / static_cast<float>(dst_h);
    const float scale_w = static_cast<float>(src_w) / static_cast<float>(dst_w);

    for (int oh = 0; oh < dst_h; ++oh) {
        const float ih_f = (static_cast<float>(oh) + 0.5f) * scale_h - 0.5f;
        const int   ih0  = static_cast<int>(ih_f);
        const int   ih1  = ih0 + 1;
        const float dh   = ih_f - static_cast<float>(ih0);
        const int   ih0c = ih0 < 0 ? 0 : (ih0 >= src_h ? src_h - 1 : ih0);
        const int   ih1c = ih1 < 0 ? 0 : (ih1 >= src_h ? src_h - 1 : ih1);

        for (int ow = 0; ow < dst_w; ++ow) {
            const float iw_f = (static_cast<float>(ow) + 0.5f) * scale_w - 0.5f;
            const int   iw0  = static_cast<int>(iw_f);
            const int   iw1  = iw0 + 1;
            const float dw   = iw_f - static_cast<float>(iw0);
            const int   iw0c = iw0 < 0 ? 0 : (iw0 >= src_w ? src_w - 1 : iw0);
            const int   iw1c = iw1 < 0 ? 0 : (iw1 >= src_w ? src_w - 1 : iw1);

            float* out_ptr = dst + (oh * dst_w + ow) * channels;
            const float* p00 = src + (ih0c * src_w + iw0c) * channels;
            const float* p01 = src + (ih0c * src_w + iw1c) * channels;
            const float* p10 = src + (ih1c * src_w + iw0c) * channels;
            const float* p11 = src + (ih1c * src_w + iw1c) * channels;

            for (int c = 0; c < channels; ++c) {
                out_ptr[c] = p00[c] * (1.0f - dh) * (1.0f - dw)
                           + p01[c] * (1.0f - dh) * dw
                           + p10[c] * dh           * (1.0f - dw)
                           + p11[c] * dh           * dw;
            }
        }
    }
}

// Bilinear resize for uint8 HWC tensors (rounds to nearest).
void BilinearResizeUint8(const uint8_t* src, int src_h, int src_w,
                          uint8_t* dst, int dst_h, int dst_w, int channels) {
    const float scale_h = static_cast<float>(src_h) / static_cast<float>(dst_h);
    const float scale_w = static_cast<float>(src_w) / static_cast<float>(dst_w);

    for (int oh = 0; oh < dst_h; ++oh) {
        const float ih_f = (static_cast<float>(oh) + 0.5f) * scale_h - 0.5f;
        const int   ih0  = static_cast<int>(ih_f);
        const int   ih1  = ih0 + 1;
        const float dh   = ih_f - static_cast<float>(ih0);
        const int   ih0c = ih0 < 0 ? 0 : (ih0 >= src_h ? src_h - 1 : ih0);
        const int   ih1c = ih1 < 0 ? 0 : (ih1 >= src_h ? src_h - 1 : ih1);

        for (int ow = 0; ow < dst_w; ++ow) {
            const float iw_f = (static_cast<float>(ow) + 0.5f) * scale_w - 0.5f;
            const int   iw0  = static_cast<int>(iw_f);
            const int   iw1  = iw0 + 1;
            const float dw   = iw_f - static_cast<float>(iw0);
            const int   iw0c = iw0 < 0 ? 0 : (iw0 >= src_w ? src_w - 1 : iw0);
            const int   iw1c = iw1 < 0 ? 0 : (iw1 >= src_w ? src_w - 1 : iw1);

            uint8_t* out_ptr = dst + (oh * dst_w + ow) * channels;
            const uint8_t* p00 = src + (ih0c * src_w + iw0c) * channels;
            const uint8_t* p01 = src + (ih0c * src_w + iw1c) * channels;
            const uint8_t* p10 = src + (ih1c * src_w + iw0c) * channels;
            const uint8_t* p11 = src + (ih1c * src_w + iw1c) * channels;

            for (int c = 0; c < channels; ++c) {
                out_ptr[c] = static_cast<uint8_t>(
                    p00[c] * (1.0f - dh) * (1.0f - dw)
                  + p01[c] * (1.0f - dh) * dw
                  + p10[c] * dh           * (1.0f - dw)
                  + p11[c] * dh           * dw + 0.5f);
            }
        }
    }
}
}  // namespace

ResizeNode::ResizeNode(int target_h, int target_w)
    : target_h_(target_h), target_w_(target_w) {}

std::string_view ResizeNode::Name() const { return kNodeName; }

utils::ErrorCode ResizeNode::Process(const Context& ctx,
                                    const utils::Tensor& input, utils::Tensor* output) {
    if (output == nullptr) return utils::ErrorCode::kInvalidArgument;
    if (input.info.shape.size() != 3) return utils::ErrorCode::kInvalidArgument;

    const int src_h    = input.info.shape[0];
    const int src_w    = input.info.shape[1];
    const int channels = input.info.shape[2];

    const size_t elem_size = utils::ElementByteSize(input.info.dtype);
    if (elem_size == 0) return utils::ErrorCode::kInvalidArgument;

    const size_t out_bytes =
        static_cast<size_t>(target_h_) *
        static_cast<size_t>(target_w_) *
        static_cast<size_t>(channels) * elem_size;

    output->info           = input.info;
    output->info.shape     = {target_h_, target_w_, channels};
    output->EnsureCapacity(out_bytes);

    if (output->data == nullptr) return utils::ErrorCode::kInvalidArgument;

    if (input.info.dtype == utils::DataType::kFloat32) {
        BilinearResizeFloat(
            static_cast<const float*>(input.data), src_h, src_w,
            static_cast<float*>(output->data), target_h_, target_w_,
            channels);
    } else if (input.info.dtype == utils::DataType::kUInt8) {
        BilinearResizeUint8(
            static_cast<const uint8_t*>(input.data), src_h, src_w,
            static_cast<uint8_t*>(output->data), target_h_, target_w_,
            channels);
    } else {
        free(output->data);
        output->data = nullptr;
        return utils::ErrorCode::kInvalidArgument;
    }
    return utils::ErrorCode::kOk;
}

// static
std::unique_ptr<IPipelineNode> ResizeNode::CreateFromParams(
    const std::unordered_map<std::string, std::string>& params) {
    auto h_it = params.find("height");
    auto w_it = params.find("width");
    if (h_it == params.end() || w_it == params.end()) return nullptr;
    int height = 0;
    int width  = 0;
    if (utils::Strings::ParseInt(h_it->second, &height) != utils::ErrorCode::kOk) {
        return nullptr;
    }
    if (utils::Strings::ParseInt(w_it->second, &width) != utils::ErrorCode::kOk) {
        return nullptr;
    }
    return std::make_unique<ResizeNode>(height, width);
}

}  // namespace pipeline
}  // namespace atlas

ATLAS_REGISTER_PIPELINE_NODE(kNodeName, atlas::pipeline::ResizeNode)

