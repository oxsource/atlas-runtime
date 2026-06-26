#include "src/pipeline/pipeline_node_factory.h"

#include <algorithm>

namespace atlas {
namespace pipeline {

// static
PipelineNodeFactory& PipelineNodeFactory::Instance() {
    static PipelineNodeFactory instance;
    return instance;
}

void PipelineNodeFactory::Register(const std::string& name,
                                     NodeCreator creator) {
    creators_[name] = std::move(creator);
}

std::unique_ptr<IPipelineNode> PipelineNodeFactory::Create(
    const std::string& name,
    const std::unordered_map<std::string, std::string>& params) const {
    auto it = creators_.find(name);
    if (it == creators_.end()) return nullptr;
    return it->second(params);
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
