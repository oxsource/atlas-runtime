#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/pipeline/pipeline_node.h"

namespace atlas {
namespace pipeline {

// Factory function: creates a node from flat key-value params.
using NodeCreator = std::function<std::unique_ptr<IPipelineNode>(
    const std::unordered_map<std::string, std::string>& params)>;

// Registry mapping node name strings to creator functions.
//
// Each built-in node registers itself at program startup via
// ATLAS_REGISTER_PIPELINE_NODE.  Pipeline::BuildFromManifest() then
// uses Create() to instantiate nodes from manifest declarations.
class PipelineNodeFactory {
 public:
    // Returns the global singleton instance.
    static PipelineNodeFactory& Instance();

    // Registers a node creator under |name|.
    // If |name| is already registered, the previous entry is replaced.
    void Register(std::string_view name, NodeCreator creator);

    // Creates a new node instance for |name| with |params|.
    // Returns nullptr if |name| has not been registered.
    std::unique_ptr<IPipelineNode> Create(
        const std::string& name,
        const std::unordered_map<std::string, std::string>& params) const;

    // Returns a sorted list of all registered node names.
    std::vector<std::string> ListNodeNames() const;

 private:
    PipelineNodeFactory() = default;
    ~PipelineNodeFactory() = default;
    PipelineNodeFactory(const PipelineNodeFactory&) = delete;
    PipelineNodeFactory& operator=(const PipelineNodeFactory&) = delete;

    std::unordered_map<std::string, NodeCreator> creators_;
};

}  // namespace pipeline
}  // namespace atlas

// Convenience macro for registering a pipeline node from its own
// translation unit.  |name| is the string key (e.g. "resize");
// |cls| is the class name (e.g. ResizeNode).
// The class must implement a static method:
//   static std::unique_ptr<IPipelineNode> CreateFromParams(
//       const std::unordered_map<std::string, std::string>& params);
#define ATLAS_REGISTER_PIPELINE_NODE_IMPL_(name, cls, counter)             \
    namespace {                                                             \
    const bool kPipelineNodeRegistered_##counter = []() {                  \
        ::atlas::pipeline::PipelineNodeFactory::Instance().Register(        \
            name,                                                           \
            [](const std::unordered_map<std::string, std::string>& p) {     \
                return cls::CreateFromParams(p);                            \
            });                                                             \
        return true;                                                        \
    }();                                                                    \
    }  // namespace

#define ATLAS_REGISTER_PIPELINE_NODE(name, cls)  \
    ATLAS_REGISTER_PIPELINE_NODE_IMPL_(name, cls, __COUNTER__)
