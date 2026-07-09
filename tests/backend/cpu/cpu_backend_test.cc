#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/backend/base/backend_factory.h"
#include "src/backend/cpu/cpu_backend.h"
#include "src/core/manifest_config.h"
#include "src/utils/types.h"

namespace atlas {
namespace backend {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

constexpr std::string_view kModelFile   = "identity_1x3x4x4.onnx";
constexpr std::string_view kBackendName = "cpu";
constexpr int              kBatchSize   = 1;
constexpr int              kChannels    = 3;
constexpr int              kHeight      = 4;
constexpr int              kWidth       = 4;

std::string TestDataPath(std::string_view filename) {
    const std::string base = "tests/backend/cpu/test_data/";
    std::string path = base + std::string(filename);
    std::ifstream probe(path);
    if (probe.good()) return path;

    const char* srcdir = std::getenv("TEST_SRCDIR");
    const char* ws     = std::getenv("TEST_WORKSPACE");
    if (srcdir && ws) {
        return std::string(srcdir) + "/" + ws + "/" + base +
               std::string(filename);
    }
    return path;
}

// Returns a float32 Tensor with shape [N, C, H, W] filled with |fill_value|.
utils::Tensor MakeFloatTensor(int n, int c, int h, int w, float fill_value) {
    utils::Tensor t;
    t.info.dtype  = utils::DataType::kFloat32;
    t.info.shape  = {n, c, h, w};
    t.info.layout = "NCHW";
    t.byte_size   = static_cast<size_t>(n * c * h * w) * sizeof(float);
    t.data        = malloc(t.byte_size);
    t.owns_data   = true;
    float* ptr    = static_cast<float*>(t.data);
    const size_t count = t.byte_size / sizeof(float);
    for (size_t i = 0; i < count; ++i) ptr[i] = fill_value;
    return t;
}

core::ModelConfig MakeModelConfig(const std::string& model_path) {
    core::ModelConfig cfg;
    cfg.id         = "test_model";
    cfg.backend    = std::string(kBackendName);
    cfg.model_path = model_path;
    cfg.config["num_threads"] = "1";
    return cfg;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class CpuBackendTest : public ::testing::Test {
 protected:
    CpuBackend backend_;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(CpuBackendTest, InitialStateIsNotLoaded) {
    EXPECT_FALSE(backend_.IsLoaded());
}

TEST_F(CpuBackendTest, LoadValidModelSucceeds) {
    auto cfg = MakeModelConfig(TestDataPath(kModelFile));
    ASSERT_EQ(backend_.Load(cfg.model_path, cfg), utils::ErrorCode::kOk);
    EXPECT_TRUE(backend_.IsLoaded());
}

TEST_F(CpuBackendTest, LoadNonExistentModelFails) {
    auto cfg = MakeModelConfig("/nonexistent/model.onnx");
    EXPECT_NE(backend_.Load(cfg.model_path, cfg), utils::ErrorCode::kOk);
    EXPECT_FALSE(backend_.IsLoaded());
}

TEST_F(CpuBackendTest, GetInputInfoReturnsCorrectShape) {
    auto cfg = MakeModelConfig(TestDataPath(kModelFile));
    ASSERT_EQ(backend_.Load(cfg.model_path, cfg), utils::ErrorCode::kOk);

    auto inputs = backend_.GetInputInfo();
    ASSERT_EQ(inputs.size(), 1u);
    EXPECT_EQ(inputs[0].name, "images");
    ASSERT_EQ(inputs[0].shape.size(), 4u);
    EXPECT_EQ(inputs[0].shape[0], kBatchSize);
    EXPECT_EQ(inputs[0].shape[1], kChannels);
    EXPECT_EQ(inputs[0].shape[2], kHeight);
    EXPECT_EQ(inputs[0].shape[3], kWidth);
    EXPECT_EQ(inputs[0].dtype, utils::DataType::kFloat32);
}

TEST_F(CpuBackendTest, GetOutputInfoReturnsCorrectShape) {
    auto cfg = MakeModelConfig(TestDataPath(kModelFile));
    ASSERT_EQ(backend_.Load(cfg.model_path, cfg), utils::ErrorCode::kOk);

    auto outputs = backend_.GetOutputInfo();
    ASSERT_EQ(outputs.size(), 1u);
    EXPECT_EQ(outputs[0].shape[1], kChannels);
    EXPECT_EQ(outputs[0].dtype, utils::DataType::kFloat32);
}

TEST_F(CpuBackendTest, InferIdentityPreservesValues) {
    auto cfg = MakeModelConfig(TestDataPath(kModelFile));
    ASSERT_EQ(backend_.Load(cfg.model_path, cfg), utils::ErrorCode::kOk);

    constexpr float kFill = 3.14f;
    auto input = MakeFloatTensor(kBatchSize, kChannels, kHeight, kWidth, kFill);

    std::vector<utils::Tensor> inputs;
    inputs.push_back(std::move(input));
    std::vector<utils::Tensor> outputs;

    ASSERT_EQ(backend_.Infer(inputs, outputs), utils::ErrorCode::kOk);
    ASSERT_EQ(outputs.size(), 1u);

    utils::Span<const float> data(
        static_cast<const float*>(outputs[0].data),
        outputs[0].byte_size / sizeof(float));
    for (size_t i = 0; i < data.size; ++i) {
        EXPECT_FLOAT_EQ(data[i], kFill);
    }
}

TEST_F(CpuBackendTest, InferBeforeLoadReturnsNotInitialized) {
    std::vector<utils::Tensor> inputs;
    std::vector<utils::Tensor> outputs;
    EXPECT_EQ(backend_.Infer(inputs, outputs),
              utils::ErrorCode::kNotInitialized);
}

TEST_F(CpuBackendTest, UnloadResetsState) {
    auto cfg = MakeModelConfig(TestDataPath(kModelFile));
    ASSERT_EQ(backend_.Load(cfg.model_path, cfg), utils::ErrorCode::kOk);
    backend_.Unload();
    EXPECT_FALSE(backend_.IsLoaded());
    EXPECT_TRUE(backend_.GetInputInfo().empty());
    EXPECT_TRUE(backend_.GetOutputInfo().empty());
}

TEST_F(CpuBackendTest, BackendFactoryCreatesCpuBackend) {
    auto& factory = BackendFactory::Instance();
    auto b = factory.Create(std::string(kBackendName));
    ASSERT_NE(b, nullptr);
}

}  // namespace
}  // namespace backend
}  // namespace atlas
