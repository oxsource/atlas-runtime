#include "src/backend/snpe/snpe_backend.h"

#include <string>
#include <vector>

#include "src/backend/base/backend_factory.h"
#include "src/backend/snpe/snpe_backend_context.h"
#include "src/core/manifest_config.h"
#include "src/utils/types.h"

#define LOG_TAG "Atlas::SnpeBE_Stub"
#include "src/utils/logger.h"

namespace atlas {
namespace backend {

// ===================================================================
// === Stub implementation (non-target platforms) ===
// No SNPE SDK dependency; compiles cleanly on macOS / Linux x86_64.
// ===================================================================

// Empty SnpeImpl for stub — no SNPE SDK headers available.
struct SnpeBackend::SnpeImpl {};

SnpeBackend::SnpeBackend() : impl_(std::make_unique<SnpeImpl>()) {}

SnpeBackend::~SnpeBackend() {
    Unload();
}

utils::ErrorCode SnpeBackend::Load(const std::string& model_path,
                                    const core::ModelConfig& config,
                                    IBackendContext* ctx) {
    ATLAS_LOGD("SNPE stub: Load(%s) -> kBackendNotFound", model_path.c_str());
    (void)model_path;
    (void)config;
    (void)ctx;
    return utils::ErrorCode::kBackendNotFound;
}

utils::ErrorCode SnpeBackend::Infer(const std::vector<utils::Tensor>& inputs,
                                     std::vector<utils::Tensor>& outputs) {
    ATLAS_LOGD("SNPE stub: Infer(%zu inputs) -> kNotInitialized", inputs.size());
    (void)inputs;
    (void)outputs;
    return utils::ErrorCode::kNotInitialized;
}

std::vector<utils::TensorInfo> SnpeBackend::GetInputInfo() const {
    return {};
}

std::vector<utils::TensorInfo> SnpeBackend::GetOutputInfo() const {
    return {};
}

void SnpeBackend::Unload() {
    ATLAS_LOGD("SNPE stub: Unload");
    active_ctx_ = nullptr;
    loaded_ = false;
}

bool SnpeBackend::IsLoaded() const { return false; }

utils::Span<void> SnpeBackend::GetInputBuffer(size_t index) const {
    (void)index;
    return {};
}

utils::Span<void> SnpeBackend::GetOutputBuffer(size_t index) const {
    (void)index;
    return {};
}

std::string SnpeBackend::Version() const {
    return "stub";
}

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND("snpe", atlas::backend::SnpeBackend)