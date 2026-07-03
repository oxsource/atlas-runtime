#include "src/backend/base/backend_factory.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#define LOG_TAG "Atlas::BackendFactory"
#include "src/utils/logger.h"

namespace atlas {
namespace backend {

BackendFactory& BackendFactory::Instance() {
    static BackendFactory instance;
    return instance;
}

void BackendFactory::Register(const std::string& name, BackendCreator creator) {
    ATLAS_LOGD("registering backend: %s", name.c_str());
    creators_[name] = std::move(creator);
}

std::unique_ptr<IBackend> BackendFactory::Create(
    const std::string& name) const {
    auto it = creators_.find(name);
    if (it == creators_.end()) {
        ATLAS_LOGD("backend not registered: %s", name.c_str());
        return nullptr;
    }
    ATLAS_LOGD("creating backend instance: %s", name.c_str());
    return it->second();
}

std::vector<std::string> BackendFactory::ListBackends() const {
    std::vector<std::string> names;
    names.reserve(creators_.size());
    for (const auto& [name, _] : creators_) names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

void BackendFactory::RegisterContext(const std::string& name,
                                      BackendContextCreator creator) {
    ATLAS_LOGD("registering context: %s", name.c_str());
    context_creators_[name] = std::move(creator);
}

std::unique_ptr<IBackendContext> BackendFactory::CreateContext(
    const std::string& name) const {
    auto it = context_creators_.find(name);
    if (it == context_creators_.end()) {
        ATLAS_LOGD("context not registered: %s", name.c_str());
        return nullptr;
    }
    ATLAS_LOGD("creating context instance: %s", name.c_str());
    return it->second();
}

}  // namespace backend
}  // namespace atlas
