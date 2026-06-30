#include "src/backend/snpe/snpe_backend.h"

#include <string>
#include <vector>

#include "src/backend/base/backend_factory.h"
#include "src/backend/snpe/snpe_backend_context.h"
#include "src/utils/types.h"

namespace atlas {
namespace backend {

#ifdef ATLAS_SNPE_ENABLED

// === Full implementation (Linux aarch64 / Android arm64 + SNPE SDK) ===

// #include "zdl/SNPE/SNPE.hpp"
// #include "zdl/SNPE/SNPEFactory.hpp"
// #include "zdl/DlSystem/DlSystem.hpp"

SnpeBackend::SnpeBackend() = default;

SnpeBackend::~SnpeBackend() {
    Unload();
}

utils::ErrorCode SnpeBackend::Load(const std::string& model_path,
                                    const core::ModelConfig& config,
                                    IBackendContext* ctx) {
    Unload();

    // 1. Ensure shared context is initialized (idempotent).
    if (ctx != nullptr) {
        active_ctx_ = static_cast<SnpeBackendContext*>(ctx);
        active_ctx_->Init(config.config);
    }

    // 2. Extract per-model parameters from config.
    // auto runtime = config.config.count("runtime")
    //                    ? ParseRuntime(config.config.at("runtime"))
    //                    : Runtime_t::GPU;
    // auto profile = config.config.count("performance_profile")
    //                    ? ParseProfile(config.config.at("performance_profile"))
    //                    : PerformanceProfile_t::BALANCED;

    // 3. Build SNPE network instance.
    // TODO(pizzk): snpe_ = SNPEBuilder(...).build();

    loaded_ = true;
    return utils::ErrorCode::kOk;
}

utils::ErrorCode SnpeBackend::Infer(const std::vector<utils::Tensor>& inputs,
                                     std::vector<utils::Tensor>& outputs) {
    if (!loaded_) return utils::ErrorCode::kNotInitialized;

    // TODO(pizzk): Real SNPE inference execution.
    outputs.clear();
    return utils::ErrorCode::kOk;
}

std::vector<utils::TensorInfo> SnpeBackend::GetInputInfo() const {
    return input_info_;
}

std::vector<utils::TensorInfo> SnpeBackend::GetOutputInfo() const {
    return output_info_;
}

void SnpeBackend::Unload() {
    // TODO(pizzk): Release SNPE network instance.
    input_info_.clear();
    output_info_.clear();
    active_ctx_ = nullptr;
    loaded_ = false;
}

bool SnpeBackend::IsLoaded() const { return loaded_; }

#else

// === Stub implementation (non-target platforms) ===
// No SNPE SDK dependency; compiles cleanly on macOS / Linux x86_64.

SnpeBackend::SnpeBackend() = default;

SnpeBackend::~SnpeBackend() {
    Unload();
}

utils::ErrorCode SnpeBackend::Load(const std::string& model_path,
                                    const core::ModelConfig& config,
                                    IBackendContext* ctx) {
    return utils::ErrorCode::kBackendNotFound;
}

utils::ErrorCode SnpeBackend::Infer(const std::vector<utils::Tensor>& inputs,
                                     std::vector<utils::Tensor>& outputs) {
    return utils::ErrorCode::kNotInitialized;
}

std::vector<utils::TensorInfo> SnpeBackend::GetInputInfo() const {
    return {};
}

std::vector<utils::TensorInfo> SnpeBackend::GetOutputInfo() const {
    return {};
}

void SnpeBackend::Unload() {
    active_ctx_ = nullptr;
    loaded_ = false;
}

bool SnpeBackend::IsLoaded() const { return false; }

#endif

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND("snpe", atlas::backend::SnpeBackend)