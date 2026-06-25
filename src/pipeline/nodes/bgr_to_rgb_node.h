#pragma once

#include "src/pipeline/pipeline_node.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

// Swaps channel 0 and channel 2 of a HWC tensor (BGR → RGB or RGB → BGR).
// Supports float32 and uint8 inputs.  Operates in-place.
class BGRToRGBNode : public IPipelineNode {
 public:
    BGRToRGBNode() = default;

    utils::ErrorCode Process(const utils::Tensor& input,
                              utils::Tensor* output) override;
    std::string_view Name() const override;
    bool SupportsInPlace() const override { return true; }
};

}  // namespace pipeline
}  // namespace atlas
