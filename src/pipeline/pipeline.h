#pragma once

#include <memory>
#include <vector>

#include "src/core/manifest_config.h"
#include "src/pipeline/pipeline_node.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

// Chains a sequence of IPipelineNode instances and runs them in order.
//
// Data flows through nodes one at a time; intermediate buffers are managed
// internally.  The final result is written into the |output| Tensor passed
// to Run().
class Pipeline {
 public:
    Pipeline() = default;

    // Appends |node| to the end of the processing chain.
    void AddNode(std::unique_ptr<IPipelineNode> node);

    // Returns true if no nodes have been added.
    bool IsEmpty() const { return nodes_.empty(); }

    // Returns the number of nodes in the pipeline.
    size_t NodeCount() const { return nodes_.size(); }

    // Runs all nodes in order.  |input| is the raw source tensor;
    // |output| receives the final processed tensor.
    // |ctx| is forwarded to every node's Process() call.
    // Returns kOk on success, or the error code from the first failing node.
    utils::ErrorCode Run(const utils::Tensor& input,
                        utils::Tensor* output, const IPipelineNode::Context& ctx = {}) const;

    // Builds a pipeline from explicit manifest node declarations.
    // Returns empty pipeline if any node name is unknown.
    static Pipeline BuildFromManifest(
        const std::vector<core::ManifestPipelineNode>& nodes);

    // Alias for BuildFromManifest used by output pipeline construction.
    static Pipeline BuildOutputFromManifest(
        const std::vector<core::ManifestPipelineNode>& nodes);

 private:
    std::vector<std::unique_ptr<IPipelineNode>> nodes_;
};

}  // namespace pipeline
}  // namespace atlas

