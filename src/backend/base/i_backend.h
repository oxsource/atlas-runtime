#pragma once

#include <string>
#include <vector>

#include "src/backend/base/i_backend_context.h"
#include "src/core/manifest_config.h"
#include "src/utils/types.h"

namespace atlas {
namespace backend {

// Abstract interface that every inference backend must implement.
//
// Lifecycle:  Load() → Infer() [repeatable] → Unload()
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

    // Returns metadata for all input tensors.  Valid only after Load().
    virtual std::vector<utils::TensorInfo> GetInputInfo() const = 0;

    // Returns metadata for all output tensors.  Valid only after Load().
    virtual std::vector<utils::TensorInfo> GetOutputInfo() const = 0;

    // Releases all model resources.  Safe to call even if Load() was not called.
    virtual void Unload() = 0;

    // Returns true if Load() completed successfully and Unload() has not been
    // called since.
    virtual bool IsLoaded() const = 0;
};

}  // namespace backend
}  // namespace atlas
