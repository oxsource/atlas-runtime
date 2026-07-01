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

#if ATLAS_SNPE_VERSION_MAJOR != 1 && ATLAS_SNPE_VERSION_MAJOR != 2
#error "ATLAS_SNPE_VERSION_MAJOR must be 1 or 2"
#endif

// SNPE 1.x headers may use a different include layout than 2.x.
// Both versions expose SNPEFactory.hpp and DlEnums.hpp under the same paths.
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

#if ATLAS_SNPE_VERSION_MAJOR >= 2
    // 2.x: DlSystem::LogLevel_t::LOG_WARN
    if (!SNPE::SNPEFactory::initializeLogging(
            DlSystem::LogLevel_t::LOG_WARN)) {
        return utils::ErrorCode::kInferFailed;
    }
#else
    // 1.x: logging level enum may use a different naming or may not
    // provide LogLevel_t. Fall back to conservative defaults.
    // SNPEFactory::initializeLogging() in 1.x takes no arguments.
    if (!SNPE::SNPEFactory::initializeLogging()) {
        return utils::ErrorCode::kInferFailed;
    }
#endif

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