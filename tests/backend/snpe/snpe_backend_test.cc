#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/backend/base/backend_factory.h"
#include "src/backend/snpe/snpe_backend.h"
#include "src/backend/snpe/snpe_backend_context.h"
#include "src/core/manifest_config.h"
#include "src/utils/types.h"

namespace atlas {
namespace backend {
namespace {

constexpr std::string_view kBackendName = "snpe";

core::ModelConfig MakeModelConfig() {
    core::ModelConfig cfg;
    cfg.id         = "test_snpe_model";
    cfg.backend    = std::string(kBackendName);
    cfg.model_path = "/nonexistent/model.dlc";
    return cfg;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class SnpeBackendTest : public ::testing::Test {
 protected:
    SnpeBackend backend_;
};

// ---------------------------------------------------------------------------
// SnpeBackend basic tests
// ---------------------------------------------------------------------------

TEST_F(SnpeBackendTest, InitialStateIsNotLoaded) {
    EXPECT_FALSE(backend_.IsLoaded());
}

TEST_F(SnpeBackendTest, LoadWithNonexistentModelReturnsError) {
    auto cfg = MakeModelConfig();
#ifdef ATLAS_SNPE_ENABLED
    EXPECT_EQ(backend_.Load(cfg.model_path, cfg),
              utils::ErrorCode::kFileNotFound);
#else
    EXPECT_EQ(backend_.Load(cfg.model_path, cfg),
              utils::ErrorCode::kBackendNotFound);
#endif
    EXPECT_FALSE(backend_.IsLoaded());
}

TEST_F(SnpeBackendTest, LoadWithContextAndNonexistentModelReturnsError) {
    SnpeBackendContext ctx;
    auto cfg = MakeModelConfig();
#ifdef ATLAS_SNPE_ENABLED
    EXPECT_EQ(backend_.Load(cfg.model_path, cfg, &ctx),
              utils::ErrorCode::kFileNotFound);
#else
    EXPECT_EQ(backend_.Load(cfg.model_path, cfg, &ctx),
              utils::ErrorCode::kBackendNotFound);
#endif
    EXPECT_FALSE(backend_.IsLoaded());
}

TEST_F(SnpeBackendTest, InferBeforeLoadReturnsNotInitialized) {
    std::vector<utils::Tensor> inputs;
    std::vector<utils::Tensor> outputs;
    EXPECT_EQ(backend_.Infer(inputs, outputs),
              utils::ErrorCode::kNotInitialized);
}

TEST_F(SnpeBackendTest, GetInputInfoReturnsEmpty) {
    auto inputs = backend_.GetInputInfo();
    EXPECT_TRUE(inputs.empty());
}

TEST_F(SnpeBackendTest, GetOutputInfoReturnsEmpty) {
    auto outputs = backend_.GetOutputInfo();
    EXPECT_TRUE(outputs.empty());
}

TEST_F(SnpeBackendTest, UnloadIsSafe) {
    // Unload() is a no-op in stub; must not crash or throw.
    backend_.Unload();
    EXPECT_FALSE(backend_.IsLoaded());
}

TEST_F(SnpeBackendTest, BackendFactoryCreatesSnpeBackend) {
    auto& factory = BackendFactory::Instance();
    auto b = factory.Create(std::string(kBackendName));
    ASSERT_NE(b, nullptr);
}

TEST_F(SnpeBackendTest, BackendFactoryCreatesSnpeBackendContext) {
    auto& factory = BackendFactory::Instance();
    auto ctx = factory.CreateContext(std::string(kBackendName));
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(ctx->BackendType(), "snpe");
}

TEST_F(SnpeBackendTest, ContextInitSucceedsOrReturnsBackendNotFound) {
    SnpeBackendContext ctx;
    std::unordered_map<std::string, std::string> config;
#ifdef ATLAS_SNPE_ENABLED
    EXPECT_EQ(ctx.Init(config), utils::ErrorCode::kOk);
#else
    EXPECT_EQ(ctx.Init(config), utils::ErrorCode::kBackendNotFound);
#endif
}

TEST_F(SnpeBackendTest, ContextBackendTypeIsSnpe) {
    SnpeBackendContext ctx;
    EXPECT_EQ(ctx.BackendType(), "snpe");
}

}  // namespace
}  // namespace backend
}  // namespace atlas