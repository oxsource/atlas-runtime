#include "src/backend/snpe/snpe_backend_context.h"

#include <string_view>

#include "src/backend/base/backend_factory.h"

namespace atlas {
namespace backend {

namespace {
constexpr std::string_view kBackendType = "snpe";
}  // namespace

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

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND_CONTEXT("snpe", atlas::backend::SnpeBackendContext)