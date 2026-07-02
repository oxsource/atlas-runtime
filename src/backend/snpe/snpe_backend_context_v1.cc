#include "src/backend/snpe/snpe_backend_context.h"

#include <string_view>

// SNPE 1.x headers use the zdl/ nesting layout.
// SNPE/SNPEFactory types are in zdl::SNPE (aliased for uniform usage).
// DlSystem is at global scope in 1.x, not inside zdl.

#include "src/backend/base/backend_factory.h"

namespace atlas {
namespace backend {

namespace {
constexpr std::string_view kBackendType = "snpe";
}  // namespace

// === Full implementation (SNPE 1.x: Linux aarch64 / Android arm64) ===

SnpeBackendContext::SnpeBackendContext() = default;

SnpeBackendContext::~SnpeBackendContext() {
    // SNPE 1.x does not provide terminateLogging().
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

    // SNPE 1.x does not provide initializeLogging().
    initialized_ = true;
    return utils::ErrorCode::kOk;
}

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND_CONTEXT("snpe", atlas::backend::SnpeBackendContext)