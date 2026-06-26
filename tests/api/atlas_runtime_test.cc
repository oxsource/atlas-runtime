#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/api/atlas_runtime.h"
#include "src/api/model_handle.h"
#include "src/utils/types.h"

// Force registration of cpu backend and context via alwayslink deps.
#include "src/backend/cpu/cpu_backend.h"
#include "src/backend/cpu/cpu_backend_context.h"

namespace atlas {
namespace api {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string TestModelPath() {
    const std::string rel = "tests/backend/cpu/test_data/identity_1x3x4x4.onnx";
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
    const std::string path = "/tmp/atlas_runtime_test_manifest.json";
    std::ofstream f(path);
    f << json;
    return path;
}

std::string SingleModelManifest(const std::string& model_path,
                                  const std::string& strategy = "eager") {
    std::ostringstream ss;
    ss << R"({
  "version": "1.0",
  "name": "rt-test",
  "models": [{
    "id": "identity",
    "backend": "cpu",
    "model_path": ")" << model_path << R"(",
    "load_strategy": ")" << strategy << R"(",
    "inputs":  [{"name":"images","shape":[1,3,4,4],"dtype":"float32"}],
    "outputs": [{"name":"output","shape":[1,3,4,4],"dtype":"float32"}],
    "config": {"num_threads":"1"}
  }]
})";
    return ss.str();
}

std::string MultiModelManifest(const std::string& model_path) {
    std::ostringstream ss;
    ss << R"({
  "version": "1.0",
  "name": "rt-test-multi",
  "models": [
    {
      "id": "model_a",
      "backend": "cpu",
      "model_path": ")" << model_path << R"(",
      "load_strategy": "eager",
      "inputs":  [{"name":"images","shape":[1,3,4,4],"dtype":"float32"}],
      "outputs": [{"name":"output","shape":[1,3,4,4],"dtype":"float32"}],
      "config": {"num_threads":"1"}
    },
    {
      "id": "model_b",
      "backend": "cpu",
      "model_path": ")" << model_path << R"(",
      "load_strategy": "lazy",
      "inputs":  [{"name":"images","shape":[1,3,4,4],"dtype":"float32"}],
      "outputs": [{"name":"output","shape":[1,3,4,4],"dtype":"float32"}],
      "config": {"num_threads":"1"}
    }
  ]
})";
    return ss.str();
}

// Creates a raw HWC uint8 image tensor (the expected input to the pipeline).
// For the 4×4×3 identity model: pipeline converts HWC uint8 → NCHW float32.
utils::Tensor MakeRawHWCImage(uint8_t fill) {
    utils::Tensor t;
    t.info.dtype  = utils::DataType::kUInt8;
    t.info.shape  = {4, 4, 3};   // H=4, W=4, C=3 (HWC)
    t.info.layout = "HWC";
    t.byte_size   = 4 * 4 * 3;
    t.data        = malloc(t.byte_size);
    t.owns_data   = true;
    std::memset(t.data, fill, t.byte_size);
    return t;
}

// ---------------------------------------------------------------------------
// AtlasRuntime tests
// ---------------------------------------------------------------------------

TEST(AtlasRuntimeTest, InitValidManifestSucceeds) {
    AtlasRuntime rt;
    auto path = WriteTempManifest(SingleModelManifest(TestModelPath()));
    ASSERT_EQ(rt.Init(path), utils::ErrorCode::kOk);
    EXPECT_TRUE(rt.IsInitialized());
}

TEST(AtlasRuntimeTest, InitNonExistentFileReturnsFileNotFound) {
    AtlasRuntime rt;
    EXPECT_EQ(rt.Init("/nonexistent/manifest.json"),
              utils::ErrorCode::kFileNotFound);
    EXPECT_FALSE(rt.IsInitialized());
}

TEST(AtlasRuntimeTest, GetModelExistingIdReturnsValidHandle) {
    AtlasRuntime rt;
    auto path = WriteTempManifest(SingleModelManifest(TestModelPath()));
    ASSERT_EQ(rt.Init(path), utils::ErrorCode::kOk);

    auto handle = rt.GetModel("identity");
    EXPECT_TRUE(handle.IsValid());
}

TEST(AtlasRuntimeTest, GetModelNonExistentIdReturnsInvalidHandle) {
    AtlasRuntime rt;
    auto path = WriteTempManifest(SingleModelManifest(TestModelPath()));
    ASSERT_EQ(rt.Init(path), utils::ErrorCode::kOk);

    auto handle = rt.GetModel("no_such_model");
    EXPECT_FALSE(handle.IsValid());
}

TEST(AtlasRuntimeTest, GetModelBeforeInitReturnsInvalidHandle) {
    AtlasRuntime rt;
    EXPECT_FALSE(rt.GetModel("identity").IsValid());
}

TEST(AtlasRuntimeTest, EndToEndInferencePreservesValues) {
    AtlasRuntime rt;
    auto path = WriteTempManifest(SingleModelManifest(TestModelPath()));
    ASSERT_EQ(rt.Init(path), utils::ErrorCode::kOk);

    auto handle = rt.GetModel("identity");
    ASSERT_TRUE(handle.IsValid());

    // Pass a raw HWC uint8 image (4×4×3) — the pipeline converts it to
    // float32 NCHW [1,3,4,4] before inference.
    constexpr uint8_t kFill = 100;
    auto input = MakeRawHWCImage(kFill);

    std::vector<utils::Tensor> outputs;
    ASSERT_EQ(handle.Run(input, &outputs), utils::ErrorCode::kOk);
    ASSERT_EQ(outputs.size(), 1u);

    // After DtypeConvert (cast only, no /255), BGR2RGB (no-op for uniform
    // colour), HWCToCHW, and identity inference, all values should equal
    // static_cast<float>(kFill).
    const float expected = static_cast<float>(kFill);
    const float* data    = static_cast<const float*>(outputs[0].data);
    const size_t count   = outputs[0].byte_size / sizeof(float);
    for (size_t i = 0; i < count; ++i) {
        EXPECT_FLOAT_EQ(data[i], expected);
    }
}

TEST(AtlasRuntimeTest, ReleaseAllowsReinit) {
    AtlasRuntime rt;
    auto path = WriteTempManifest(SingleModelManifest(TestModelPath()));
    ASSERT_EQ(rt.Init(path), utils::ErrorCode::kOk);
    rt.Release();
    EXPECT_FALSE(rt.IsInitialized());
    ASSERT_EQ(rt.Init(path), utils::ErrorCode::kOk);
    EXPECT_TRUE(rt.IsInitialized());
}

TEST(AtlasRuntimeTest, MultiModelBothModelsRunSuccessfully) {
    AtlasRuntime rt;
    auto path = WriteTempManifest(MultiModelManifest(TestModelPath()));
    ASSERT_EQ(rt.Init(path), utils::ErrorCode::kOk);

    auto ha = rt.GetModel("model_a");
    auto hb = rt.GetModel("model_b");
    ASSERT_TRUE(ha.IsValid());
    ASSERT_TRUE(hb.IsValid());

    // Both models share the same CpuBackendContext (Ort::Env).
    // Use a raw HWC uint8 image — both pipelines will preprocess it.
    constexpr uint8_t kFill = 80;
    auto input = MakeRawHWCImage(kFill);
    std::vector<utils::Tensor> out_a, out_b;

    ASSERT_EQ(ha.Run(input, &out_a), utils::ErrorCode::kOk);
    ASSERT_EQ(hb.Run(input, &out_b), utils::ErrorCode::kOk);

    ASSERT_EQ(out_a.size(), 1u);
    ASSERT_EQ(out_b.size(), 1u);

    const float expected = static_cast<float>(kFill);
    const float* pa = static_cast<const float*>(out_a[0].data);
    EXPECT_FLOAT_EQ(pa[0], expected);
    const float* pb = static_cast<const float*>(out_b[0].data);
    EXPECT_FLOAT_EQ(pb[0], expected);
}

TEST(ModelHandleTest, DefaultHandleIsInvalid) {
    ModelHandle h;
    EXPECT_FALSE(h.IsValid());
}

TEST(ModelHandleTest, RunOnInvalidHandleReturnsNotInitialized) {
    ModelHandle h;
    auto input = MakeRawHWCImage(0);
    std::vector<utils::Tensor> outputs;
    EXPECT_EQ(h.Run(input, &outputs), utils::ErrorCode::kNotInitialized);
}

}  // namespace
}  // namespace api
}  // namespace atlas
