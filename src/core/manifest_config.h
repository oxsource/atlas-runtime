#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "src/profiler/profile_config.h"
#include "src/utils/types.h"

namespace atlas {
namespace core {

// Model loading strategy declared in the manifest.
enum class LoadStrategy {
    kEager = 0,  // Load at ModelManager::Init() time (default).
    kLazy,       // Load on first ModelHandle::Run() call.
};

// Describes one pipeline node declared in the manifest.
// |name| must be unique within a single pipeline array.
struct ManifestPipelineNode {
    std::string name;                                    // Node name, e.g. "resize"
    std::unordered_map<std::string, std::string> params; // Flat key-value params
};

// Manifest-layer tensor info with optional pipeline configuration.
// Separated from the public utils::TensorInfo to avoid leaking
// manifest-specific fields into the public header.
struct ManifestTensorInfo {
    std::string name;
    std::vector<int> shape;
    utils::DataType dtype = utils::DataType::kFloat32;
    std::string layout = "NCHW";
    bool has_normalize = false;
    utils::NormalizeParams normalize;
    // Quick toggle: when true, the pipeline list is ignored and identity
    // passthrough is used instead.  Useful for debugging without removing
    // the pipeline declaration.
    bool disable_pipeline = false;
    // Optional: explicit pipeline node chain.  Empty = identity passthrough.
    std::vector<ManifestPipelineNode> pipeline;

    // Converts to a public TensorInfo (drops pipeline config).
    utils::TensorInfo ToTensorInfo() const {
        utils::TensorInfo info;
        info.name          = name;
        info.shape         = shape;
        info.dtype         = dtype;
        info.layout        = layout;
        info.has_normalize = has_normalize;
        info.normalize     = normalize;
        return info;
    }
};

// Holds all configuration for a single model entry in the manifest.
// model_path has already had environment variables expanded.
struct ModelConfig {
    std::string id;
    std::string name;
    std::string backend;
    std::string model_path;
    LoadStrategy load_strategy = LoadStrategy::kEager;
    std::vector<ManifestTensorInfo> inputs;
    std::vector<ManifestTensorInfo> outputs;
    // Backend-specific key-value options declared under "config" in the manifest.
    std::unordered_map<std::string, std::string> config;
};

// Top-level result of parsing a manifest file.
struct ManifestConfig {
    std::string version;
    std::string name;
    ProfileConfig    profile;
    std::vector<ModelConfig> models;

    // Returns a pointer to the model with the given id, or nullptr if not found.
    const ModelConfig* FindModel(const std::string& id) const {
        for (const auto& m : models) {
            if (m.id == id) return &m;
        }
        return nullptr;
    }
};

}  // namespace core
}  // namespace atlas
