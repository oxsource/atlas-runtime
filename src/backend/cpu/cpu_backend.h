#pragma once

#include <memory>
#include <string>
#include <vector>

#include "onnxruntime_cxx_api.h"

#include "src/backend/base/i_backend.h"
#include "src/backend/base/i_backend_context.h"
#include "src/core/manifest_config.h"
#include "src/utils/types.h"

namespace atlas {
namespace backend {

// CPU inference backend implemented on top of ONNX Runtime.
// Register via ATLAS_REGISTER_BACKEND("cpu", CpuBackend).
class CpuBackend : public IBackend {
 public:
    CpuBackend();
    ~CpuBackend() override;

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
    // Reads input/output metadata from the loaded session into
    // input_info_ / output_info_ and populates input_names_ / output_names_.
    utils::ErrorCode BuildTensorInfos();

    // Converts an ONNX Runtime element type to atlas DataType.
    static utils::DataType OrtDtypeToAtlas(ONNXTensorElementDataType ort_type);

    // Owned fallback Env (created when ctx == nullptr).
    std::unique_ptr<Ort::Env>     own_env_;
    // Non-owning pointer to the active Env (either own_env_ or shared).
    Ort::Env*                     active_env_ = nullptr;
    std::unique_ptr<Ort::Session> session_;
    Ort::AllocatorWithDefaultOptions allocator_;

    // Owned name strings kept alive for the lifetime of session_.
    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;

    std::vector<utils::TensorInfo> input_info_;
    std::vector<utils::TensorInfo> output_info_;

    bool loaded_ = false;
};

}  // namespace backend
}  // namespace atlas
