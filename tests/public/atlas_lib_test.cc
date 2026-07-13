// Verifies that the //src/public:atlas target can be independently linked
// and that the CpuBackend registration anchor (atlas_init.cc / alwayslink)
// is effective: AtlasRuntime::Init with a cpu-backend manifest must succeed,
// which implicitly proves the "cpu" backend is registered.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>

#include <gtest/gtest.h>

#include "atlas/atlas.h"

namespace {

// Returns the path to the test model, handling both local and Bazel sandbox.
std::string TestModelPath() {
    const std::string rel =
        "tests/backend/cpu/test_data/identity_1x3x4x4.onnx";
    std::ifstream probe(rel);
    if (probe.good()) return rel;
    const char* srcdir = std::getenv("TEST_SRCDIR");
    const char* ws     = std::getenv("TEST_WORKSPACE");
    if (srcdir && ws)
        return std::string(srcdir) + "/" + ws + "/" + rel;
    return rel;
}

// Writes a manifest JSON to a temp file and returns the path.
std::string WriteTempManifest(const std::string& json) {
    const std::string path = "/tmp/atlas_lib_test_manifest.json";
    std::ofstream f(path);
    f << json;
    return path;
}

// Builds a single-model manifest pointing at the test identity model.
std::string SingleModelManifest(const std::string& model_path) {
    return R"({
  "version": "1.0",
  "name": "lib-test",
  "models": [{
    "id": "identity",
    "backend": "cpu",
    "model_path": ")" + model_path + R"(",
    "load_strategy": "eager",
    "inputs":  [{"name":"images","shape":[1,3,4,4],"dtype":"float32","pipeline":[
      {"name":"atlas::dtype_convert","params":{"target":"float32"}},
      {"name":"atlas::hwc_to_chw"}
    ]}],
    "outputs": [{"name":"output","shape":[1,3,4,4],"dtype":"float32"}],
    "config": {"num_threads":"1"}
  }]
})";
}

}  // namespace

// Verify that AtlasRuntime can be constructed and initialized via the
// public API. If the CpuBackend were not linked (alwayslink / anchor
// failure), Init() would return kBackendNotFound.
TEST(AtlasLibTest, InitSucceedsViaPublicLib) {
    atlas::api::AtlasRuntime runtime;
    auto path = WriteTempManifest(SingleModelManifest(TestModelPath()));
    ASSERT_EQ(runtime.Init(path), atlas::utils::ErrorCode::kOk);
    EXPECT_TRUE(runtime.IsInitialized());
    runtime.Release();
}

// Verify end-to-end inference through the public API.
TEST(AtlasLibTest, EndToEndInferenceWorks) {
    atlas::api::AtlasRuntime runtime;
    auto path = WriteTempManifest(SingleModelManifest(TestModelPath()));
    ASSERT_EQ(runtime.Init(path), atlas::utils::ErrorCode::kOk);

    auto handle = runtime.GetModel("identity");
    ASSERT_TRUE(handle.IsValid());

    // Build a raw HWC uint8 image (4x4x3).
    atlas::utils::Tensor input;
    input.info.dtype  = atlas::utils::DataType::kUInt8;
    input.info.shape  = {4, 4, 3};
    input.info.layout = "HWC";
    input.byte_size   = 4 * 4 * 3;
    input.data        = std::malloc(input.byte_size);
    input.owns_data   = true;
    std::memset(input.data, 100, input.byte_size);

    std::vector<atlas::utils::Tensor> outputs;
    ASSERT_EQ(handle.Run(input, &outputs), atlas::utils::ErrorCode::kOk);
    ASSERT_EQ(outputs.size(), 1u);

    runtime.Release();
    std::remove(path.c_str());
}

// Verify version string is accessible via the public API.
TEST(AtlasLibTest, VersionStringAccessible) {
    const char* ver = atlas::utils::VersionString();
    ASSERT_NE(ver, nullptr);
    EXPECT_STRNE(ver, "");
}

// Verify Release allows re-Init.
TEST(AtlasLibTest, ReleaseAllowsReinit) {
    atlas::api::AtlasRuntime runtime;
    auto path = WriteTempManifest(SingleModelManifest(TestModelPath()));
    ASSERT_EQ(runtime.Init(path), atlas::utils::ErrorCode::kOk);
    runtime.Release();
    EXPECT_FALSE(runtime.IsInitialized());
    ASSERT_EQ(runtime.Init(path), atlas::utils::ErrorCode::kOk);
    EXPECT_TRUE(runtime.IsInitialized());
    runtime.Release();
    std::remove(path.c_str());
}