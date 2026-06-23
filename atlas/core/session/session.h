#pragma once

#include <memory>

#include "runtime/any_tensor.h"
#include "runtime/model_config.h"
#include "runtime/status.h"

namespace uvr {

struct SessionCapability {
  bool thread_safe = false;
  bool support_async = false;
  bool support_batch = false;
};

class ISession {
 public:
  virtual ~ISession() = default;

  virtual Status Run(const TensorMap& inputs, TensorMap& outputs) = 0;
  virtual const ModelMeta& Meta() const = 0;
  virtual SessionCapability Capability() const = 0;
};

using SessionPtr = std::shared_ptr<ISession>;

}  // namespace uvr
