#include "runtime/model_config.h"

namespace uvr {

std::string ToString(BackendType backend) {
  switch (backend) {
    case BackendType::Ort:
      return "ort";
    case BackendType::Snpe:
      return "snpe";
    case BackendType::Rknn:
      return "rknn";
    case BackendType::Qnn:
      return "qnn";
  }
  return "unknown";
}

std::string ToString(SessionPolicy policy) {
  switch (policy) {
    case SessionPolicy::Shared:
      return "shared";
    case SessionPolicy::ThreadLocal:
      return "thread_local";
    case SessionPolicy::SessionPool:
      return "session_pool";
  }
  return "unknown";
}

}  // namespace uvr
