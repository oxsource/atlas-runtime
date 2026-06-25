#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "src/utils/types.h"

namespace atlas {
namespace core {

// Holds all configuration for a single model entry in the manifest.
// model_path has already had environment variables expanded.
struct ModelConfig {
    std::string id;
    std::string name;
    std::string backend;
    std::string model_path;
    std::vector<utils::TensorInfo> inputs;
    std::vector<utils::TensorInfo> outputs;
    // Backend-specific key-value options declared under "config" in the manifest.
    std::unordered_map<std::string, std::string> config;
};

// Top-level result of parsing a manifest file.
struct ManifestConfig {
    std::string version;
    std::string name;
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
