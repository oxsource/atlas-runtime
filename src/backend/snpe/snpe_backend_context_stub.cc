#include "src/backend/snpe/snpe_backend_context.h"

#include <string_view>

#define LOG_TAG "Atlas::SnpeCtx_Stub"
#include "src/utils/logger.h"

namespace atlas {
namespace backend {
namespace snpe {

namespace {
constexpr std::string_view kBackendType = "snpe";
}  // namespace

// === Stub / fallback implementation (non-target platforms, no SNPE SDK) ===

SnpeBackendContext::SnpeBackendContext() = default;

SnpeBackendContext::~SnpeBackendContext() = default;

std::string_view SnpeBackendContext::BackendType() const {
    ATLAS_LOGD("SnpeBackendContext::BackendType (stub)");
    return kBackendType;
}

utils::ErrorCode SnpeBackendContext::Init(
    const std::unordered_map<std::string, std::string>& config) {
    ATLAS_LOGD("SnpeBackendContext::Init (stub)");
    (void)config;
    // No SNPE SDK on this platform — stub succeeds silently.
    initialized_ = true;
    ATLAS_LOGI("SnpeBackendContext initialized (stub, no SNPE)");
    return utils::ErrorCode::kOk;
}

}  // namespace snpe
}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND_CONTEXT("snpe", atlas::backend::snpe::SnpeBackendContext)