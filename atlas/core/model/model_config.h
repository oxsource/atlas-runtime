#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "runtime/any_tensor.h"

namespace uvr {

enum class BackendType {
  Ort,
  Snpe,
  Rknn,
  Qnn,
};

enum class SessionPolicy {
  Shared,
  ThreadLocal,
  SessionPool,
};

struct TensorConfig {
  std::string name;
  std::vector<int64_t> shape;
  DType dtype = DType::FLOAT32;
  Layout layout = Layout::UNKNOWN;
};

struct SessionConfig {
  DeviceType device = DeviceType::CPU;
  int threads = 1;
  int pool_size = 1;
  bool lazy_load = false;
  SessionPolicy policy = SessionPolicy::SessionPool;

  std::string runtime;
  bool user_buffer = false;
  bool enable_cache = false;
  std::string core_mask;
};

struct ModelConfig {
  std::string name;
  BackendType backend = BackendType::Ort;
  std::string model_path;
  std::string config_path;
  SessionConfig session;
  std::vector<TensorConfig> inputs;
  std::vector<TensorConfig> outputs;
};

struct ModelMeta {
  std::string name;
  std::string model_path;
  BackendType backend = BackendType::Ort;
  std::vector<TensorConfig> inputs;
  std::vector<TensorConfig> outputs;
};

struct BackendInfo {
  std::string name;
  BackendType type = BackendType::Ort;
  std::string version;
};

std::string ToString(BackendType backend);
std::string ToString(SessionPolicy policy);

}  // namespace uvr
