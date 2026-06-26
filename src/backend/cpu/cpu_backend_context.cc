#include "src/backend/cpu/cpu_backend_context.h"

#include "src/backend/base/backend_factory.h"

namespace atlas {
namespace backend {

namespace {
constexpr std::string_view kBackendType  = "cpu";
constexpr char             kOrtEnvName[] = "atlas_cpu_shared";
}  // namespace

CpuBackendContext::CpuBackendContext()
    : env_(std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING,
                                      kOrtEnvName)) {}

std::string_view CpuBackendContext::BackendType() const {
    return kBackendType;
}

Ort::Env& CpuBackendContext::GetEnv() { return *env_; }

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND_CONTEXT("cpu", atlas::backend::CpuBackendContext)
