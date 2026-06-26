#pragma once

#include <string_view>

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
};

}  // namespace backend
}  // namespace atlas
