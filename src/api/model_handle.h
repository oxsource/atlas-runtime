#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "src/utils/types.h"

namespace atlas {
namespace core { struct ModelEntry; }

namespace api {

// Lightweight handle to a single model's inference context.
//
// Returned by AtlasRuntime::GetModel().  The handle does NOT own the
// underlying resources — its lifetime must not exceed the AtlasRuntime
// instance that produced it.
//
// NOT thread-safe: do not call Run() concurrently on the same handle
// instance.
class ModelHandle {
 public:
    ModelHandle() = default;

    // Returns true if this handle references a valid, loaded model entry.
    bool IsValid() const;

    // Runs the full inference chain:
    //   raw_input  →  Pipeline (preprocessing)  →  IBackend::Infer()
    //
    // @param raw_input  Source tensor (e.g. HWC uint8 image).
    // @param outputs    Populated with inference results on success.
    // @return kOk on success; kNotInitialized if IsValid() == false.
    utils::ErrorCode Run(const utils::Tensor& raw_input,
                          std::vector<utils::Tensor>* outputs);

    // Multi-input overload: runs each input through its corresponding
    // pipeline, then passes all preprocessed inputs to IBackend::Infer().
    utils::ErrorCode Run(const std::vector<utils::Tensor>& raw_inputs,
                          std::vector<utils::Tensor>* outputs);

    // Returns input / output tensor metadata from the backend.
    // Both return empty vectors if IsValid() == false.
    std::vector<utils::TensorInfo> GetInputInfo()  const;
    std::vector<utils::TensorInfo> GetOutputInfo() const;
    utils::TensorInfo GetInputInfoAt(size_t index) const;
    utils::TensorInfo GetOutputInfoAt(size_t index) const;

    // Returns a Tensor backed by the backend's internal input buffer.
    // Callers write data directly into tensor.data, then pass the tensor
    // to Run() for zero-copy inference (no memcpy on the input path).
    // Returns an empty Tensor if the backend does not support this.
    utils::Tensor GetInputTensor(size_t index) const;

    // Returns a Tensor backed by the backend's internal output buffer.
    // After Run(), tensor.data contains the inference result with no extra
    // memcpy — the buffer is the backend's own output memory.
    // Returns an empty Tensor if the backend does not support this.
    utils::Tensor GetOutputTensor(size_t index) const;

    // Injects an external memory buffer as the input source for the given
    // input index (Level 3 external memory injection).
    //
    // After a successful call, subsequent Run() calls will read input data
    // from |external_mem| instead of copying from the Tensor passed to
    // Run().  The caller must ensure |external_mem| remains valid and
    // contains the correct input data before each Run().
    //
    // Passing nullptr for |external_mem| resets to the backend's internal
    // buffer.
    //
    // @return kOk on success; kInvalidArgument if the backend does not
    //         support external memory injection; kNotInitialized if the
    //         handle is invalid.
    utils::ErrorCode SetInputBuffer(size_t index, void* external_mem,
                                     size_t byte_size) const;

    std::string GetBackend() const;
    std::string GetModelPath() const;
    int GetLoadStrategy() const;
    std::unordered_map<std::string, std::string> GetConfig() const;

 private:
    friend class AtlasRuntime;
    explicit ModelHandle(core::ModelEntry* entry);

    core::ModelEntry* entry_ = nullptr;  // non-owning
};

}  // namespace api
}  // namespace atlas
