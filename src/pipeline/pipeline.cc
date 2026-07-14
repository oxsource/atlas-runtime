#include "src/pipeline/pipeline.h"

#include <memory>
#include <utility>

#include "src/backend/base/i_backend.h"
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
        output->capacity  = input.capacity;
        output->owns_data = false;
        return utils::ErrorCode::kOk;
    }

    // Use two Tensors as ping-pong buffers.  The first node reads from input.
    utils::Tensor buf_a;
    utils::Tensor buf_b;

    const utils::Tensor* cur_in = &input;
    utils::Tensor*       cur_out = &buf_a;

    // Copy once; node_index_ / node_count_ will be updated in the loop.
    IPipelineNode::Context mutable_ctx = ctx;

    for (size_t i = 0; i < nodes_.size(); ++i) {
        mutable_ctx.node_index_ = i;
        mutable_ctx.node_count_ = nodes_.size();

        // Last node writes directly into the final output, skipping a final move.
        utils::Tensor* target = (i + 1 == nodes_.size()) ? output : cur_out;

        auto ret = nodes_[i]->Process(mutable_ctx, *cur_in, target);
        if (ret != utils::ErrorCode::kOk) return ret;

        if (i + 1 < nodes_.size()) {
            cur_in  = target;
            cur_out = (cur_out == &buf_a) ? &buf_b : &buf_a;
        }
    }

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

// ── IPipelineNode::Context method implementations ────────────

bool IPipelineNode::Context::GetInputTensor(utils::Tensor& tensor,
                                           bool force) const {
    if (!force) return false;
    if (backend == nullptr) return false;
    auto buf = backend->GetInputBuffer(input_index_);
    if (buf.data == nullptr) return false;
    auto info = backend->GetInputInfoAt(input_index_);
    tensor.data      = buf.data;
    tensor.byte_size = buf.size;
    tensor.capacity  = buf.size;
    tensor.owns_data = false;
    tensor.info      = std::move(info);
    return true;
}

bool IPipelineNode::Context::GetOutputTensor(utils::Tensor& tensor,
                                            bool force) const {
    if (!force) return false;
    if (backend == nullptr) return false;
    auto buf = backend->GetOutputBuffer(output_index_);
    if (buf.data == nullptr) return false;
    auto info = backend->GetOutputInfoAt(output_index_);
    tensor.data      = buf.data;
    tensor.byte_size = buf.size;
    tensor.capacity  = buf.size;
    tensor.owns_data = false;
    tensor.info      = std::move(info);
    return true;
}

}  // namespace pipeline
}  // namespace atlas
