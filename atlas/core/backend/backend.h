#pragma once

#include "runtime/model_config.h"
#include "runtime/session.h"
#include "runtime/status.h"

namespace uvr {

class IBackend {
 public:
  virtual ~IBackend() = default;

  virtual Status Init() = 0;
  virtual SessionPtr CreateSession(const ModelConfig& config) = 0;
  virtual BackendInfo Info() const = 0;
};

}  // namespace uvr
