#include "src/backend/snpe/snpe_backend_context.h"

#include <string_view>

#include "DlSystem/DlEnums.hpp"
#include "SNPE/SNPEFactory.hpp"

#include "src/backend/base/backend_factory.h"

namespace atlas {
namespace backend {

namespace {
constexpr std::string_view kBackendType = "snpe";
}  // namespace

// === Full implementation (SNPE 2.x: Linux aarch64 / Android arm64) ===

SnpeBackendContext::SnpeBackendContext() = default;

SnpeBackendContext::~SnpeBackendContext() {
    if (initialized_) {
        SNPE::SNPEFactory::terminateLogging();
    }
}

std::string_view SnpeBackendContext::BackendType() const {
    return kBackendType;
}

utils::ErrorCode SnpeBackendContext::Init(
    const std::unordered_map<std::string, std::string>& config) {
    if (initialized_) return utils::ErrorCode::kOk;

    // Only global config fields (e.g. log level) would be extracted here;
    // per-model fields (runtime, performance_profile, use_buffer) are ignored.
    (void)config;

    // 2.x: initializeLogging with LogLevel_t parameter.
    if (!SNPE::SNPEFactory::initializeLogging(
            DlSystem::LogLevel_t::LOG_WARN)) {
        return utils::ErrorCode::kInferFailed;
    }

    initialized_ = true;
    return utils::ErrorCode::kOk;
}

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND_CONTEXT("snpe", atlas::backend::SnpeBackendContext)