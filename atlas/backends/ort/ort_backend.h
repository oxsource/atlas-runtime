#pragma once

#include <memory>

#include "runtime/backend.h"

namespace uvr {

class OrtBackend final : public IBackend {
 public:
  Status Init() override;
  SessionPtr CreateSession(const ModelConfig& config) override;
  BackendInfo Info() const override;

 private:
  bool initialized_ = false;
};

}  // namespace uvr
