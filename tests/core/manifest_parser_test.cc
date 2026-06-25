#include <cstdlib>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "src/backend/base/backend_factory.h"
#include "src/backend/base/i_backend.h"
#include "src/core/manifest_config.h"
#include "src/core/manifest_parser.h"
#include "src/utils/types.h"

namespace atlas {
namespace core {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Resolves a path relative to the test data directory, supporting both
// direct workspace-relative execution and Bazel runfiles.
std::string TestDataPath(const std::string& relative) {
    const std::string base = "tests/core/test_data/";
    std::string path = base + relative;

    // When running under Bazel the working directory is the runfiles root.
    // Try TEST_SRCDIR / TEST_WORKSPACE as a fallback.
    std::ifstream probe(path);
    if (probe.good()) return path;

    const char* srcdir = std::getenv("TEST_SRCDIR");
    const char* ws     = std::getenv("TEST_WORKSPACE");
    if (srcdir != nullptr && ws != nullptr) {
        return std::string(srcdir) + "/" + ws + "/" + base + relative;
    }
    return path;
}

// ---------------------------------------------------------------------------
// ManifestParser tests
// ---------------------------------------------------------------------------

class ManifestParserTest : public ::testing::Test {
 protected:
    ManifestParser parser_;
};

TEST_F(ManifestParserTest, ParsesValidManifest) {
    ManifestConfig config;
    auto ret = parser_.Parse(TestDataPath("valid_manifest.json"), &config);

    EXPECT_EQ(ret, utils::ErrorCode::kOk);
    EXPECT_EQ(config.version, "1.0");
    EXPECT_EQ(config.name, "test-app");
    ASSERT_EQ(config.models.size(), 1u);

    const ModelConfig& m = config.models[0];
    EXPECT_EQ(m.id, "detector");
    EXPECT_EQ(m.backend, "onnx");
    EXPECT_EQ(m.model_path, "/tmp/model.onnx");

    ASSERT_EQ(m.inputs.size(), 1u);
    EXPECT_EQ(m.inputs[0].name, "images");
    EXPECT_EQ(m.inputs[0].dtype, utils::DataType::kFloat32);
    ASSERT_EQ(m.inputs[0].shape.size(), 4u);
    EXPECT_EQ(m.inputs[0].shape[0], 1);
    EXPECT_EQ(m.inputs[0].shape[1], 3);

    ASSERT_EQ(m.outputs.size(), 1u);
    EXPECT_EQ(m.outputs[0].name, "output0");
}

TEST_F(ManifestParserTest, FindModelByIdReturnsCorrectEntry) {
    ManifestConfig config;
    ASSERT_EQ(parser_.Parse(TestDataPath("valid_manifest.json"), &config),
              utils::ErrorCode::kOk);

    const ModelConfig* found = config.FindModel("detector");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, "detector");

    EXPECT_EQ(config.FindModel("nonexistent"), nullptr);
}

TEST_F(ManifestParserTest, ReturnsParseErrorWhenVersionMissing) {
    ManifestConfig config;
    auto ret = parser_.Parse(TestDataPath("missing_version.json"), &config);
    EXPECT_EQ(ret, utils::ErrorCode::kParseError);
}

TEST_F(ManifestParserTest, ReturnsInvalidArgumentOnDuplicateId) {
    ManifestConfig config;
    auto ret = parser_.Parse(TestDataPath("duplicate_id.json"), &config);
    EXPECT_EQ(ret, utils::ErrorCode::kInvalidArgument);
}

TEST_F(ManifestParserTest, ReturnsParseErrorOnInvalidDtype) {
    ManifestConfig config;
    auto ret = parser_.Parse(TestDataPath("invalid_dtype.json"), &config);
    EXPECT_EQ(ret, utils::ErrorCode::kParseError);
}

TEST_F(ManifestParserTest, ReturnsVersionMismatchOnMajorVersionDifference) {
    ManifestConfig config;
    auto ret = parser_.Parse(TestDataPath("version_mismatch.json"), &config);
    EXPECT_EQ(ret, utils::ErrorCode::kVersionMismatch);
}

TEST_F(ManifestParserTest, ReturnsFileNotFoundForMissingFile) {
    ManifestConfig config;
    auto ret = parser_.Parse("/nonexistent/path/manifest.json", &config);
    EXPECT_EQ(ret, utils::ErrorCode::kFileNotFound);
}

TEST_F(ManifestParserTest, ReturnsInvalidArgumentForNullConfig) {
    auto ret = parser_.Parse(TestDataPath("valid_manifest.json"), nullptr);
    EXPECT_EQ(ret, utils::ErrorCode::kInvalidArgument);
}

TEST_F(ManifestParserTest, ExpandsEnvironmentVariableInModelPath) {
    // Set MODEL_DIR so the env-var manifest can be parsed.
    setenv("MODEL_DIR", "/opt/models", 1);

    // Reuse valid_manifest.json but construct an in-memory JSON string
    // to avoid a separate test-data file for this case.
    const std::string json_content = R"({
        "version": "1.0",
        "name": "env-app",
        "models": [{
            "id": "m",
            "backend": "onnx",
            "model_path": "${MODEL_DIR}/net.onnx",
            "inputs":  [{"name":"x","shape":[1,3,224,224],"dtype":"float32"}],
            "outputs": [{"name":"y","shape":[1,1000],"dtype":"float32"}]
        }]
    })";

    // Write to a temp file.
    const std::string tmp_path = "/tmp/atlas_env_test_manifest.json";
    {
        std::ofstream f(tmp_path);
        f << json_content;
    }

    ManifestConfig config;
    ASSERT_EQ(parser_.Parse(tmp_path, &config), utils::ErrorCode::kOk);
    EXPECT_EQ(config.models[0].model_path, "/opt/models/net.onnx");

    unsetenv("MODEL_DIR");
}

TEST_F(ManifestParserTest, ReturnsInvalidArgumentForUndefinedEnvVar) {
    unsetenv("ATLAS_UNDEFINED_VAR_XYZ");

    const std::string json_content = R"({
        "version": "1.0",
        "name": "env-app",
        "models": [{
            "id": "m",
            "backend": "onnx",
            "model_path": "${ATLAS_UNDEFINED_VAR_XYZ}/net.onnx",
            "inputs":  [{"name":"x","shape":[1,3,224,224],"dtype":"float32"}],
            "outputs": [{"name":"y","shape":[1,1000],"dtype":"float32"}]
        }]
    })";

    const std::string tmp_path = "/tmp/atlas_undef_env_manifest.json";
    {
        std::ofstream f(tmp_path);
        f << json_content;
    }

    ManifestConfig config;
    EXPECT_EQ(parser_.Parse(tmp_path, &config),
              utils::ErrorCode::kInvalidArgument);
}

// ---------------------------------------------------------------------------
// BackendFactory tests
// ---------------------------------------------------------------------------

// A minimal stub backend used only in factory registration tests.
class StubBackend : public backend::IBackend {
 public:
    utils::ErrorCode Load(const std::string&,
                           const core::ModelConfig&) override {
        loaded_ = true;
        return utils::ErrorCode::kOk;
    }
    utils::ErrorCode Infer(const std::vector<utils::Tensor>&,
                            std::vector<utils::Tensor>&) override {
        return utils::ErrorCode::kOk;
    }
    std::vector<utils::TensorInfo> GetInputInfo()  const override { return {}; }
    std::vector<utils::TensorInfo> GetOutputInfo() const override { return {}; }
    void Unload() override { loaded_ = false; }
    bool IsLoaded() const override { return loaded_; }

 private:
    bool loaded_ = false;
};

TEST(BackendFactoryTest, RegisterAndCreateReturnInstance) {
    auto& factory = backend::BackendFactory::Instance();
    factory.Register("stub_test",
                     []() { return std::make_unique<StubBackend>(); });

    auto backend = factory.Create("stub_test");
    ASSERT_NE(backend, nullptr);
}

TEST(BackendFactoryTest, CreateUnregisteredBackendReturnsNullptr) {
    auto& factory = backend::BackendFactory::Instance();
    auto backend = factory.Create("nonexistent_backend_xyz");
    EXPECT_EQ(backend, nullptr);
}

TEST(BackendFactoryTest, ListBackendsIncludesRegistered) {
    auto& factory = backend::BackendFactory::Instance();
    factory.Register("list_test_backend",
                     []() { return std::make_unique<StubBackend>(); });

    auto names = factory.ListBackends();
    bool found = false;
    for (const auto& n : names) {
        if (n == "list_test_backend") { found = true; break; }
    }
    EXPECT_TRUE(found);
}

}  // namespace
}  // namespace core
}  // namespace atlas
