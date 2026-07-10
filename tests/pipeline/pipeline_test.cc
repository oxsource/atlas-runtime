#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "src/pipeline/nodes/bgr_to_rgb_node.h"
#include "src/pipeline/nodes/dtype_convert_node.h"
#include "src/pipeline/nodes/hwc_to_chw_node.h"
#include "src/pipeline/nodes/normalize_node.h"
#include "src/pipeline/nodes/resize_node.h"
#include "src/pipeline/pipeline.h"
#include "src/utils/types.h"

namespace atlas {
namespace pipeline {
namespace {

// ---------------------------------------------------------------------------
// Tensor factory helpers
// ---------------------------------------------------------------------------

utils::Tensor MakeUint8HWC(int h, int w, int c, uint8_t fill) {
    utils::Tensor t;
    t.info.dtype  = utils::DataType::kUInt8;
    t.info.shape  = {h, w, c};
    t.info.layout = "HWC";
    t.byte_size   = static_cast<size_t>(h * w * c);
    t.data        = malloc(t.byte_size);
    t.owns_data   = true;
    std::memset(t.data, fill, t.byte_size);
    return t;
}

utils::Tensor MakeFloat32CHW(int c, int h, int w, float fill) {
    utils::Tensor t;
    t.info.dtype  = utils::DataType::kFloat32;
    t.info.shape  = {c, h, w};
    t.info.layout = "CHW";
    t.byte_size   = static_cast<size_t>(c * h * w) * sizeof(float);
    t.data        = malloc(t.byte_size);
    t.owns_data   = true;
    float* p = static_cast<float*>(t.data);
    const size_t n = t.byte_size / sizeof(float);
    for (size_t i = 0; i < n; ++i) p[i] = fill;
    return t;
}

// ---------------------------------------------------------------------------
// DtypeConvertNode tests
// ---------------------------------------------------------------------------

TEST(DtypeConvertNodeTest, Uint8ToFloat32CastsValues) {
    auto input = MakeUint8HWC(2, 2, 1, 128);

    DtypeConvertNode node(utils::DataType::kFloat32);
    utils::Tensor output;
    ASSERT_EQ(node.Process({}, input, &output), utils::ErrorCode::kOk);

    EXPECT_EQ(output.info.dtype, utils::DataType::kFloat32);
    const float* data = static_cast<const float*>(output.data);
    EXPECT_FLOAT_EQ(data[0], 128.0f);
}

TEST(DtypeConvertNodeTest, SameDtypeReturnsBorrowedTensor) {
    auto input = MakeUint8HWC(2, 2, 1, 5);
    input.info.dtype = utils::DataType::kFloat32;

    DtypeConvertNode node(utils::DataType::kFloat32);
    utils::Tensor output;
    ASSERT_EQ(node.Process({}, input, &output), utils::ErrorCode::kOk);
    // Borrowed: no allocation.
    EXPECT_EQ(output.data, input.data);
    EXPECT_FALSE(output.owns_data);
}

TEST(DtypeConvertNodeTest, NameIsCorrect) {
    DtypeConvertNode node(utils::DataType::kFloat32);
    EXPECT_EQ(node.Name(), "atlas::dtype_convert");
}

// ---------------------------------------------------------------------------
// ResizeNode tests
// ---------------------------------------------------------------------------

TEST(ResizeNodeTest, ResizesUint8HWC) {
    auto input = MakeUint8HWC(4, 4, 3, 200);

    ResizeNode node(2, 2);
    utils::Tensor output;
    ASSERT_EQ(node.Process({}, input, &output), utils::ErrorCode::kOk);

    ASSERT_EQ(output.info.shape.size(), 3u);
    EXPECT_EQ(output.info.shape[0], 2);
    EXPECT_EQ(output.info.shape[1], 2);
    EXPECT_EQ(output.info.shape[2], 3);
}

TEST(ResizeNodeTest, ResizesFloat32HWC) {
    utils::Tensor input;
    input.info.dtype  = utils::DataType::kFloat32;
    input.info.shape  = {4, 4, 1};
    input.byte_size   = 4 * 4 * sizeof(float);
    input.data        = malloc(input.byte_size);
    input.owns_data   = true;
    float* p = static_cast<float*>(input.data);
    for (int i = 0; i < 16; ++i) p[i] = static_cast<float>(i);

    ResizeNode node(2, 2);
    utils::Tensor output;
    ASSERT_EQ(node.Process({}, input, &output), utils::ErrorCode::kOk);
    EXPECT_EQ(output.info.shape[0], 2);
    EXPECT_EQ(output.info.shape[1], 2);
}

// ---------------------------------------------------------------------------
// BGRToRGBNode tests
// ---------------------------------------------------------------------------

TEST(BGRToRGBNodeTest, SwapsChannels) {
    auto input = MakeUint8HWC(1, 1, 3, 0);
    uint8_t* p = static_cast<uint8_t*>(input.data);
    p[0] = 10;  // B
    p[1] = 20;  // G
    p[2] = 30;  // R

    BGRToRGBNode node;
    utils::Tensor output;
    ASSERT_EQ(node.Process({}, input, &output), utils::ErrorCode::kOk);

    const uint8_t* out = static_cast<const uint8_t*>(output.data);
    EXPECT_EQ(out[0], 30u);  // R
    EXPECT_EQ(out[1], 20u);  // G
    EXPECT_EQ(out[2], 10u);  // B
}

TEST(BGRToRGBNodeTest, RejectsNon3ChannelInput) {
    auto input = MakeUint8HWC(2, 2, 1, 0);
    BGRToRGBNode node;
    utils::Tensor output;
    EXPECT_NE(node.Process({}, input, &output), utils::ErrorCode::kOk);
}

// ---------------------------------------------------------------------------
// HWCToCHWNode tests
// ---------------------------------------------------------------------------

TEST(HWCToCHWNodeTest, TransposesLayout) {
    // 1×1×3 tensor: each channel has a distinct value.
    utils::Tensor input;
    input.info.dtype  = utils::DataType::kUInt8;
    input.info.shape  = {1, 1, 3};
    input.byte_size   = 3;
    input.data        = malloc(3);
    input.owns_data   = true;
    uint8_t* p = static_cast<uint8_t*>(input.data);
    p[0] = 10; p[1] = 20; p[2] = 30;

    HWCToCHWNode node;
    utils::Tensor output;
    ASSERT_EQ(node.Process({}, input, &output), utils::ErrorCode::kOk);

    ASSERT_EQ(output.info.shape.size(), 3u);
    EXPECT_EQ(output.info.shape[0], 3);  // C
    EXPECT_EQ(output.info.shape[1], 1);  // H
    EXPECT_EQ(output.info.shape[2], 1);  // W

    const uint8_t* out = static_cast<const uint8_t*>(output.data);
    EXPECT_EQ(out[0], 10u);
    EXPECT_EQ(out[1], 20u);
    EXPECT_EQ(out[2], 30u);
}

// ---------------------------------------------------------------------------
// NormalizeNode tests
// ---------------------------------------------------------------------------

TEST(NormalizeNodeTest, NormalizesValues) {
    // Single-channel 1×1×1 CHW float32 tensor with value 255.
    auto input = MakeFloat32CHW(1, 1, 1, 255.0f);

    const std::vector<float> mean = {0.5f};
    const std::vector<float> std  = {0.5f};
    NormalizeNode node(mean, std);

    utils::Tensor output;
    ASSERT_EQ(node.Process({}, input, &output), utils::ErrorCode::kOk);

    const float* out = static_cast<const float*>(output.data);
    // (255/255 - 0.5) / 0.5 = (1.0 - 0.5) / 0.5 = 1.0
    EXPECT_NEAR(out[0], 1.0f, 1e-5f);
}

TEST(NormalizeNodeTest, RejectsNonFloat32Input) {
    auto input = MakeUint8HWC(1, 1, 1, 128);
    NormalizeNode node({0.5f}, {0.5f});
    utils::Tensor output;
    EXPECT_NE(node.Process({}, input, &output), utils::ErrorCode::kOk);
}

// ---------------------------------------------------------------------------
// Pipeline tests
// ---------------------------------------------------------------------------

TEST(PipelineTest, EmptyPipelineBorrowsInput) {
    auto input = MakeUint8HWC(4, 4, 3, 100);
    Pipeline p;
    utils::Tensor output;
    ASSERT_EQ(p.Run(input, &output), utils::ErrorCode::kOk);
    EXPECT_EQ(output.data, input.data);
}

}  // namespace
}  // namespace pipeline
}  // namespace atlas
