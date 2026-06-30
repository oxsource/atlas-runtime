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

SnpeBackendContext::SnpeBackendContext() = default;

std::string_view SnpeBackendContext::BackendType() const {
    return kBackendType;
}

utils::ErrorCode SnpeBackendContext::Init(
    const std::unordered_map<std::string, std::string>& config) {
    if (initialized_) return utils::ErrorCode::kOk;
    // TODO(pizzk): Initialize SNPE global runtime (SNPEFactory::InitializeLogging, etc.)
    // using global config fields extracted from |config|.
    initialized_ = true;
    return utils::ErrorCode::kOk;
}

#else

// === Stub implementation (non-target platforms) ===

SnpeBackendContext::SnpeBackendContext() = default;

std::string_view SnpeBackendContext::BackendType() const {
    return kBackendType;
}

utils::ErrorCode SnpeBackendContext::Init(
    const std::unordered_map<std::string, std::string>& config) {
    return utils::ErrorCode::kBackendNotFound;
}

#endif

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND_CONTEXT("snpe", atlas::backend::SnpeBackendContext)