#include "runtime/runtime.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace uvr {

Runtime::Runtime(std::unique_ptr<IBackend> backend) : backend_(std::move(backend)) {}

Status Runtime::Init() {
  if (!backend_) {
    return Status::InvalidArgument("runtime backend is null");
  }
  return backend_->Init();
}

Status Runtime::LoadModels(const std::string& path) {
  if (!backend_) {
    return Status::InvalidArgument("runtime backend is null");
  }

  Status status = registry_.Load(path);
  if (!status.ok()) {
    return status;
  }

  for (const std::string& name : registry_.Names()) {
    const ModelConfig* config = registry_.Get(name);
    if (!config) {
      return Status::Internal("registry returned missing model: " + name);
    }
    if (config->backend != backend_->Info().type) {
      return Status::InvalidArgument("model backend " + ToString(config->backend) +
                                     " does not match runtime backend " +
                                     ToString(backend_->Info().type));
    }
    status = BuildSessionPool(*config);
    if (!status.ok()) {
      return status;
    }
  }
  return Status::Ok();
}

Status Runtime::GetSession(const std::string& name, SessionPtr& session) {
  const auto iter = pools_.find(name);
  if (iter == pools_.end()) {
    return Status::NotFound("session pool is not loaded: " + name);
  }
  return iter->second->Acquire(session);
}

Status Runtime::Run(const std::string& name, const TensorMap& inputs, TensorMap& outputs) {
  const auto iter = pools_.find(name);
  if (iter == pools_.end()) {
    return Status::NotFound("session pool is not loaded: " + name);
  }

  SessionPtr session;
  Status status = iter->second->Acquire(session);
  if (!status.ok()) {
    return status;
  }

  ScopedSession scoped(*iter->second, std::move(session));
  return scoped->Run(inputs, outputs);
}

Status Runtime::BuildSessionPool(const ModelConfig& config) {
  const int pool_size = std::max(1, config.session.pool_size);
  std::vector<SessionPtr> sessions;
  sessions.reserve(static_cast<size_t>(pool_size));

  for (int i = 0; i < pool_size; ++i) {
    SessionPtr session = backend_->CreateSession(config);
    if (!session) {
      return Status::Internal("backend failed to create session: " + config.name);
    }
    sessions.push_back(std::move(session));
  }

  pools_[config.name] = std::make_unique<SessionPool>(std::move(sessions));
  return Status::Ok();
}

}  // namespace uvr
