#include "src/core/model_manager.h"

#include <string>
#include <unordered_set>

#include "src/backend/base/backend_factory.h"
#include "src/pipeline/pipeline.h"
#include "src/utils/types.h"

namespace atlas {
namespace core {

ModelManager::~ModelManager() { ReleaseAll(); }

utils::ErrorCode ModelManager::Init(const ManifestConfig& manifest) {
    std::lock_guard<std::mutex> lock(mutex_);
    ReleaseAll();

    auto& factory = backend::BackendFactory::Instance();

    // Step 1: collect unique backend types and create one context each.
    std::unordered_set<std::string> seen_types;
    for (const auto& model : manifest.models) {
        if (seen_types.insert(model.backend).second) {
            auto ctx = factory.CreateContext(model.backend);
            if (ctx != nullptr) {
                contexts_.emplace(model.backend, std::move(ctx));
            }
            // No context is also valid — backend falls back to own resource.
        }
    }

    // Step 2: build ModelEntry for each model.
    for (const auto& model : manifest.models) {
        auto backend_instance = factory.Create(model.backend);
        if (backend_instance == nullptr) {
            ReleaseAll();
            return utils::ErrorCode::kBackendNotFound;
        }

        ModelEntry entry;
        entry.config   = model;
        entry.backend  = std::move(backend_instance);
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

        entries_.emplace(model.id, std::move(entry));
    }

    // Step 3: eagerly load models whose strategy is kEager.
    for (auto& [id, entry] : entries_) {
        if (entry.config.load_strategy == LoadStrategy::kEager) {
            auto ret = EnsureLoaded(&entry);
            if (ret != utils::ErrorCode::kOk) {
                ReleaseAll();
                return ret;
            }
        }
    }

    return utils::ErrorCode::kOk;
}

utils::ErrorCode ModelManager::GetEntry(const std::string& model_id,
                                         ModelEntry** entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(model_id);
    if (it == entries_.end()) {
        return utils::ErrorCode::kInvalidArgument;
    }
    auto ret = EnsureLoaded(&it->second);
    if (ret != utils::ErrorCode::kOk) return ret;
    *entry = &it->second;
    return utils::ErrorCode::kOk;
}

void ModelManager::ReleaseAll() {
    for (auto& [id, entry] : entries_) {
        if (entry.loaded && entry.backend) {
            entry.backend->Unload();
        }
        entry.loaded = false;
    }
    entries_.clear();
    contexts_.clear();
}

utils::ErrorCode ModelManager::EnsureLoaded(ModelEntry* entry) {
    if (entry->loaded) return utils::ErrorCode::kOk;

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
    }
    return ret;
}

}  // namespace core
}  // namespace atlas
