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

// ===========================================================================
// SnpeBackendContext + SnpeMemoryPool integration tests
//
// These tests verify that the shared memory pool inside SnpeBackendContext
// works correctly.  SnpeMemoryPool is pure C++ with no SNPE SDK dependency,
// so these compile and pass on any platform including macOS / CI.
// ===========================================================================

TEST_F(SnpeBackendTest, ContextGetMemoryPool) {
    SnpeBackendContext ctx;
    auto& pool = ctx.GetMemoryPool();

    // Pool reference is valid — Acquire/Release round-trip works.
    auto buf = pool.Acquire(64);
    EXPECT_NE(buf.data, nullptr);
    EXPECT_GE(buf.size, 64u);

    pool.Release(std::move(buf));
    SUCCEED();
}

TEST_F(SnpeBackendTest, ContextPoolSharedAcquire) {
    SnpeBackendContext ctx;
    auto& pool = ctx.GetMemoryPool();

    void* p1 = pool.AcquireShared("integ_key", 256);
    ASSERT_NE(p1, nullptr);

    void* p2 = pool.AcquireShared("integ_key", 256);
    EXPECT_EQ(p2, p1);  // Same pointer (refcount=2).

    pool.ReleaseShared("integ_key");  // refcount=1
    pool.ReleaseShared("integ_key");  // refcount=0 → recycled
}

TEST_F(SnpeBackendTest, ContextPoolClearAndReuse) {
    SnpeBackendContext ctx;
    auto& pool = ctx.GetMemoryPool();

    pool.AcquireShared("c", 512);
    EXPECT_GT(pool.TotalAllocatedBytes(), 0u);

    pool.Clear();
    EXPECT_EQ(pool.TotalAllocatedBytes(), 0u);

    // After Clear, pool is usable again.
    auto buf = pool.Acquire(256);
    EXPECT_NE(buf.data, nullptr);
}

TEST_F(SnpeBackendTest, ContextPoolDoubleFreeProtection) {
    SnpeBackendContext ctx;
    auto& pool = ctx.GetMemoryPool();

    // ReleaseShared on non-existent key is a no-op (must not crash).
    pool.ReleaseShared("nonexistent");

    pool.AcquireShared("d", 128);
    pool.ReleaseShared("d");
    // Repeated release is safe (already moved to free list).
    pool.ReleaseShared("d");
    SUCCEED();
}

TEST_F(SnpeBackendTest, ContextPoolWithoutSharedNoOp) {
    SnpeBackendContext ctx;
    auto& pool = ctx.GetMemoryPool();

    // Load/Unload cycle without any shared_input config.
    // In stub mode, Load returns kBackendNotFound — pool stays untouched.
    auto cfg = MakeModelConfig();
    (void)backend_.Load(cfg.model_path, cfg, &ctx);

    // Regardless of Load result, the pool should be clean.
    EXPECT_EQ(pool.TotalAllocatedBytes(), 0u);

    backend_.Unload();
    EXPECT_EQ(pool.TotalAllocatedBytes(), 0u);
}

}  // namespace
}  // namespace backend
}  // namespace atlas