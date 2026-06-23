#include "backends/ort/ort_backend.h"

#include <utility>

namespace uvr {
namespace {

class OrtSession final : public ISession {
 public:
  explicit OrtSession(ModelConfig config) {
    meta_.name = std::move(config.name);
    meta_.model_path = std::move(config.model_path);
    meta_.backend = config.backend;
    meta_.inputs = std::move(config.inputs);
    meta_.outputs = std::move(config.outputs);
  }

  Status Run(const TensorMap& inputs, TensorMap& outputs) override {
    (void)outputs;
    if (inputs.empty()) {
      return Status::InvalidArgument("inputs must not be empty");
    }
    return Status::Unimplemented(
        "OrtBackend adapter is a buildable placeholder; link ONNX Runtime SDK in "
        "runtime/backends/ort for real inference");
  }

  const ModelMeta& Meta() const override { return meta_; }

  SessionCapability Capability() const override {
    SessionCapability capability;
    capability.thread_safe = true;
    capability.support_async = false;
    capability.support_batch = true;
    return capability;
  }

 private:
  ModelMeta meta_;
};

}  // namespace

Status OrtBackend::Init() {
  initialized_ = true;
  return Status::Ok();
}

SessionPtr OrtBackend::CreateSession(const ModelConfig& config) {
  if (!initialized_ || config.backend != BackendType::Ort) {
    return nullptr;
  }
  return std::make_shared<OrtSession>(config);
}

BackendInfo OrtBackend::Info() const {
  BackendInfo info;
  info.name = "onnxruntime";
  info.type = BackendType::Ort;
  info.version = "adapter-placeholder";
  return info;
}

std::unique_ptr<IBackend> CreateOrtBackend() {
  return std::make_unique<OrtBackend>();
}

}  // namespace uvr
