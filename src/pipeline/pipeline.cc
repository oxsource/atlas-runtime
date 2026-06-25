#include "src/pipeline/pipeline.h"

#include <memory>
#include <utility>

#include "src/pipeline/nodes/bgr_to_rgb_node.h"
#include "src/pipeline/nodes/dtype_convert_node.h"
#include "src/pipeline/nodes/hwc_to_chw_node.h"
#include "src/pipeline/nodes/normalize_node.h"
#include "src/pipeline/nodes/resize_node.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

namespace {
// Assumed source channel count for BGR→RGB decision.
constexpr int kRgbChannels = 3;
// Index of H/W dimensions in an NCHW shape vector.
constexpr int kNchwHIdx = 2;
constexpr int kNchwWIdx = 3;
}  // namespace

void Pipeline::AddNode(std::unique_ptr<IPipelineNode> node) {
    nodes_.push_back(std::move(node));
}

utils::ErrorCode Pipeline::Run(const utils::Tensor& input,
                                utils::Tensor* output) const {
    if (output == nullptr) return utils::ErrorCode::kInvalidArgument;
    if (nodes_.empty()) {
        // Identity: point output at input without copying.
        output->info      = input.info;
        output->data      = input.data;
        output->byte_size = input.byte_size;
        output->owns_data = false;
        return utils::ErrorCode::kOk;
    }

    // Use two Tensors as ping-pong buffers.  The first node reads from input.
    utils::Tensor buf_a;
    utils::Tensor buf_b;

    const utils::Tensor* cur_in = &input;
    utils::Tensor*       cur_out = &buf_a;

    for (size_t i = 0; i < nodes_.size(); ++i) {
        auto ret = nodes_[i]->Process(*cur_in, cur_out);
        if (ret != utils::ErrorCode::kOk) return ret;

        // Swap buffers for the next iteration.
        if (i + 1 < nodes_.size()) {
            cur_in  = cur_out;
            cur_out = (cur_out == &buf_a) ? &buf_b : &buf_a;
        }
    }

    *output = std::move(*cur_out);
    return utils::ErrorCode::kOk;
}

// static
Pipeline Pipeline::BuildInputPipeline(const utils::TensorInfo& target_info) {
    Pipeline p;

    // 1. uint8 → float32 type conversion.
    p.AddNode(std::make_unique<DtypeConvertNode>(utils::DataType::kFloat32));

    // 2. Spatial resize (only for NCHW targets where shape has 4 dims).
    if (target_info.layout == "NCHW" &&
        target_info.shape.size() == 4) {
        const int target_h = target_info.shape[kNchwHIdx];
        const int target_w = target_info.shape[kNchwWIdx];
        if (target_h > 0 && target_w > 0) {
            p.AddNode(std::make_unique<ResizeNode>(target_h, target_w));
        }
    }

    // 3. BGR → RGB channel swap (for 3-channel NCHW models).
    if (target_info.layout == "NCHW" &&
        target_info.shape.size() >= 2 &&
        target_info.shape[1] == kRgbChannels) {
        p.AddNode(std::make_unique<BGRToRGBNode>());
    }

    // 4. HWC → CHW layout transpose.
    if (target_info.layout == "NCHW") {
        p.AddNode(std::make_unique<HWCToCHWNode>());
    }

    // 5. Per-channel normalization.
    if (target_info.has_normalize &&
        !target_info.normalize.mean.empty() &&
        !target_info.normalize.std.empty()) {
        p.AddNode(std::make_unique<NormalizeNode>(
            target_info.normalize.mean,
            target_info.normalize.std));
    }

    return p;
}

}  // namespace pipeline
}  // namespace atlas
