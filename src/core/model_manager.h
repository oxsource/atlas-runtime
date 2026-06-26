#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "src/backend/base/i_backend.h"
#include "src/backend/base/i_backend_context.h"
#include "src/core/manifest_config.h"
#include "src/pipeline/pipeline.h"
#include "src/utils/types.h"

namespace atlas {
namespace core {

// Runtime state for one model entry managed by ModelManager.
struct ModelEntry {
    ModelConfig                            config;
    std::unique_ptr<backend::IBackend>     backend;
    pipeline::Pipeline                     pipeline;
    bool                                   loaded = false;
};

// Manages the lifecycle of all models declared in a manifest.
//
// One IBackendContext is created per unique backend type and shared across
// all IBackend instances of that type.
//
// Thread safety: all public methods are thread-safe via an internal mutex.
// ModelHandle::Run() is NOT thread-safe — callers must not share a handle
// across threads.
class ModelManager {
 public:
    ModelManager() = default;
    ~ModelManager();

    // Initializes from a parsed manifest.
    // - Creates one IBackendContext per unique backend type.
    // - For kEager entries, calls IBackend::Load() immediately.
    // - For kLazy entries, defers loading to first GetEntry() call.
    // Returns kOk only if all eager models load successfully.
    // On failure, ReleaseAll() is called before returning.
    utils::ErrorCode Init(const ManifestConfig& manifest);

    // Returns a non-owning pointer to the ModelEntry for |model_id|.
    // For kLazy entries not yet loaded, triggers Load() on first call.
    // Returns kInvalidArgument if |model_id| is not found.
    utils::ErrorCode GetEntry(const std::string& model_id,
                               ModelEntry** entry);

    // Unloads all backends and clears internal state.
    void ReleaseAll();

 private:
    // Ensures the entry's backend is loaded.  Caller must hold mutex_.
    utils::ErrorCode EnsureLoaded(ModelEntry* entry);

    // One shared context per unique backend type string.
    std::unordered_map<std::string,
                       std::unique_ptr<backend::IBackendContext>> contexts_;
    std::unordered_map<std::string, ModelEntry> entries_;
    mutable std::mutex mutex_;
};

}  // namespace core
}  // namespace atlas
