#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "src/pipeline/pipeline_node.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

// Converts a 3-D tensor from HWC layout (H × W × C) to CHW layout (C × H × W).
// The output shape is updated accordingly.
// Supports float32 and uint8 inputs.
class HWCToCHWNode : public IPipelineNode {
 public:
    HWCToCHWNode() = default;

    utils::ErrorCode Process(const Context& ctx,
                            const utils::Tensor& input, utils::Tensor* output) override;
    std::string_view Name() const override;

    // Creates a node from manifest params. No params required.
    static std::unique_ptr<IPipelineNode> CreateFromParams(
        const std::unordered_map<std::string, std::string>& params);
};

}  // namespace pipeline
}  // namespace atlas

