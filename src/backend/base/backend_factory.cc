#include "src/backend/base/backend_factory.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace atlas {
namespace backend {

BackendFactory& BackendFactory::Instance() {
    static BackendFactory instance;
    return instance;
}

void BackendFactory::Register(const std::string& name, BackendCreator creator) {
    creators_[name] = std::move(creator);
}

std::unique_ptr<IBackend> BackendFactory::Create(
    const std::string& name) const {
    auto it = creators_.find(name);
    if (it == creators_.end()) {
        return nullptr;
    }
    return it->second();
}

std::vector<std::string> BackendFactory::ListBackends() const {
    std::vector<std::string> names;
    names.reserve(creators_.size());
    for (const auto& [name, _] : creators_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace backend
}  // namespace atlas
