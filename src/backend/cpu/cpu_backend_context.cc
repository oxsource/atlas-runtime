#include "src/backend/cpu/cpu_backend_context.h"

#include <string_view>

#include "src/backend/base/backend_factory.h"

#define LOG_TAG "Atlas::CpuCtx"
#include "src/utils/logger.h"

namespace atlas {
namespace backend {
namespace cpu {

namespace {
constexpr std::string_view kBackendType = "cpu";
constexpr char kOrtLoggerName[] = "atlas_cpu";
}  // namespace

CpuBackendContext::CpuBackendContext() = default;

CpuBackendContext::~CpuBackendContext() = default;

std::string_view CpuBackendContext::BackendType() const {
    return kBackendType;
}

utils::ErrorCode CpuBackendContext::Init(
    const std::unordered_map<std::string, std::string>& config) {
    ATLAS_LOGD("%s called", __FUNCTION__);
    if (initialized_) return utils::ErrorCode::kOk;

    (void)config;
    env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, kOrtLoggerName);
    initialized_ = true;
    ATLAS_LOGI("CpuBackendContext initialized");
    return utils::ErrorCode::kOk;
}

}  // namespace cpu
}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND_CONTEXT("cpu", atlas::backend::cpu::CpuBackendContext)