#pragma once

#include <memory>
#include <string>
#include <vector>

#include "src/backend/base/i_backend.h"
#include "src/backend/base/i_backend_context.h"
#include "src/core/manifest_config.h"
#include "src/utils/types.h"

namespace atlas {
namespace backend {

class SnpeBackendContext;

// SNPE inference backend — loads .dlc models and runs inference
// on Qualcomm DSP / GPU / AIP.
//
// Register via ATLAS_REGISTER_BACKEND("snpe", SnpeBackend).
//
// On target platforms (Linux aarch64 / Android arm64) with SNPE SDK available,
// SnpeImpl holds the full SNPE network handle and builder resources.
// On non-target platforms (stub), SnpeImpl is an empty dummy struct.
class SnpeBackend : public IBackend {
 public:
    SnpeBackend();
    ~SnpeBackend() override;

    utils::ErrorCode Load(const std::string& model_path,
                           const core::ModelConfig& config,
                           IBackendContext* ctx = nullptr) override;

    utils::ErrorCode Infer(const std::vector<utils::Tensor>& inputs,
                            std::vector<utils::Tensor>& outputs) override;

    std::vector<utils::TensorInfo> GetInputInfo()  const override;
    std::vector<utils::TensorInfo> GetOutputInfo() const override;

    void Unload()   override;
    bool IsLoaded() const override;
    std::string Version() const override;

 private:
    // Reads input/output metadata from the loaded SNPE network into
    // input_info_ / output_info_ and populates impl_->input_names /
    // impl_->output_names.
    utils::ErrorCode BuildTensorInfos();

    // Non-owning pointer to the shared SNPE context (borrowed from ModelManager).
    SnpeBackendContext* active_ctx_ = nullptr;

    std::vector<utils::TensorInfo> input_info_;
    std::vector<utils::TensorInfo> output_info_;

    bool loaded_ = false;

    // Opaque implementation struct:
    //   - v1.cc / v2.cc: defined with SNPE SDK types
    //   - stub.cc: defined as empty dummy struct
    struct SnpeImpl;
    std::unique_ptr<SnpeImpl> impl_;
};

}  // namespace backend
}  // namespace atlas