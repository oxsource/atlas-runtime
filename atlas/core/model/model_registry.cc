#include "runtime/model_registry.h"

#include <algorithm>
#include <filesystem>

#include "core/config/archive_parser.h"

namespace uvr {

Status ModelRegistry::Load(const std::string& path) {
  namespace fs = std::filesystem;

  const fs::path root(path);
  if (!fs::exists(root)) {
    return Status::NotFound("model path does not exist: " + path);
  }

  if (fs::is_regular_file(root)) {
    return LoadConfigFile(root.string());
  }

  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (entry.path().filename() == "config.yaml" ||
        entry.path().filename() == "config.yml") {
      Status status = LoadConfigFile(entry.path().string());
      if (!status.ok()) {
        return status;
      }
    }
  }
  return Status::Ok();
}

Status ModelRegistry::Reload(const std::string& name) {
  const auto iter = config_paths_.find(name);
  if (iter == config_paths_.end()) {
    return Status::NotFound("model is not registered: " + name);
  }
  return LoadConfigFile(iter->second);
}

bool ModelRegistry::Exists(const std::string& name) const {
  return models_.find(name) != models_.end();
}

const ModelConfig* ModelRegistry::Get(const std::string& name) const {
  const auto iter = models_.find(name);
  if (iter == models_.end()) {
    return nullptr;
  }
  return &iter->second;
}

std::vector<std::string> ModelRegistry::Names() const {
  std::vector<std::string> names;
  names.reserve(models_.size());
  for (const auto& item : models_) {
    names.push_back(item.first);
  }
  std::sort(names.begin(), names.end());
  return names;
}

Status ModelRegistry::LoadConfigFile(const std::string& config_path) {
  ModelConfig config;
  Status status = ParseModelArchive(config_path, config);
  if (!status.ok()) {
    return status;
  }
  config_paths_[config.name] = config_path;
  models_[config.name] = std::move(config);
  return Status::Ok();
}

}  // namespace uvr
