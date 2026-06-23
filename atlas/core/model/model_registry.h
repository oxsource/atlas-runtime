#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/model_config.h"
#include "runtime/status.h"

namespace uvr {

class ModelRegistry {
 public:
  Status Load(const std::string& path);
  Status Reload(const std::string& name);
  bool Exists(const std::string& name) const;
  const ModelConfig* Get(const std::string& name) const;
  std::vector<std::string> Names() const;

 private:
  Status LoadConfigFile(const std::string& config_path);

  std::unordered_map<std::string, ModelConfig> models_;
  std::unordered_map<std::string, std::string> config_paths_;
};

}  // namespace uvr
