#include "src/core/model_manager.h"

#include <string>
#include <unordered_set>

#include "src/backend/base/backend_factory.h"
#include "src/backend/base/profiling_backend.h"
#include "src/pipeline/pipeline.h"
#include "src/utils/types.h"

#define LOG_TAG "Atlas::ModelManager"
#include "src/utils/logger.h"

namespace atlas {
namespace core {

ModelManager::~ModelManager() { ReleaseAll(); }

utils::ErrorCode ModelManager::Init(const ManifestConfig& manifest) {
    ATLAS_LOGD("Init called: name=%s, models=%zu",
               manifest.name.c_str(), manifest.models.size());

    std::lock_guard<std::mutex> lock(mutex_);
    ReleaseAll();

    auto& factory = backend::BackendFactory::Instance();

    // Step 1: collect unique backend types and create one context each.
    std::unordered_set<std::string> seen_types;
    for (const auto& model : manifest.models) {
        if (seen_types.insert(model.backend).second) {
            ATLAS_LOGD("creating context for backend: %s", model.backend.c_str());
            auto ctx = factory.CreateContext(model.backend);
            if (ctx != nullptr) {
                contexts_.emplace(model.backend, std::move(ctx));
                ATLAS_LOGD("context created for backend: %s", model.backend.c_str());
            }
            // No context is also valid — backend falls back to own resource.
        }
    }

    // Step 2: build ModelEntry for each model.
    for (const auto& model : manifest.models) {
        ATLAS_LOGD("building entry: id=%s, backend=%s, strategy=%s",
                   model.id.c_str(), model.backend.c_str(),
                   model.load_strategy == LoadStrategy::kLazy ? "lazy" : "eager");

        // Insert entry into the map first so ProfileConfig lives at a
        // permanent address.  Profiler::config_ will point to
        // entry.profile inside the map and remain valid for the lifetime
        // of the entry.
        auto [it, inserted] = entries_.emplace(model.id, ModelEntry{});
        if (!inserted) {
            ATLAS_LOGE("duplicate model id: %s", model.id.c_str());
            ReleaseAll();
            return utils::ErrorCode::kInvalidArgument;
        }
        ModelEntry& entry = it->second;
        entry.config  = model;
        entry.profile = manifest.profile;

        // Create Profiler, pointing to entry.profile (permanent map address).
        std::unique_ptr<backend::Profiler> profiler;
        if (manifest.profile.enabled) {
            profiler = std::make_unique<backend::Profiler>(entry.profile, model.id);
        }

        // Create the raw backend instance.
        auto backend_instance = factory.Create(model.backend);
        if (backend_instance == nullptr) {
            ATLAS_LOGE("backend not found: %s", model.backend.c_str());
            ReleaseAll();
            return utils::ErrorCode::kBackendNotFound;
        }

        // Wrap with ProfilingBackend if profiling is enabled.
        // ProfilingBackend receives a raw pointer to Profiler (via
        // unique_ptr::get()): moving the unique_ptr into entry.profiler
        // preserves the heap object's address, so the raw pointer stays
        // valid.
        if (profiler) {
            backend_instance = std::make_unique<backend::ProfilingBackend>(
                std::move(backend_instance), profiler.get());
        }

        entry.backend  = std::move(backend_instance);
        entry.profiler = std::move(profiler);

        // Build per-input preprocessing pipeline and per-output postprocessing
        // pipeline. Priority: explicit pipeline array > disable_pipeline flag
        // > auto-built input pipeline.
        for (const auto& input : model.inputs) {
            if (!input.pipeline.empty()) {
                entry.input_pipelines.push_back(
                    pipeline::Pipeline::BuildFromManifest(input.pipeline));
            } else if (input.disable_pipeline) {
                entry.input_pipelines.push_back(pipeline::Pipeline{});  // identity
            } else {
                entry.input_pipelines.push_back(
                    pipeline::Pipeline::BuildInputPipeline(input.ToTensorInfo()));
            }
        }
        for (const auto& output : model.outputs) {
            if (!output.pipeline.empty()) {
                entry.output_pipelines.push_back(
                    pipeline::Pipeline::BuildOutputFromManifest(output.pipeline));
            } else {
                entry.output_pipelines.push_back(pipeline::Pipeline{});
            }
        }
        entry.loaded = false;
    }

    // Step 3: eagerly load models whose strategy is kEager.
    for (auto& [id, entry] : entries_) {
        if (entry.config.load_strategy == LoadStrategy::kEager) {
            ATLAS_LOGD("eager-loading model: %s", id.c_str());
            auto ret = EnsureLoaded(&entry);
            if (ret != utils::ErrorCode::kOk) {
                ATLAS_LOGE("eager-load failed for model: %s (error=%d)",
                           id.c_str(), static_cast<int>(ret));
                ReleaseAll();
                return ret;
            }
            ATLAS_LOGD("eager-load success for model: %s", id.c_str());
        }
    }

    ATLAS_LOGD("Init complete: %zu model(s) ready", entries_.size());
    return utils::ErrorCode::kOk;
}

utils::ErrorCode ModelManager::GetEntry(const std::string& model_id,
                                         ModelEntry** entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(model_id);
    if (it == entries_.end()) {
        ATLAS_LOGE("model not found: %s", model_id.c_str());
        return utils::ErrorCode::kInvalidArgument;
    }
    ATLAS_LOGD("GetEntry: %s (loaded=%s)",
               model_id.c_str(), it->second.loaded ? "true" : "false");
    auto ret = EnsureLoaded(&it->second);
    if (ret != utils::ErrorCode::kOk) {
        ATLAS_LOGE("EnsureLoaded failed for model: %s (error=%d)",
                   model_id.c_str(), static_cast<int>(ret));
        return ret;
    }
    *entry = &it->second;
    return utils::ErrorCode::kOk;
}

void ModelManager::ReleaseAll() {
    ATLAS_LOGD("ReleaseAll called");
    for (auto& [id, entry] : entries_) {
        if (entry.loaded && entry.backend) {
            ATLAS_LOGD("unloading model: %s", id.c_str());
            entry.backend->Unload();
        }
        entry.loaded = false;
    }
    entries_.clear();
    contexts_.clear();
}

utils::ErrorCode ModelManager::EnsureLoaded(ModelEntry* entry) {
    if (entry->loaded) return utils::ErrorCode::kOk;

    ATLAS_LOGD("loading model: %s, path=%s, backend=%s",
               entry->config.id.c_str(),
               entry->config.model_path.c_str(),
               entry->config.backend.c_str());

    // Look up the shared context for this backend type (may be nullptr).
    backend::IBackendContext* ctx = nullptr;
    auto ctx_it = contexts_.find(entry->config.backend);
    if (ctx_it != contexts_.end()) {
        ctx = ctx_it->second.get();
    }

    auto ret = entry->backend->Load(entry->config.model_path,
                                     entry->config,
                                     ctx);
    if (ret == utils::ErrorCode::kOk) {
        const auto backend_input_infos = entry->backend->GetInputInfo();
        for (size_t i = 0; i < entry->config.inputs.size(); ++i) {
            const auto& input = entry->config.inputs[i];
            if (!input.pipeline.empty() || input.disable_pipeline) {
                continue;
            }
            if (i < backend_input_infos.size()) {
                auto target_info = backend_input_infos[i];
                target_info.name = input.name;
                target_info.has_normalize = input.has_normalize;
                target_info.normalize = input.normalize;
                entry->input_pipelines[i] =
                    pipeline::Pipeline::BuildInputPipeline(target_info);
            }
        }
        entry->loaded = true;
        ATLAS_LOGD("model loaded successfully: %s", entry->config.id.c_str());
    } else {
        ATLAS_LOGE("model load failed: %s (error=%d)",
                   entry->config.id.c_str(), static_cast<int>(ret));
    }
    return ret;
}

}  // namespace core
}  // namespace atlas
