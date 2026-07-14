#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/backend/base/i_backend.h"
#include "src/backend/base/i_backend_context.h"

namespace atlas {
namespace backend {

// Factory function types.
using BackendCreator        = std::function<std::unique_ptr<IBackend>()>;
using BackendContextCreator = std::function<std::unique_ptr<IBackendContext>()>;

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

    // Registers a context creator for |name|.  Called via
    // ATLAS_REGISTER_BACKEND_CONTEXT.  If |name| is already registered,
    // the previous entry is replaced.
    void RegisterContext(const std::string& name,
                          BackendContextCreator creator);

    // Creates a new shared context for |name|.
    // Returns nullptr if no context creator is registered for |name|.
    std::unique_ptr<IBackendContext> CreateContext(
        const std::string& name) const;

 private:
    BackendFactory() = default;
    ~BackendFactory() = default;
    BackendFactory(const BackendFactory&) = delete;
    BackendFactory& operator=(const BackendFactory&) = delete;

    std::unordered_map<std::string, BackendCreator>        creators_;
    std::unordered_map<std::string, BackendContextCreator> context_creators_;
};

// Convenience macro for registering a backend from its own translation unit.
// Place this macro at the end of the backend's .cc file, outside any namespace.
// |name| is the string key (e.g. "cpu"); |cls| is the fully-qualified class
// name (e.g. atlas::backend::cpu::CpuBackend).
//
// __COUNTER__ is used to generate a unique variable name so that |cls| may
// contain namespace separators (::) without breaking token-paste (##).
//
// Example:
//   ATLAS_REGISTER_BACKEND("cpu", atlas::backend::cpu::CpuBackend)
#define ATLAS_REGISTER_BACKEND_IMPL_(name, cls, counter)                \
    namespace {                                                           \
    const bool kAtlasBackendRegistered_##counter = []() {               \
        ::atlas::backend::BackendFactory::Instance().Register(           \
            name, []() { return std::make_unique<cls>(); });             \
        return true;                                                      \
    }();                                                                  \
    }  // namespace

#define ATLAS_REGISTER_BACKEND(name, cls)  \
    ATLAS_REGISTER_BACKEND_IMPL_(name, cls, __COUNTER__)

// Registers a context creator.  Use at the end of a backend context .cc file.
// Example:
//   ATLAS_REGISTER_BACKEND_CONTEXT("cpu", atlas::backend::cpu::CpuBackendContext)
#define ATLAS_REGISTER_BACKEND_CONTEXT_IMPL_(name, ctx_cls, counter)         \
    namespace {                                                                \
    const bool kAtlasCtxRegistered_##counter = []() {                        \
        ::atlas::backend::BackendFactory::Instance().RegisterContext(          \
            name, []() { return std::make_unique<ctx_cls>(); });              \
        return true;                                                           \
    }();                                                                       \
    }  /* namespace */

#define ATLAS_REGISTER_BACKEND_CONTEXT(name, ctx_cls) \
    ATLAS_REGISTER_BACKEND_CONTEXT_IMPL_(name, ctx_cls, __COUNTER__)

}  // namespace backend
}  // namespace atlas
