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
#include "src/backend/snpe/snpe_backend.h"
#include "src/backend/snpe/snpe_backend_context.h"

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

std::string SnpeTestModelPath() {
    const char* sample_model_dir = std::getenv("SAMPLE_MODEL_DIR");
    if (sample_model_dir != nullptr) {
        const std::string path =
            std::string(sample_model_dir) + "/identity_snpe.dlc";
        std::ifstream probe(path);
        if (probe.good()) return path;
    }

    const std::string repo_rel = "tests/backend/snpe/test_data/identity_1x3x4x4.dlc";
    {
        std::ifstream probe(repo_rel);
        if (probe.good()) return repo_rel;
    }

    const std::string sample_rel = "/tmp/snpe_sample_models/identity_snpe.dlc";
    {
        std::ifstream probe(sample_rel);
        if (probe.good()) return sample_rel;
    }

    const char* srcdir = std::getenv("TEST_SRCDIR");
    const char* ws     = std::getenv("TEST_WORKSPACE");
    if (srcdir && ws) {
        const std::string path =
            std::string(srcdir) + "/" + ws + "/" + repo_rel;
        std::ifstream probe(path);
        if (probe.good()) return path;
    }

    return {};
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

std::string SnpeSingleModelManifest(const std::string& model_path) {
    std::ostringstream ss;
    ss << R"({
    "version": "1.0",
    "name": "snpe-rt-test",
    "models": [{
        "id": "snpe_identity",
        "backend": "snpe",
        "model_path": ")" << model_path << R"(",
        "load_strategy": "eager",
        "inputs":  [{"name":"images","shape":[1,3,4,4],"dtype":"float32","layout":"NCHW"}],
        "outputs": [{"name":"output","shape":[1,3,4,4],"dtype":"float32"}],
        "config": {"runtime":"cpu"}
    }]
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

TEST(AtlasRuntimeTest, SnpeEndToEndInferenceUsesRuntimeReportedInputLayout) {
    const char* run_snpe_tests = std::getenv("ATLAS_RUN_SNPE_TESTS");
    if (run_snpe_tests == nullptr || std::string(run_snpe_tests) != "1") {
        GTEST_SKIP() << "Set ATLAS_RUN_SNPE_TESTS=1 to enable SNPE integration regression tests";
    }

    const std::string model_path = SnpeTestModelPath();
    if (model_path.empty()) {
        GTEST_SKIP() << "SNPE test model not available";
    }

    AtlasRuntime rt;
    auto manifest_path = WriteTempManifest(SnpeSingleModelManifest(model_path));
    auto init_ret = rt.Init(manifest_path);
    if (init_ret == utils::ErrorCode::kBackendNotFound) {
        GTEST_SKIP() << "SNPE backend not available on this platform";
    }
    ASSERT_EQ(init_ret, utils::ErrorCode::kOk);

    auto handle = rt.GetModel("snpe_identity");
    ASSERT_TRUE(handle.IsValid());

    const auto input_infos = handle.GetInputInfo();
    ASSERT_EQ(input_infos.size(), 1u);
    EXPECT_FALSE(input_infos[0].shape.empty());
    EXPECT_TRUE(input_infos[0].layout == "NHWC" ||
                input_infos[0].layout == "NCHW");

    constexpr uint8_t kFill = 100;
    auto input = MakeRawHWCImage(kFill);
    std::vector<utils::Tensor> outputs;
    ASSERT_EQ(handle.Run(input, &outputs), utils::ErrorCode::kOk);
    ASSERT_EQ(outputs.size(), 1u);

    const float* data = static_cast<const float*>(outputs[0].data);
    const size_t count = outputs[0].byte_size / sizeof(float);
    ASSERT_GT(count, 0u);
    bool has_non_zero = false;
    for (size_t i = 0; i < count; ++i) {
        if (data[i] != 0.0f) {
            has_non_zero = true;
            break;
        }
    }
    EXPECT_TRUE(has_non_zero);
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
