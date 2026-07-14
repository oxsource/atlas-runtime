#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "src/utils/types.h"

namespace atlas {
namespace backend {

// Per-backend-type shared runtime resource.
// One instance is created per unique backend type in a manifest and shared
// across all IBackend instances of that type.
// Lifetime is owned and managed by ModelManager.
class IBackendContext {
 public:
    virtual ~IBackendContext() = default;

    // Returns the backend type string this context serves (e.g. "cpu").
    virtual std::string_view BackendType() const = 0;

    // Initializes the shared context (e.g. creates Ort::Env or SNPE runtime).
    // Called by ModelManager once after construction.  Idempotent:
    // implementations must handle repeated calls gracefully.
    // Default no-op returns kOk.
    virtual utils::ErrorCode Init(
        const std::unordered_map<std::string, std::string>& config) {
        (void)config;
        return utils::ErrorCode::kOk;
    }
};

}  // namespace backend
}  // namespace atlas
