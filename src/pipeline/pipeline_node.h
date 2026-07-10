#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace atlas {
namespace core { struct ModelConfig; }
namespace backend { class IBackend; }
namespace utils {
enum class ErrorCode : int;
struct Tensor;
}  // namespace utils

namespace pipeline {

// ── Pipeline execution context flags ─────────────────────────
constexpr uint8_t kPipeFlagNone       = 0x00;
constexpr uint8_t kPipeFlagInputPipe  = 0x01;  // running in input pipeline
constexpr uint8_t kPipeFlagOutputPipe = 0x02;  // running in output pipeline
// Bits 2-7: reserved for user-defined flags

// Abstract base for a single processing step in a Pipeline.
//
// Each node receives one input Tensor and writes its result into |output|.
// The node is responsible for allocating |output|'s data buffer when the
// transformation requires a new buffer.  When the transformation can be
// done in-place, the node may reuse the input buffer and set SupportsInPlace()
// to true; callers may then pass output == &input.
class IPipelineNode {
 public:
    // ── Execution context passed to every Process() call ──────
    struct Context {
        const core::ModelConfig*  config   = nullptr;  // Model manifest config
        const backend::IBackend*  backend  = nullptr;  // Backend for zero-copy buffer access
        size_t                    input_index  = 0;  // Current input index
        size_t                    output_index = 0;  // Current output index
        uint8_t                   flags    = 0;        // Bitmask (see kPipeFlag*)
        void*                     args     = nullptr;  // User-defined extension data
    };

    virtual ~IPipelineNode() = default;

    // Processes |input| and writes the result into |output|.
    // |ctx| provides model config, backend buffer access, I/O index, etc.
    // |output| must not be nullptr.
    virtual utils::ErrorCode Process(const Context& ctx,
                                    const utils::Tensor& input, utils::Tensor* output) = 0;

    // Human-readable name used for logging / debugging.
    virtual std::string_view Name() const = 0;

    // Returns true if this node can operate in-place (output == &input).
    virtual bool SupportsInPlace() const { return false; }
};

}  // namespace pipeline
}  // namespace atlas

