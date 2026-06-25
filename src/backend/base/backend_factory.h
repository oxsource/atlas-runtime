#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/backend/base/i_backend.h"

namespace atlas {
namespace backend {

// Factory function type used to create IBackend instances.
using BackendCreator = std::function<std::unique_ptr<IBackend>()>;

// Singleton registry that maps backend name strings to creator functions.
//
// Each backend registers itself at program startup via ATLAS_REGISTER_BACKEND.
// BackendFactory::Create() is then used by ModelManager to instantiate the
// correct backend for a given manifest entry.
class BackendFactory {
 public:
    // Returns the global singleton instance.
    static BackendFactory& Instance();

    // Registers a backend under |name|.  If |name| is already registered, the
    // previous entry is replaced.  Intended to be called from static
    // initializers via ATLAS_REGISTER_BACKEND.
    void Register(const std::string& name, BackendCreator creator);

    // Creates a new backend instance for |name|.
    // Returns nullptr if |name| has not been registered.
    std::unique_ptr<IBackend> Create(const std::string& name) const;

    // Returns a sorted list of all registered backend names.
    std::vector<std::string> ListBackends() const;

 private:
    BackendFactory() = default;
    ~BackendFactory() = default;
    BackendFactory(const BackendFactory&) = delete;
    BackendFactory& operator=(const BackendFactory&) = delete;

    std::unordered_map<std::string, BackendCreator> creators_;
};

// Convenience macro for registering a backend from its own translation unit.
// Place this macro at the end of the backend's .cc file, outside any class.
//
// Example:
//   ATLAS_REGISTER_BACKEND("onnx", OnnxBackend)
#define ATLAS_REGISTER_BACKEND(name, cls)                               \
    namespace {                                                          \
    const bool kRegistered_##cls = []() {                               \
        ::atlas::backend::BackendFactory::Instance().Register(          \
            name, []() { return std::make_unique<cls>(); });            \
        return true;                                                     \
    }();                                                                 \
    }  // namespace

}  // namespace backend
}  // namespace atlas
