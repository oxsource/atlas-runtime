#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uvr {

enum class DType {
  UINT8,
  INT8,
  INT16,
  INT32,
  INT64,
  FLOAT16,
  FLOAT32,
  FLOAT64,
};

enum class DeviceType {
  CPU,
  GPU,
  DSP,
  HTP,
  NPU,
};

enum class MemoryType {
  Host,
  Device,
  Shared,
  DmaBuf,
};

enum class Layout {
  NCHW,
  NHWC,
  CHW,
  HWC,
  NC,
  UNKNOWN,
};

struct AnyTensor {
  void* data = nullptr;
  std::vector<int64_t> shape;
  DType dtype = DType::FLOAT32;
  DeviceType device = DeviceType::CPU;
  MemoryType memory = MemoryType::Host;
  Layout layout = Layout::UNKNOWN;
  size_t bytes = 0;
};

using TensorMap = std::unordered_map<std::string, AnyTensor>;

size_t DTypeSize(DType dtype);
size_t ComputeTensorBytes(DType dtype, const std::vector<int64_t>& shape);
std::string ToString(DType dtype);
std::string ToString(DeviceType device);
std::string ToString(MemoryType memory);
std::string ToString(Layout layout);

}  // namespace uvr
