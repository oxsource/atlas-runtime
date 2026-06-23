#include "runtime/any_tensor.h"

#include <numeric>
#include <stdexcept>

namespace uvr {

size_t DTypeSize(DType dtype) {
  switch (dtype) {
    case DType::UINT8:
    case DType::INT8:
      return 1;
    case DType::INT16:
    case DType::FLOAT16:
      return 2;
    case DType::INT32:
    case DType::FLOAT32:
      return 4;
    case DType::INT64:
    case DType::FLOAT64:
      return 8;
  }
  throw std::invalid_argument("unknown dtype");
}

size_t ComputeTensorBytes(DType dtype, const std::vector<int64_t>& shape) {
  if (shape.empty()) {
    return 0;
  }
  int64_t elements = 1;
  for (int64_t dim : shape) {
    if (dim <= 0) {
      return 0;
    }
    elements *= dim;
  }
  return static_cast<size_t>(elements) * DTypeSize(dtype);
}

std::string ToString(DType dtype) {
  switch (dtype) {
    case DType::UINT8:
      return "uint8";
    case DType::INT8:
      return "int8";
    case DType::INT16:
      return "int16";
    case DType::INT32:
      return "int32";
    case DType::INT64:
      return "int64";
    case DType::FLOAT16:
      return "fp16";
    case DType::FLOAT32:
      return "fp32";
    case DType::FLOAT64:
      return "fp64";
  }
  return "unknown";
}

std::string ToString(DeviceType device) {
  switch (device) {
    case DeviceType::CPU:
      return "cpu";
    case DeviceType::GPU:
      return "gpu";
    case DeviceType::DSP:
      return "dsp";
    case DeviceType::HTP:
      return "htp";
    case DeviceType::NPU:
      return "npu";
  }
  return "unknown";
}

std::string ToString(MemoryType memory) {
  switch (memory) {
    case MemoryType::Host:
      return "host";
    case MemoryType::Device:
      return "device";
    case MemoryType::Shared:
      return "shared";
    case MemoryType::DmaBuf:
      return "dmabuf";
  }
  return "unknown";
}

std::string ToString(Layout layout) {
  switch (layout) {
    case Layout::NCHW:
      return "nchw";
    case Layout::NHWC:
      return "nhwc";
    case Layout::CHW:
      return "chw";
    case Layout::HWC:
      return "hwc";
    case Layout::NC:
      return "nc";
    case Layout::UNKNOWN:
      return "unknown";
  }
  return "unknown";
}

}  // namespace uvr
