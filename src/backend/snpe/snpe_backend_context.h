#pragma once

#include <string_view>
#include <unordered_map>

#include "src/backend/base/i_backend_context.h"
#include "src/utils/types.h"

namespace atlas {
namespace backend {

// Shared SNPE runtime context for all SnpeBackend instances.
//
// Holds global SNPE runtime resources (e.g. logging, platform runtime handles)
// that are shared across all SNPE model instances within a single manifest.
//
// Init() is called by SnpeBackend::Load() — it is idempotent so repeated
// calls from multiple per-model Load() invocations are safe.
class SnpeBackendContext : public IBackendContext {
 public:
    SnpeBackendContext();
    ~SnpeBackendContext() override = default;

    std::string_view BackendType() const override;

    // Initializes global SNPE runtime resources.
    // |config| — per-model config map; only global fields (e.g. log level) are
    // consumed. Per-model fields (runtime, performance_profile, use_buffer) are
    // ignored and extracted directly by SnpeBackend::Load() instead.
    //
    // Idempotent: subsequent calls after the first are no-ops.
    utils::ErrorCode Init(const std::unordered_map<std::string,
                          std::string>& config);

 private:
#ifdef ATLAS_SNPE_ENABLED
    bool initialized_ = false;
#endif
};

}  // namespace backend
}  // namespace atlas