#pragma once

#include <string_view>
#include <unordered_map>

#include "onnxruntime_cxx_api.h"

#include "src/backend/base/i_backend_context.h"
#include "src/utils/types.h"

namespace atlas {
namespace backend {
namespace cpu {

// Shared ONNX Runtime environment for all CpuBackend instances.
//
// The shared Ort::Env is created once during Init() and all CpuBackend
// instances (in the same manifest) borrow it via GetEnv().
class CpuBackendContext : public IBackendContext {
 public:
    CpuBackendContext();
    ~CpuBackendContext() override;

    std::string_view BackendType() const override;

    // Creates the shared Ort::Env.
    // Idempotent: subsequent calls after the first are no-ops.
    utils::ErrorCode Init(const std::unordered_map<std::string,
                          std::string>& config) override;

    // Returns the shared Ort::Env (valid only after Init() is called).
    Ort::Env& GetEnv() { return *env_; }

 private:
    bool initialized_ = false;
    std::unique_ptr<Ort::Env> env_;
};

}  // namespace cpu
}  // namespace backend
}  // namespace atlas