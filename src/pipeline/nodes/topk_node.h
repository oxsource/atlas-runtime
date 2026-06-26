#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "src/pipeline/pipeline_node.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

// Finds the top-k largest values in a 1-D float32 tensor of shape [N].
//
// Output: a 1-D float32 tensor of shape [k] containing the top-k values
// in descending order.  (Indices are not emitted because the Pipeline
// contract only produces a single output tensor.)
//
// Required param: "k" (int, 1 <= k <= N).
class TopKNode : public IPipelineNode {
 public:
    explicit TopKNode(int k);

    utils::ErrorCode Process(const utils::Tensor& input,
                              utils::Tensor* output) override;
    std::string_view Name() const override;

    // Creates a node from manifest params.
    // Required key: "k" (positive int).
    static std::unique_ptr<IPipelineNode> CreateFromParams(
        const std::unordered_map<std::string, std::string>& params);

 private:
    int k_;
};

}  // namespace pipeline
}  // namespace atlas
