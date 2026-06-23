#pragma once

#include <string>
#include <vector>

#include "runtime/model_config.h"
#include "runtime/status.h"

namespace uvr {

Status ParseModelArchive(const std::string& config_path, ModelConfig& config);

std::string TrimForConfig(std::string value);
DType ParseDTypeForConfig(const std::string& value);
Layout ParseLayoutForConfig(const std::string& value);
DeviceType ParseDeviceForConfig(const std::string& value);
BackendType ParseBackendForConfig(const std::string& value);
SessionPolicy ParseSessionPolicyForConfig(const std::string& value);
std::vector<int64_t> ParseShapeForConfig(const std::string& value);

}  // namespace uvr
