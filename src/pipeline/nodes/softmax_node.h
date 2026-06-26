#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "src/pipeline/pipeline_node.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

// Applies softmax to a float32 tensor along a configurable axis
// (default -1, meaning the last dimension).
//
// For a 1-D tensor, softmax is computed over the entire flat array.
// For multi-dimensional tensors, softmax is computed independently
// along the specified axis.
//
// Supports in-place operation.
class SoftmaxNode : public IPipelineNode {
 public:
    // |axis| follows NumPy semantics: negative values count from the end.
    explicit SoftmaxNode(int axis = -1);

    utils::ErrorCode Process(const utils::Tensor& input,
                              utils::Tensor* output) override;
    std::string_view Name() const override;
    bool SupportsInPlace() const override { return true; }

    // Creates a node from manifest params.
    // Optional key: "axis" (int, default -1 = last dimension).
    static std::unique_ptr<IPipelineNode> CreateFromParams(
        const std::unordered_map<std::string, std::string>& params);

 private:
    int axis_;
};

}  // namespace pipeline
}  // namespace atlas
