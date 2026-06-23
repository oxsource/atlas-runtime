#include "core/config/archive_parser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace uvr {
namespace {

enum class Section {
  kRoot,
  kSession,
  kInputs,
  kOutputs,
};

// YAML delimiters
constexpr char kCommentDelimiter  = '#';
constexpr char kKeyValueDelimiter = ':';

// YAML section markers
constexpr const char* kSectionSession  = "session:";
constexpr const char* kSectionInputs   = "inputs:";
constexpr const char* kSectionOutputs  = "outputs:";

// YAML config keys (root level)
constexpr const char* kKeyName    = "name";
constexpr const char* kKeyBackend = "backend";
constexpr const char* kKeyModel   = "model";

// YAML config keys (tensor fields)
constexpr const char* kKeyShape  = "shape";
constexpr const char* kKeyDtype  = "dtype";
constexpr const char* kKeyLayout = "layout";

// YAML config keys (session fields)
constexpr const char* kKeyDevice      = "device";
constexpr const char* kKeyThreads     = "threads";
constexpr const char* kKeyPoolSize    = "pool_size";
constexpr const char* kKeyLazyLoad    = "lazy_load";
constexpr const char* kKeyPolicy      = "policy";
constexpr const char* kKeyRuntime     = "runtime";
constexpr const char* kKeyUserBuffer  = "user_buffer";
constexpr const char* kKeyEnableCache = "enable_cache";
constexpr const char* kKeyCoreMask    = "core_mask";

// Boolean literal values
constexpr const char* kBoolTrue = "true";
constexpr const char* kBoolYes  = "yes";
constexpr const char* kBoolOne  = "1";

// YAML list item prefix
constexpr const char* kListItemPrefix = "- ";

// Data type identifiers (lowercase)
constexpr const char* kDtypeUint8   = "uint8";
constexpr const char* kDtypeInt8    = "int8";
constexpr const char* kDtypeInt16   = "int16";
constexpr const char* kDtypeInt32   = "int32";
constexpr const char* kDtypeInt64   = "int64";
constexpr const char* kDtypeFp16    = "fp16";
constexpr const char* kDtypeFloat16 = "float16";
constexpr const char* kDtypeFp64    = "fp64";
constexpr const char* kDtypeFloat64 = "float64";

// Layout identifiers (lowercase)
constexpr const char* kLayoutNchw = "nchw";
constexpr const char* kLayoutNhwc = "nhwc";
constexpr const char* kLayoutChw  = "chw";
constexpr const char* kLayoutHwc  = "hwc";
constexpr const char* kLayoutNc   = "nc";

// Device identifiers (lowercase)
constexpr const char* kDeviceGpu = "gpu";
constexpr const char* kDeviceDsp = "dsp";
constexpr const char* kDeviceHtp = "htp";
constexpr const char* kDeviceNpu = "npu";

// Backend identifiers (lowercase)
constexpr const char* kBackendSnpe = "snpe";
constexpr const char* kBackendRknn = "rknn";
constexpr const char* kBackendQnn  = "qnn";

// Session policy identifiers (lowercase)
constexpr const char* kPolicyShared      = "shared";
constexpr const char* kPolicyThreadLocal = "thread_local";
constexpr const char* kPolicyThreadlocal = "threadlocal";

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool StartsWith(const std::string& value, const std::string& prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string StripComment(const std::string& line) {
  const auto pos = line.find(kCommentDelimiter);
  return pos == std::string::npos ? line : line.substr(0, pos);
}

std::pair<std::string, std::string> SplitKeyValue(const std::string& line) {
  const auto pos = line.find(kKeyValueDelimiter);
  if (pos == std::string::npos) {
    return {TrimForConfig(line), ""};
  }
  return {TrimForConfig(line.substr(0, pos)), TrimForConfig(line.substr(pos + 1))};
}

bool ParseBool(const std::string& value) {
  const std::string lower = ToLower(TrimForConfig(value));
  return lower == kBoolTrue || lower == kBoolYes || lower == kBoolOne;
}

void ApplyTensorField(TensorConfig& tensor, const std::string& key,
                      const std::string& value) {
  if (key == kKeyName) {
    tensor.name = TrimForConfig(value);
  } else if (key == kKeyShape) {
    tensor.shape = ParseShapeForConfig(value);
  } else if (key == kKeyDtype) {
    tensor.dtype = ParseDTypeForConfig(value);
  } else if (key == kKeyLayout) {
    tensor.layout = ParseLayoutForConfig(value);
  }
}

void ApplySessionField(SessionConfig& session, const std::string& key,
                       const std::string& value) {
  if (key == kKeyDevice) {
    session.device = ParseDeviceForConfig(value);
  } else if (key == kKeyThreads) {
    session.threads = std::max(1, std::stoi(value));
  } else if (key == kKeyPoolSize) {
    session.pool_size = std::max(1, std::stoi(value));
  } else if (key == kKeyLazyLoad) {
    session.lazy_load = ParseBool(value);
  } else if (key == kKeyPolicy) {
    session.policy = ParseSessionPolicyForConfig(value);
  } else if (key == kKeyRuntime) {
    session.runtime = TrimForConfig(value);
  } else if (key == kKeyUserBuffer) {
    session.user_buffer = ParseBool(value);
  } else if (key == kKeyEnableCache) {
    session.enable_cache = ParseBool(value);
  } else if (key == kKeyCoreMask) {
    session.core_mask = TrimForConfig(value);
  }
}

}  // namespace

std::string TrimForConfig(std::string value) {
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

DType ParseDTypeForConfig(const std::string& value) {
  const std::string lower = ToLower(TrimForConfig(value));
  if (lower == kDtypeUint8) return DType::UINT8;
  if (lower == kDtypeInt8) return DType::INT8;
  if (lower == kDtypeInt16) return DType::INT16;
  if (lower == kDtypeInt32) return DType::INT32;
  if (lower == kDtypeInt64) return DType::INT64;
  if (lower == kDtypeFp16 || lower == kDtypeFloat16) return DType::FLOAT16;
  if (lower == kDtypeFp64 || lower == kDtypeFloat64) return DType::FLOAT64;
  return DType::FLOAT32;
}

Layout ParseLayoutForConfig(const std::string& value) {
  const std::string lower = ToLower(TrimForConfig(value));
  if (lower == kLayoutNchw) return Layout::NCHW;
  if (lower == kLayoutNhwc) return Layout::NHWC;
  if (lower == kLayoutChw) return Layout::CHW;
  if (lower == kLayoutHwc) return Layout::HWC;
  if (lower == kLayoutNc) return Layout::NC;
  return Layout::UNKNOWN;
}

DeviceType ParseDeviceForConfig(const std::string& value) {
  const std::string lower = ToLower(TrimForConfig(value));
  if (lower == kDeviceGpu) return DeviceType::GPU;
  if (lower == kDeviceDsp) return DeviceType::DSP;
  if (lower == kDeviceHtp) return DeviceType::HTP;
  if (lower == kDeviceNpu) return DeviceType::NPU;
  return DeviceType::CPU;
}

BackendType ParseBackendForConfig(const std::string& value) {
  const std::string lower = ToLower(TrimForConfig(value));
  if (lower == kBackendSnpe) return BackendType::Snpe;
  if (lower == kBackendRknn) return BackendType::Rknn;
  if (lower == kBackendQnn) return BackendType::Qnn;
  return BackendType::Ort;
}

SessionPolicy ParseSessionPolicyForConfig(const std::string& value) {
  const std::string lower = ToLower(TrimForConfig(value));
  if (lower == kPolicyShared) return SessionPolicy::Shared;
  if (lower == kPolicyThreadLocal || lower == kPolicyThreadlocal) {
    return SessionPolicy::ThreadLocal;
  }
  return SessionPolicy::SessionPool;
}

std::vector<int64_t> ParseShapeForConfig(const std::string& value) {
  std::string body = TrimForConfig(value);
  if (!body.empty() && body.front() == '[') {
    body.erase(body.begin());
  }
  if (!body.empty() && body.back() == ']') {
    body.pop_back();
  }

  std::vector<int64_t> shape;
  std::stringstream stream(body);
  std::string item;
  while (std::getline(stream, item, ',')) {
    item = TrimForConfig(item);
    if (!item.empty()) {
      shape.push_back(std::stoll(item));
    }
  }
  return shape;
}

Status ParseModelArchive(const std::string& config_path, ModelConfig& config) {
  std::ifstream input(config_path);
  if (!input.is_open()) {
    return Status::NotFound("failed to open config: " + config_path);
  }

  config = ModelConfig{};
  config.config_path = config_path;

  const std::filesystem::path base_dir = std::filesystem::path(config_path).parent_path();
  Section section = Section::kRoot;
  TensorConfig* current_tensor = nullptr;

  std::string raw_line;
  while (std::getline(input, raw_line)) {
    std::string line = TrimForConfig(StripComment(raw_line));
    if (line.empty()) {
      continue;
    }

    if (line == kSectionSession) {
      section = Section::kSession;
      current_tensor = nullptr;
      continue;
    }
    if (line == kSectionInputs) {
      section = Section::kInputs;
      current_tensor = nullptr;
      continue;
    }
    if (line == kSectionOutputs) {
      section = Section::kOutputs;
      current_tensor = nullptr;
      continue;
    }

    if (StartsWith(line, kListItemPrefix)) {
      TensorConfig tensor;
      auto [key, value] = SplitKeyValue(line.substr(2));
      ApplyTensorField(tensor, key, value);
      if (section == Section::kInputs) {
        config.inputs.push_back(std::move(tensor));
        current_tensor = &config.inputs.back();
      } else if (section == Section::kOutputs) {
        config.outputs.push_back(std::move(tensor));
        current_tensor = &config.outputs.back();
      }
      continue;
    }

    auto [key, value] = SplitKeyValue(line);
    if (section == Section::kSession) {
      ApplySessionField(config.session, key, value);
    } else if ((section == Section::kInputs || section == Section::kOutputs) && current_tensor) {
      ApplyTensorField(*current_tensor, key, value);
    } else if (key == kKeyName) {
      config.name = value;
    } else if (key == kKeyBackend) {
      config.backend = ParseBackendForConfig(value);
    } else if (key == kKeyModel) {
      std::filesystem::path model_path(value);
      if (model_path.is_relative()) {
        model_path = base_dir / model_path;
      }
      config.model_path = model_path.lexically_normal().string();
    }
  }

  if (config.name.empty()) {
    return Status::InvalidArgument("model config missing name: " + config_path);
  }
  if (config.model_path.empty()) {
    return Status::InvalidArgument("model config missing model path: " + config_path);
  }
  return Status::Ok();
}

}  // namespace uvr
