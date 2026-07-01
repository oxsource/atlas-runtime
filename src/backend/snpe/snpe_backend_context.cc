#include "src/backend/snpe/snpe_backend_context.h"

#include <string_view>

#include "src/backend/base/backend_factory.h"

namespace atlas {
namespace backend {

namespace {
constexpr std::string_view kBackendType = "snpe";
}  // namespace

#ifdef ATLAS_SNPE_ENABLED

// === Full implementation (Linux aarch64 / Android arm64 + SNPE SDK) ===

#include "DlSystem/DlEnums.hpp"
#include "SNPE/SNPEFactory.hpp"

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

    // Initialize SNPE logging at WARN level. Only global config fields
    // (e.g. log_level) would be extracted from |config| here; per-model
    // fields (runtime, performance_profile, use_buffer) are ignored.
    (void)config;

    if (!SNPE::SNPEFactory::initializeLogging(
            DlSystem::LogLevel_t::LOG_WARN)) {
        return utils::ErrorCode::kInferFailed;
    }

    initialized_ = true;
    return utils::ErrorCode::kOk;
}

#else

// === Stub implementation (non-target platforms) ===

SnpeBackendContext::SnpeBackendContext() = default;

SnpeBackendContext::~SnpeBackendContext() = default;

std::string_view SnpeBackendContext::BackendType() const {
    return kBackendType;
}

utils::ErrorCode SnpeBackendContext::Init(
    const std::unordered_map<std::string, std::string>& config) {
    (void)config;
    return utils::ErrorCode::kBackendNotFound;
}

#endif

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND_CONTEXT("snpe", atlas::backend::SnpeBackendContext)