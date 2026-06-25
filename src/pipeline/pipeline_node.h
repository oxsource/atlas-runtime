#pragma once

#include <string_view>

#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

// Abstract base for a single processing step in a Pipeline.
//
// Each node receives one input Tensor and writes its result into |output|.
// The node is responsible for allocating |output|'s data buffer when the
// transformation requires a new buffer.  When the transformation can be
// done in-place, the node may reuse the input buffer and set SupportsInPlace()
// to true; callers may then pass output == &input.
class IPipelineNode {
 public:
    virtual ~IPipelineNode() = default;

    // Processes |input| and writes the result into |output|.
    // |output| must not be nullptr.
    virtual utils::ErrorCode Process(const utils::Tensor& input,
                                      utils::Tensor* output) = 0;

    // Human-readable name used for logging / debugging.
    virtual std::string_view Name() const = 0;

    // Returns true if this node can operate in-place (output == &input).
    virtual bool SupportsInPlace() const { return false; }
};

}  // namespace pipeline
}  // namespace atlas
