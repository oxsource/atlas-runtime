#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "src/pipeline/pipeline_node.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

// Swaps channel 0 and channel 2 of a HWC tensor (RGB → BGR).
// Supports float32 and uint8 inputs.  Operates in-place.
class RGBToBGRNode : public IPipelineNode {
 public:
    RGBToBGRNode() = default;

    utils::ErrorCode Process(const Context& ctx,
                            const utils::Tensor& input, utils::Tensor* output) override;
    std::string_view Name() const override;
    bool SupportsInPlace() const override { return true; }

    // Creates a node from manifest params. No params required.
    static std::unique_ptr<IPipelineNode> CreateFromParams(
        const std::unordered_map<std::string, std::string>& params);
};

}  // namespace pipeline
}  // namespace atlas

