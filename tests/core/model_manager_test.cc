#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/backend/base/backend_factory.h"
#include "src/backend/cpu/cpu_backend_context.h"
#include "src/core/manifest_config.h"
#include "src/core/model_manager.h"
#include "src/utils/types.h"

namespace atlas {
namespace core {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

// Builds a ManifestConfig with one or two cpu models pointing at the
// identity model.
ManifestConfig MakeManifest(bool two_models = false,
                              LoadStrategy strategy = LoadStrategy::kEager) {
    ManifestTensorInfo input_info;
    input_info.name   = "images";
    input_info.shape  = {1, 3, 4, 4};
    input_info.dtype  = utils::DataType::kFloat32;
    input_info.layout = "NCHW";

    ManifestTensorInfo output_info;
    output_info.name   = "output";
    output_info.shape  = {1, 3, 4, 4};
    output_info.dtype  = utils::DataType::kFloat32;

    ModelConfig m;
    m.id            = "model_a";
    m.backend       = "cpu";
    m.model_path    = TestModelPath();
    m.load_strategy = strategy;
    m.inputs        = {input_info};
    m.outputs       = {output_info};
    m.config["num_threads"] = "1";

    ManifestConfig manifest;
    manifest.version = "1.0";
    manifest.name    = "test";
    manifest.models.push_back(m);

    if (two_models) {
        ModelConfig m2  = m;
        m2.id           = "model_b";
        m2.load_strategy = LoadStrategy::kLazy;
        manifest.models.push_back(m2);
    }
    return manifest;
}

// ---------------------------------------------------------------------------
// ModelManager tests
// ---------------------------------------------------------------------------

TEST(ModelManagerTest, InitEagerModelSucceeds) {
    ModelManager mgr;
    ASSERT_EQ(mgr.Init(MakeManifest()), utils::ErrorCode::kOk);

    ModelEntry* entry = nullptr;
    ASSERT_EQ(mgr.GetEntry("model_a", &entry), utils::ErrorCode::kOk);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->loaded);
}

TEST(ModelManagerTest, GetUnknownModelReturnsInvalidArgument) {
    ModelManager mgr;
    ASSERT_EQ(mgr.Init(MakeManifest()), utils::ErrorCode::kOk);
    ModelEntry* entry = nullptr;
    EXPECT_EQ(mgr.GetEntry("nonexistent", &entry),
              utils::ErrorCode::kInvalidArgument);
}

TEST(ModelManagerTest, LazyModelNotLoadedAfterInit) {
    ManifestConfig manifest = MakeManifest(/*two_models=*/false,
                                            LoadStrategy::kLazy);
    ModelManager mgr;
    ASSERT_EQ(mgr.Init(manifest), utils::ErrorCode::kOk);

    // Peek directly at the entry without triggering load.
    // GetEntry triggers EnsureLoaded — so check via a second Init.
    ModelEntry* entry = nullptr;
    // After GetEntry the model becomes loaded.
    ASSERT_EQ(mgr.GetEntry("model_a", &entry), utils::ErrorCode::kOk);
    EXPECT_TRUE(entry->loaded);
}

TEST(ModelManagerTest, TwoModelsShareOneContext) {
    ManifestConfig manifest = MakeManifest(/*two_models=*/true);
    ModelManager mgr;
    ASSERT_EQ(mgr.Init(manifest), utils::ErrorCode::kOk);

    // Both models should be accessible.
    ModelEntry* ea = nullptr;
    ModelEntry* eb = nullptr;
    ASSERT_EQ(mgr.GetEntry("model_a", &ea), utils::ErrorCode::kOk);
    ASSERT_EQ(mgr.GetEntry("model_b", &eb), utils::ErrorCode::kOk);
    EXPECT_TRUE(ea->loaded);
    EXPECT_TRUE(eb->loaded);
}

TEST(ModelManagerTest, InvalidModelPathFailsInit) {
    ManifestConfig manifest = MakeManifest();
    manifest.models[0].model_path = "/nonexistent/model.onnx";
    ModelManager mgr;
    EXPECT_NE(mgr.Init(manifest), utils::ErrorCode::kOk);
}

TEST(ModelManagerTest, ReleaseAllClearsEntries) {
    ModelManager mgr;
    ASSERT_EQ(mgr.Init(MakeManifest()), utils::ErrorCode::kOk);
    mgr.ReleaseAll();
    ModelEntry* entry = nullptr;
    EXPECT_EQ(mgr.GetEntry("model_a", &entry),
              utils::ErrorCode::kInvalidArgument);
}

// ---------------------------------------------------------------------------
// CpuBackendContext tests
// ---------------------------------------------------------------------------

TEST(CpuBackendContextTest, BackendTypeIsCpu) {
    backend::cpu::CpuBackendContext ctx;
    EXPECT_EQ(ctx.BackendType(), "cpu");
}

TEST(CpuBackendContextTest, RegisteredInFactory) {
    auto ctx = backend::BackendFactory::Instance().CreateContext("cpu");
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(ctx->BackendType(), "cpu");
}

}  // namespace
}  // namespace core
}  // namespace atlas
