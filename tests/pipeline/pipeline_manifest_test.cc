// Tests for manifest-configurable pipeline: BuildFromManifest,
// PipelineNodeFactory registration, and pipeline field parsing.

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/core/manifest_config.h"
#include "src/core/manifest_parser.h"
#include "src/pipeline/pipeline.h"
#include "src/pipeline/pipeline_node_factory.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {
namespace {

// Writes a manifest JSON to a temp file and returns the path.
std::string WriteTempManifest(const std::string& json) {
    const std::string path = "/tmp/atlas_pipeline_manifest_test.json";
    std::ofstream f(path);
    f << json;
    return path;
}

}  // namespace

// Verify PipelineNodeFactory has all built-in nodes registered.
TEST(PipelineNodeFactoryTest, AllBuiltinNodesRegistered) {
    auto& factory = PipelineNodeFactory::Instance();
    auto names = factory.ListNodeNames();

    // Check that all expected node names are present.
    std::vector<std::string> expected = {
        "dtype_convert", "resize", "bgr_to_rgb", "rgb_to_bgr",
        "hwc_to_chw", "chw_to_hwc", "normalize", "softmax", "topk"
    };

    for (const auto& name : expected) {
        bool found = false;
        for (const auto& registered : names) {
            if (registered == name) { found = true; break; }
        }
        EXPECT_TRUE(found) << "Node '" << name << "' not registered";
    }
}

// Verify Create returns nullptr for unknown node name.
TEST(PipelineNodeFactoryTest, UnknownNodeReturnsNull) {
    auto& factory = PipelineNodeFactory::Instance();
    auto node = factory.Create("nonexistent_node", {});
    EXPECT_EQ(node, nullptr);
}

// Verify BuildFromManifest creates a pipeline with the correct number of nodes.
TEST(PipelineManifestTest, BuildFromManifestCorrectNodeCount) {
    std::vector<core::ManifestPipelineNode> nodes;
    nodes.push_back({"dtype_convert", {{"target", "float32"}}});
    nodes.push_back({"bgr_to_rgb", {}});
    nodes.push_back({"hwc_to_chw", {}});

    auto pipeline = Pipeline::BuildFromManifest(nodes);
    EXPECT_EQ(pipeline.NodeCount(), 3u);
    EXPECT_FALSE(pipeline.IsEmpty());
}

// Verify BuildFromManifest returns empty pipeline for unknown node.
TEST(PipelineManifestTest, UnknownNodeReturnsEmptyPipeline) {
    std::vector<core::ManifestPipelineNode> nodes;
    nodes.push_back({"dtype_convert", {{"target", "float32"}}});
    nodes.push_back({"nonexistent", {}});

    auto pipeline = Pipeline::BuildFromManifest(nodes);
    EXPECT_TRUE(pipeline.IsEmpty());
}

// Verify BuildFromManifest with empty vector returns empty pipeline.
TEST(PipelineManifestTest, EmptyNodeListReturnsEmptyPipeline) {
    std::vector<core::ManifestPipelineNode> nodes;
    auto pipeline = Pipeline::BuildFromManifest(nodes);
    EXPECT_TRUE(pipeline.IsEmpty());
}

// Verify BuildOutputFromManifest works the same as BuildFromManifest.
TEST(PipelineManifestTest, BuildOutputFromManifestWorks) {
    std::vector<core::ManifestPipelineNode> nodes;
    nodes.push_back({"softmax", {}});

    auto pipeline = Pipeline::BuildOutputFromManifest(nodes);
    EXPECT_EQ(pipeline.NodeCount(), 1u);
}

// Verify manifest parser parses pipeline field correctly.
TEST(PipelineManifestTest, ManifestParserParsesPipelineField) {
    std::string json = R"({
  "version": "1.0",
  "name": "test",
  "models": [{
    "id": "m1",
    "backend": "cpu",
    "model_path": "/tmp/test.onnx",
    "inputs": [{
      "name": "images",
      "shape": [1, 3, 224, 224],
      "dtype": "float32",
      "layout": "NCHW",
      "pipeline": [
        {"name": "dtype_convert", "params": {"target": "float32"}},
        {"name": "resize", "params": {"height": 224, "width": 224}},
        {"name": "bgr_to_rgb"}
      ]
    }],
    "outputs": [{"name": "out", "shape": [1, 3], "dtype": "float32"}]
  }]
})";
    auto path = WriteTempManifest(json);

    core::ManifestParser parser;
    core::ManifestConfig config;
    auto ret = parser.Parse(path, &config);
    ASSERT_EQ(ret, utils::ErrorCode::kOk);
    ASSERT_EQ(config.models.size(), 1u);
    ASSERT_EQ(config.models[0].inputs.size(), 1u);

    const auto& input = config.models[0].inputs[0];
    ASSERT_EQ(input.pipeline.size(), 3u);
    EXPECT_EQ(input.pipeline[0].name, "dtype_convert");
    EXPECT_EQ(input.pipeline[0].params.at("target"), "float32");
    EXPECT_EQ(input.pipeline[1].name, "resize");
    EXPECT_EQ(input.pipeline[1].params.at("height"), "224");
    EXPECT_EQ(input.pipeline[1].params.at("width"), "224");
    EXPECT_EQ(input.pipeline[2].name, "bgr_to_rgb");
    EXPECT_TRUE(input.pipeline[2].params.empty());
}

// Verify manifest parser rejects duplicate pipeline node names.
TEST(PipelineManifestTest, DuplicateNodeNameReturnsParseError) {
    std::string json = R"({
  "version": "1.0",
  "name": "test",
  "models": [{
    "id": "m1",
    "backend": "cpu",
    "model_path": "/tmp/test.onnx",
    "inputs": [{
      "name": "images",
      "shape": [1, 3, 224, 224],
      "dtype": "float32",
      "layout": "NCHW",
      "pipeline": [
        {"name": "bgr_to_rgb"},
        {"name": "bgr_to_rgb"}
      ]
    }],
    "outputs": [{"name": "out", "shape": [1, 3], "dtype": "float32"}]
  }]
})";
    auto path = WriteTempManifest(json);

    core::ManifestParser parser;
    core::ManifestConfig config;
    auto ret = parser.Parse(path, &config);
    EXPECT_EQ(ret, utils::ErrorCode::kParseError);
}

// Verify manifest parser handles missing pipeline field (backward compat).
TEST(PipelineManifestTest, NoPipelineFieldIsBackwardCompatible) {
    std::string json = R"({
  "version": "1.0",
  "name": "test",
  "models": [{
    "id": "m1",
    "backend": "cpu",
    "model_path": "/tmp/test.onnx",
    "inputs": [{
      "name": "images",
      "shape": [1, 3, 224, 224],
      "dtype": "float32",
      "layout": "NCHW",
      "normalize": {"mean": [0.485, 0.456, 0.406], "std": [0.229, 0.224, 0.225]}
    }],
    "outputs": [{"name": "out", "shape": [1, 3], "dtype": "float32"}]
  }]
})";
    auto path = WriteTempManifest(json);

    core::ManifestParser parser;
    core::ManifestConfig config;
    auto ret = parser.Parse(path, &config);
    ASSERT_EQ(ret, utils::ErrorCode::kOk);
    ASSERT_EQ(config.models[0].inputs[0].pipeline.size(), 0u);
    EXPECT_TRUE(config.models[0].inputs[0].has_normalize);
}

}  // namespace pipeline
}  // namespace atlas
