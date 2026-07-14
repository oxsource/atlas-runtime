#include "src/backend/snpe/snpe_backend_context.h"

#include <string_view>

// SNPE 2.x headers dropped the zdl:: prefix.

#include "SNPE/SNPEFactory.hpp"

#include "src/backend/base/backend_factory.h"

#define LOG_TAG "Atlas::SnpeCtx_V2"
#include "src/utils/logger.h"

namespace atlas {
namespace backend {
namespace snpe {

namespace {
constexpr std::string_view kBackendType = "snpe";
}  // namespace

// === Full implementation (SNPE 2.x: Linux aarch64 / Android arm64) ===

SnpeBackendContext::SnpeBackendContext() = default;

SnpeBackendContext::~SnpeBackendContext() {
    ATLAS_LOGD("%s called", __FUNCTION__);
    SNPE::SNPEFactory::terminateLogging();
}

std::string_view SnpeBackendContext::BackendType() const {
    return kBackendType;
}

utils::ErrorCode SnpeBackendContext::Init(
    const std::unordered_map<std::string, std::string>& config) {
    ATLAS_LOGD("%s called", __FUNCTION__);
    if (initialized_) return utils::ErrorCode::kOk;

    // Only global config fields (e.g. log level) would be extracted here;
    // per-model fields (runtime, performance_profile, use_buffer) are ignored.
    (void)config;

    SNPE::SNPEFactory::initializeLogging();
    initialized_ = true;
    ATLAS_LOGI("SnpeBackendContext initialized (SNPE 2.x)");
    return utils::ErrorCode::kOk;
}

}  // namespace snpe
}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND_CONTEXT("snpe", atlas::backend::snpe::SnpeBackendContext)