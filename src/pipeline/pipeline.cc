#include "src/pipeline/pipeline.h"

#include <memory>
#include <utility>

#include "src/pipeline/pipeline_node_factory.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

void Pipeline::AddNode(std::unique_ptr<IPipelineNode> node) {
    nodes_.push_back(std::move(node));
}

utils::ErrorCode Pipeline::Run(const utils::Tensor& input,
                              utils::Tensor* output,
                              const IPipelineNode::Context& ctx) const {
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
        auto ret = nodes_[i]->Process(ctx, *cur_in, cur_out);
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
Pipeline Pipeline::BuildFromManifest(
    const std::vector<core::ManifestPipelineNode>& nodes) {
    Pipeline p;
    auto& factory = PipelineNodeFactory::Instance();
    for (const auto& node_cfg : nodes) {
        auto node = factory.Create(node_cfg.name, node_cfg.params);
        if (node == nullptr) {
            return Pipeline{};  // Unknown node type
        }
        p.AddNode(std::move(node));
    }
    return p;
}

// static
Pipeline Pipeline::BuildOutputFromManifest(
    const std::vector<core::ManifestPipelineNode>& nodes) {
    return BuildFromManifest(nodes);
}

}  // namespace pipeline
}  // namespace atlas
