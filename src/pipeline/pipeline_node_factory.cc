#include "src/pipeline/pipeline_node_factory.h"

#include <algorithm>

namespace atlas {
namespace pipeline {

// static
PipelineNodeFactory& PipelineNodeFactory::Instance() {
    static PipelineNodeFactory instance;
    return instance;
}

void PipelineNodeFactory::Register(std::string_view name,
                                     NodeCreator creator) {
    creators_[std::string(name)] = std::move(creator);
}

std::unique_ptr<IPipelineNode> PipelineNodeFactory::Create(
    const std::string& name,
    const std::unordered_map<std::string, std::string>& params) const {
    // 1. Exact match.
    auto it = creators_.find(name);
    if (it != creators_.end()) return it->second(params);

    // 2. If the name already has a namespace prefix, don't fallback.
    if (name.find("::") != std::string::npos) return nullptr;

    // 3. Unprefixed name: try atlas:: prefix as fallback.
    const std::string prefixed = std::string("atlas::") + name;
    it = creators_.find(prefixed);
    if (it != creators_.end()) return it->second(params);

    return nullptr;
}

std::vector<std::string> PipelineNodeFactory::ListNodeNames() const {
    std::vector<std::string> names;
    names.reserve(creators_.size());
    for (const auto& [name, _] : creators_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace pipeline
}  // namespace atlas
