#pragma once

#include <memory>
#include <string_view>

#include "onnxruntime_cxx_api.h"

#include "src/backend/base/i_backend_context.h"

namespace atlas {
namespace backend {

// Shared ONNX Runtime environment for all CpuBackend instances.
// Ort::Env is created once and borrowed by every CpuBackend::Load()
// that receives this context.
class CpuBackendContext : public IBackendContext {
 public:
    CpuBackendContext();
    ~CpuBackendContext() override = default;

    std::string_view BackendType() const override;

    // Returns the shared Ort::Env.  Callers must not outlive this context.
    Ort::Env& GetEnv();

 private:
    std::unique_ptr<Ort::Env> env_;
};

}  // namespace backend
}  // namespace atlas
