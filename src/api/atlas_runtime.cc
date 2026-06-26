#include "src/api/atlas_runtime.h"

#include <string>

#include "src/core/manifest_config.h"
#include "src/core/manifest_parser.h"
#include "src/core/model_manager.h"
#include "src/utils/types.h"

namespace atlas {
namespace api {

AtlasRuntime::AtlasRuntime()
    : parser_(std::make_unique<core::ManifestParser>()) {}
AtlasRuntime::~AtlasRuntime() { Release(); }

utils::ErrorCode AtlasRuntime::Init(const std::string& manifest_path) {
    Release();

    // Re-create parser if it was reset (e.g. after move).
    if (!parser_) parser_ = std::make_unique<core::ManifestParser>();

    core::ManifestConfig manifest;
    auto ret = parser_->Parse(manifest_path, &manifest);
    if (ret != utils::ErrorCode::kOk) return ret;

    manager_ = std::make_unique<core::ModelManager>();
    ret = manager_->Init(manifest);
    if (ret != utils::ErrorCode::kOk) {
        manager_.reset();
        return ret;
    }

    initialized_ = true;
    return utils::ErrorCode::kOk;
}

ModelHandle AtlasRuntime::GetModel(const std::string& model_id) {
    if (!initialized_ || manager_ == nullptr) {
        return ModelHandle{};
    }

    core::ModelEntry* entry = nullptr;
    auto ret = manager_->GetEntry(model_id, &entry);
    if (ret != utils::ErrorCode::kOk || entry == nullptr) {
        return ModelHandle{};
    }
    return ModelHandle{entry};
}

void AtlasRuntime::Release() {
    if (manager_) manager_->ReleaseAll();
    manager_.reset();
    initialized_ = false;
}

}  // namespace api
}  // namespace atlas
