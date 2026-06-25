#pragma once

#include <memory>
#include <vector>

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
    // Returns kOk on success, or the error code from the first failing node.
    utils::ErrorCode Run(const utils::Tensor& input,
                          utils::Tensor* output) const;

    // Builds a default input pre-processing pipeline from |target_info|.
    //
    // The pipeline converts a source image tensor (HWC uint8, BGR channel
    // order) into the format described by |target_info|.  Nodes are added
    // only when a transformation is actually required:
    //
    //   DtypeConvert  (uint8 → float32)
    //   Resize        (H × W → target H × W, from NCHW shape[2] / shape[3])
    //   BGRToRGB      (only when layout is NCHW and 3 channels)
    //   HWCToCHW      (only when layout is NCHW)
    //   Normalize     (only when target_info.has_normalize == true)
    static Pipeline BuildInputPipeline(const utils::TensorInfo& target_info);

 private:
    std::vector<std::unique_ptr<IPipelineNode>> nodes_;
};

}  // namespace pipeline
}  // namespace atlas
