#include "runtime/runtime.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path MakeModelTree() {
  const auto root = std::filesystem::temp_directory_path() / "uvr_runtime_core_test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "face_det");

  std::ofstream config(root / "face_det" / "config.yaml");
  config << "name: face_det\n"
         << "backend: ort\n"
         << "model: model.onnx\n"
         << "session:\n"
         << "  device: cpu\n"
         << "  threads: 4\n"
         << "  pool_size: 2\n"
         << "  lazy_load: true\n"
         << "inputs:\n"
         << "  - name: input\n"
         << "    shape: [1,3,640,640]\n"
         << "    dtype: fp32\n"
         << "    layout: nchw\n"
         << "outputs:\n"
         << "  - name: scores\n"
         << "  - name: boxes\n";
  config.close();

  std::ofstream(root / "face_det" / "model.onnx").close();
  return root;
}

void TestTensorBytes() {
  assert(uvr::DTypeSize(uvr::DType::FLOAT32) == 4);
  assert(uvr::ComputeTensorBytes(uvr::DType::FLOAT32, {1, 3, 4, 5}) == 240);
}

void TestRegistryAndRuntime() {
  const auto root = MakeModelTree();

  uvr::Runtime runtime(uvr::CreateOrtBackend());
  uvr::Status status = runtime.Init();
  assert(status.ok());

  status = runtime.LoadModels(root.string());
  assert(status.ok());
  assert(runtime.Registry().Exists("face_det"));

  const uvr::ModelConfig* config = runtime.Registry().Get("face_det");
  assert(config != nullptr);
  assert(config->name == "face_det");
  assert(config->session.pool_size == 2);
  assert(config->inputs.size() == 1);
  assert(config->inputs[0].shape.size() == 4);
  assert(config->outputs.size() == 2);

  uvr::SessionPtr session;
  status = runtime.GetSession("face_det", session);
  assert(status.ok());
  assert(session != nullptr);
  assert(session->Meta().name == "face_det");
  assert(session->Capability().thread_safe);
}

}  // namespace

int main() {
  TestTensorBytes();
  TestRegistryAndRuntime();
  std::cout << "runtime_core_test passed\n";
  return 0;
}
