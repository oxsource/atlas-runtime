#pragma once

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

 private:
    // Non-owning pointer to the shared SNPE context (borrowed from ModelManager).
    SnpeBackendContext* active_ctx_ = nullptr;

    std::vector<utils::TensorInfo> input_info_;
    std::vector<utils::TensorInfo> output_info_;

    bool loaded_ = false;
};

}  // namespace backend
}  // namespace atlas