#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "atlas/atlas_export.h"
#include "atlas/types.h"

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
class ATLAS_API ModelHandle {
 public:
    ModelHandle() = default;

    // Returns true if this handle references a valid, loaded model entry.
    bool IsValid() const;

    // Runs the full inference chain:
    //   raw_input  ->  Pipeline (preprocessing)  ->  IBackend::Infer()
    //
    // @param raw_input  Source tensor (e.g. HWC uint8 image).
    // @param outputs    Populated with inference results on success.
    // @return kOk on success; kNotInitialized if IsValid() == false.
    utils::ErrorCode Run(const utils::Tensor& raw_input,
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

    // Returns the backend name, e.g. "cpu".  Returns empty string if
    // IsValid() == false.
    std::string GetBackend() const;

    // Returns the model file path with environment variables expanded.
    // Returns empty string if IsValid() == false.
    std::string GetModelPath() const;

    // Returns the load strategy: 0 = eager load at Init() time,
    // 1 = lazy load on first Run() call.  Returns 0 if IsValid() == false.
    int GetLoadStrategy() const;

    // Returns the backend-specific key-value config table.
    // Returns empty map if IsValid() == false.
    std::unordered_map<std::string, std::string> GetConfig() const;

 private:
    friend class AtlasRuntime;
    explicit ModelHandle(core::ModelEntry* entry);

    core::ModelEntry* entry_ = nullptr;  // non-owning
};

}  // namespace api
}  // namespace atlas
