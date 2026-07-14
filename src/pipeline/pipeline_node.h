#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "atlas/types.h"

namespace atlas {
namespace api { class ModelHandle; }
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
        utils::UserData             user;               // User-defined extension data & pipeline flags

        // Returns the input index in the manifest (set by ModelHandle::Run()).
        size_t input_index()  const { return input_index_; }
        // Returns the output index in the manifest (set by ModelHandle::Run()).
        size_t output_index() const { return output_index_; }

        // Returns the 0-based index of this node in the pipeline (set by Pipeline::Run()).
        size_t node_index() const { return node_index_; }
        // Returns the total number of nodes in the pipeline (set by Pipeline::Run()).
        size_t node_count() const { return node_count_; }

        // Returns true if this node is the last one in the pipeline (endpoint).
        bool endpoint() const {
            return node_count_ > 0 && node_index_ == node_count_ - 1;
        }

        // If |force| is true, fills |tensor| with a view backed by the
        // backend's input buffer for the current input_index (zero-copy into
        // Infer) and returns true on success.  When the backend does not
        // support zero-copy, |tensor| is left unchanged and false is returned.
        // If |force| is false, |tensor| is never modified and false is
        // returned immediately.
        bool GetInputTensor(utils::Tensor& tensor, bool force) const;

        // If |force| is true, fills |tensor| with a view backed by the
        // backend's output buffer for the current output_index (zero-copy
        // post-processing) and returns true on success.  When the backend does
        // not support zero-copy, |tensor| is left unchanged and false is
        // returned.  If |force| is false, |tensor| is never modified and
        // false is returned immediately.
        bool GetOutputTensor(utils::Tensor& tensor, bool force) const;

     private:
        friend class Pipeline;
        friend class api::ModelHandle;
        size_t input_index_  = 0;
        size_t output_index_ = 0;
        size_t node_index_   = 0;
        size_t node_count_   = 0;
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

