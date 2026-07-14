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
namespace snpe {

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

    utils::Span<void> GetInputBuffer(size_t index) const override;
    utils::Span<void> GetOutputBuffer(size_t index) const override;
    utils::ErrorCode SetInputBuffer(size_t index, void* external_mem,
                                     size_t byte_size) override;

    std::vector<utils::TensorInfo> GetInputInfo()  const override;
    std::vector<utils::TensorInfo> GetOutputInfo() const override;

    void Unload()   override;
    bool IsLoaded() const override;
    std::string Version() const override;

 public:
    // Quantization parameters extracted from SNPE IBufferAttributes
    // at Load() time, used by the UserBuffer path.
    struct QuantParams {
        float    scale      = 1.0f;   // quantized_step_size
        int32_t  zero_point = 0;      // step_exactly_0
        uint32_t bandwidth  = 8;      // bits (TF8=8, TF16=16)
    };

 private:
    // Reads input/output metadata from the loaded SNPE network into
    // input_info_ / output_info_ / input_quant_params_ / output_quant_params_
    // and populates impl_->input_names / impl_->output_names.
    // This is the single source of truth for all tensor metadata used by
    // both ITensor and UserBuffer paths.
    utils::ErrorCode BuildTensorInfos();

    // ITensor-based inference path (use_buffer == false).
    utils::ErrorCode InferWithTensor(const std::vector<utils::Tensor>& inputs,
                                      std::vector<utils::Tensor>& outputs);

    // UserBuffer-based inference path (use_buffer == true).
    utils::ErrorCode InferWithBuffer(const std::vector<utils::Tensor>& inputs,
                                      std::vector<utils::Tensor>& outputs);

    // Stored model config (used for builder.setOutputTensors()).
    core::ModelConfig model_config_;

    // Non-owning pointer to the shared SNPE context (borrowed from ModelManager).
    SnpeBackendContext* active_ctx_ = nullptr;

    std::vector<utils::TensorInfo> input_info_;
    std::vector<utils::TensorInfo> output_info_;

    // Quantization params extracted in BuildTensorInfos() alongside
    // input_info_ / output_info_.  Used exclusively by the UserBuffer
    // path (CreateEncoding); ITensor path ignores them.
    std::vector<QuantParams> input_quant_params_;
    std::vector<QuantParams> output_quant_params_;

    bool loaded_ = false;

    // Opaque implementation struct:
    //   - v1.cc / v2.cc: defined with SNPE SDK types
    //   - stub.cc: defined as empty dummy struct
    struct SnpeImpl;
    std::unique_ptr<SnpeImpl> impl_;
};

}  // namespace snpe
}  // namespace backend
}  // namespace atlas