#pragma once

#include <vector>

#include "src/pipeline/pipeline_node.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

// Per-channel normalization for float32 tensors in CHW layout.
//
// Formula applied to each element of channel c:
//   output[c][h][w] = (input[c][h][w] / 255.0f - mean[c]) / std[c]
//
// The division by 255 converts from the [0, 255] range produced by
// DtypeConvertNode into [0, 1] before subtracting the mean.
class NormalizeNode : public IPipelineNode {
 public:
    NormalizeNode(const std::vector<float>& mean, const std::vector<float>& std);

    utils::ErrorCode Process(const utils::Tensor& input,
                              utils::Tensor* output) override;
    std::string_view Name() const override;
    bool SupportsInPlace() const override { return true; }

 private:
    std::vector<float> mean_;
    std::vector<float> std_;
};

}  // namespace pipeline
}  // namespace atlas
