#pragma once

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

 private:
    friend class AtlasRuntime;
    explicit ModelHandle(core::ModelEntry* entry);

    core::ModelEntry* entry_ = nullptr;  // non-owning
};

}  // namespace api
}  // namespace atlas
