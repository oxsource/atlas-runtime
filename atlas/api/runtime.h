#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "runtime/backend.h"
#include "runtime/model_registry.h"
#include "runtime/session_pool.h"

namespace uvr {

class Runtime {
 public:
  explicit Runtime(std::unique_ptr<IBackend> backend);

  Status Init();
  Status LoadModels(const std::string& path);
  Status GetSession(const std::string& name, SessionPtr& session);
  Status Run(const std::string& name, const TensorMap& inputs, TensorMap& outputs);

  const ModelRegistry& Registry() const { return registry_; }
  const IBackend& Backend() const { return *backend_; }

 private:
  Status BuildSessionPool(const ModelConfig& config);

  std::unique_ptr<IBackend> backend_;
  ModelRegistry registry_;
  std::unordered_map<std::string, std::unique_ptr<SessionPool>> pools_;
};

std::unique_ptr<IBackend> CreateOrtBackend();

}  // namespace uvr
