#pragma once

#include <string>
#include <vector>

#include "src/backend/base/i_backend_context.h"
#include "src/core/manifest_config.h"
#include "src/utils/span.h"
#include "src/utils/types.h"

namespace atlas {
namespace backend {

// Abstract interface that every inference backend must implement.
//
// Lifecycle:  Load() → GetInputBuffer() [optional] → Infer() [repeatable] → Unload()
// Thread safety: implementations are not required to be thread-safe.
//   Callers are responsible for external synchronization when sharing an
//   IBackend instance across threads.
class IBackend {
 public:
    virtual ~IBackend() = default;

    // Loads the model from |model_path| using the options in |config|.
    // |ctx| is an optional shared backend-type context (e.g. CpuBackendContext
    // holding Ort::Env).  Implementations should cast ctx to their concrete
    // type.  Passing nullptr is valid; backends fall back to creating their
    // own internal runtime resource.
    //
    // @return kOk on success; kInvalidArgument or kInferFailed on failure.
    virtual utils::ErrorCode Load(const std::string& model_path,
                                   const core::ModelConfig& config,
                                   IBackendContext* ctx = nullptr) = 0;

    // Runs one synchronous inference pass.
    // |inputs| and |outputs| must be pre-allocated by the caller.
    //
    // @return kOk on success; kNotInitialized if Load() was not called first;
    //         kInferFailed on runtime error.
    virtual utils::ErrorCode Infer(const std::vector<utils::Tensor>& inputs,
                                    std::vector<utils::Tensor>& outputs) = 0;

    // ── Metadata ─────────────────────────────────────────────────────────

    // Returns metadata for all input tensors.  Valid only after Load().
    virtual std::vector<utils::TensorInfo> GetInputInfo() const = 0;

    // Returns metadata for all output tensors.  Valid only after Load().
    virtual std::vector<utils::TensorInfo> GetOutputInfo() const = 0;

    // Returns metadata for the i-th input tensor.  Valid only after Load().
    // Default implementation delegates to GetInputInfo().
    // Backends that can efficiently provide per-index info should override.
    virtual utils::TensorInfo GetInputInfoAt(size_t index) const {
        auto infos = GetInputInfo();
        return index < infos.size() ? std::move(infos[index])
                                    : utils::TensorInfo{};
    }

    // Returns metadata for the i-th output tensor.  Valid only after Load().
    // Default implementation delegates to GetOutputInfo().
    virtual utils::TensorInfo GetOutputInfoAt(size_t index) const {
        auto infos = GetOutputInfo();
        return index < infos.size() ? std::move(infos[index])
                                    : utils::TensorInfo{};
    }

    // ── Zero-copy buffers ────────────────────────────────────────────────

    // Provides an external memory buffer for the i-th input tensor.
    // When set, the backend will use |external_mem| as the source of input
    // data instead of copying from the Tensor passed to Infer().
    // The caller must ensure |external_mem| remains valid until the next
    // Infer() call completes.
    // Returns kInvalidArgument if this backend does not support it.
    // Passing nullptr resets to internal buffer.
    virtual utils::ErrorCode SetInputBuffer(size_t index, void* external_mem,
                                             size_t byte_size) {
        (void)index;
        (void)external_mem;
        (void)byte_size;
        return utils::ErrorCode::kInvalidArgument;
    }

    // Returns a writable buffer for the i-th input tensor.
    // After Load(), callers may write input data directly into this buffer
    // and then call Infer() with the same data pointer for zero-copy input.
    // Returns an empty Span if not supported (default).
    virtual utils::Span<void> GetInputBuffer(size_t index) const {
        (void)index;
        return {};
    }

    // Returns a writable buffer for the i-th output tensor.
    // After Infer(), the buffer contains the inference result.
    // Returns an empty Span if not supported (default).
    virtual utils::Span<void> GetOutputBuffer(size_t index) const {
        (void)index;
        return {};
    }

    // ── Convenience methods ──────────────────────────────────────────────

    // Returns a Tensor backed by the internal input buffer for zero-copy input.
    // The returned Tensor shares the same data pointer as GetInputBuffer(index).
    // Callers write data directly into tensor.data, then pass the tensor to
    // Infer().  Returns an empty Tensor if GetInputBuffer returns empty.
    utils::Tensor GetInputTensor(size_t index) const {
        utils::Span<void> buf = GetInputBuffer(index);
        if (buf.data == nullptr) return {};

        utils::Tensor t;
        t.data      = buf.data;
        t.byte_size = buf.size;
        t.owns_data = false;
        t.info      = GetInputInfoAt(index);
        return t;  // NRVO
    }

    // Returns a Tensor backed by the internal output buffer.
    // After Infer(), tensor.data contains the inference result.
    // Returns an empty Tensor if GetOutputBuffer returns empty.
    utils::Tensor GetOutputTensor(size_t index) const {
        utils::Span<void> buf = GetOutputBuffer(index);
        if (buf.data == nullptr) return {};

        utils::Tensor t;
        t.data      = buf.data;
        t.byte_size = buf.size;
        t.owns_data = false;
        t.info      = GetOutputInfoAt(index);
        return t;
    }

    // ── Lifecycle ────────────────────────────────────────────────────────

    // Releases all model resources.  Safe to call even if Load() was not called.
    virtual void Unload() = 0;

    // Returns true if Load() completed successfully and Unload() has not been
    // called since.
    virtual bool IsLoaded() const = 0;

    // Returns the backend library version string.
    // Example: "1.17.3".
    virtual std::string Version() const = 0;
};

}  // namespace backend
}  // namespace atlas
