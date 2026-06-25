#pragma once

#include "src/pipeline/pipeline_node.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

// Converts the element data type of a tensor.
// For uint8 → float32, values are cast without scaling (range stays [0, 255]).
// For all other conversions, a simple static_cast is applied per element.
class DtypeConvertNode : public IPipelineNode {
 public:
    explicit DtypeConvertNode(utils::DataType target_dtype);

    utils::ErrorCode Process(const utils::Tensor& input,
                              utils::Tensor* output) override;
    std::string_view Name() const override;

 private:
    utils::DataType target_dtype_;
};

}  // namespace pipeline
}  // namespace atlas
