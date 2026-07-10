#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "src/pipeline/pipeline_node.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

// Resizes the spatial dimensions of a 3-D HWC tensor (H × W × C) to
// (target_h × target_w × C) using bilinear interpolation.
// Supports float32 and uint8 inputs.
class ResizeNode : public IPipelineNode {
 public:
    ResizeNode(int target_h, int target_w);

    utils::ErrorCode Process(const Context& ctx,
                            const utils::Tensor& input, utils::Tensor* output) override;
    std::string_view Name() const override;

    // Creates a node from manifest params.
    // Required keys: "height" (int), "width" (int).
    // Returns nullptr if either key is missing or unparseable.
    static std::unique_ptr<IPipelineNode> CreateFromParams(
        const std::unordered_map<std::string, std::string>& params);

 private:
    int target_h_;
    int target_w_;
};

}  // namespace pipeline
}  // namespace atlas

