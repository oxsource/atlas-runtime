// Verifies that the public header set (atlas/atlas.h umbrella) compiles
// without referencing any internal (src/...) headers, and that all
// public types are accessible.

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "atlas/atlas.h"

// Verify DataType enum values match the documented contract.
TEST(AtlasExportTest, DataTypeValues) {
    EXPECT_EQ(static_cast<int>(atlas::utils::DataType::kUnknown), 0);
    EXPECT_EQ(static_cast<int>(atlas::utils::DataType::kFloat32), 1);
    EXPECT_EQ(static_cast<int>(atlas::utils::DataType::kFloat16), 2);
    EXPECT_EQ(static_cast<int>(atlas::utils::DataType::kInt8), 3);
    EXPECT_EQ(static_cast<int>(atlas::utils::DataType::kUInt8), 4);
    EXPECT_EQ(static_cast<int>(atlas::utils::DataType::kInt32), 5);
}

// Verify ErrorCode enum values match the documented contract.
TEST(AtlasExportTest, ErrorCodeValues) {
    EXPECT_EQ(static_cast<int>(atlas::utils::ErrorCode::kOk), 0);
    EXPECT_EQ(static_cast<int>(atlas::utils::ErrorCode::kInvalidArgument), 1);
    EXPECT_EQ(static_cast<int>(atlas::utils::ErrorCode::kFileNotFound), 2);
    EXPECT_EQ(static_cast<int>(atlas::utils::ErrorCode::kParseError), 3);
    EXPECT_EQ(static_cast<int>(atlas::utils::ErrorCode::kVersionMismatch), 4);
    EXPECT_EQ(static_cast<int>(atlas::utils::ErrorCode::kBackendNotFound), 5);
    EXPECT_EQ(static_cast<int>(atlas::utils::ErrorCode::kInferFailed), 6);
    EXPECT_EQ(static_cast<int>(atlas::utils::ErrorCode::kNotInitialized), 7);
}

// Verify ErrorCodeToString returns non-null for all known codes.
TEST(AtlasExportTest, ErrorCodeToStringAllCodes) {
    using E = atlas::utils::ErrorCode;
    EXPECT_NE(atlas::utils::ErrorCodeToString(E::kOk), nullptr);
    EXPECT_NE(atlas::utils::ErrorCodeToString(E::kInvalidArgument), nullptr);
    EXPECT_NE(atlas::utils::ErrorCodeToString(E::kFileNotFound), nullptr);
    EXPECT_NE(atlas::utils::ErrorCodeToString(E::kParseError), nullptr);
    EXPECT_NE(atlas::utils::ErrorCodeToString(E::kVersionMismatch), nullptr);
    EXPECT_NE(atlas::utils::ErrorCodeToString(E::kBackendNotFound), nullptr);
    EXPECT_NE(atlas::utils::ErrorCodeToString(E::kInferFailed), nullptr);
    EXPECT_NE(atlas::utils::ErrorCodeToString(E::kNotInitialized), nullptr);
}

// Verify ElementByteSize returns correct values.
TEST(AtlasExportTest, ElementByteSize) {
    using D = atlas::utils::DataType;
    EXPECT_EQ(atlas::utils::ElementByteSize(D::kFloat32), 4u);
    EXPECT_EQ(atlas::utils::ElementByteSize(D::kFloat16), 2u);
    EXPECT_EQ(atlas::utils::ElementByteSize(D::kInt8), 1u);
    EXPECT_EQ(atlas::utils::ElementByteSize(D::kUInt8), 1u);
    EXPECT_EQ(atlas::utils::ElementByteSize(D::kInt32), 4u);
    EXPECT_EQ(atlas::utils::ElementByteSize(D::kUnknown), 0u);
}

// Verify ElementCount computes correctly.
TEST(AtlasExportTest, ElementCount) {
    EXPECT_EQ(atlas::utils::ElementCount({1, 3, 32, 32}), 3072u);
    EXPECT_EQ(atlas::utils::ElementCount({1}), 1u);
    EXPECT_EQ(atlas::utils::ElementCount({}), 1u);
    // Dynamic dimension (-1) treated as 1.
    EXPECT_EQ(atlas::utils::ElementCount({-1, 3, 224, 224}), 150528u);
}

// Verify Tensor move semantics.
TEST(AtlasExportTest, TensorMoveSemantics) {
    atlas::utils::Tensor a;
    a.info.dtype = atlas::utils::DataType::kFloat32;
    a.info.shape = {1, 3, 2, 2};
    a.byte_size = 48;
    a.data = std::malloc(48);
    a.owns_data = true;

    atlas::utils::Tensor b = std::move(a);
    EXPECT_EQ(a.data, nullptr);
    EXPECT_EQ(a.owns_data, false);
    EXPECT_NE(b.data, nullptr);
    EXPECT_EQ(b.byte_size, 48u);
    EXPECT_EQ(b.info.dtype, atlas::utils::DataType::kFloat32);
}

// Verify TensorInfo default values.
TEST(AtlasExportTest, TensorInfoDefaults) {
    atlas::utils::TensorInfo info;
    EXPECT_EQ(info.dtype, atlas::utils::DataType::kFloat32);
    EXPECT_EQ(info.layout, "NCHW");
    EXPECT_FALSE(info.has_normalize);
}

// Verify version constants.
TEST(AtlasExportTest, VersionConstants) {
    EXPECT_EQ(atlas::utils::kVersionMajor, 1);
    EXPECT_EQ(atlas::utils::kVersionMinor, 0);
    EXPECT_EQ(atlas::utils::kVersionPatch, 0);
}

// Verify ModelHandle default-constructed is invalid.
TEST(AtlasExportTest, ModelHandleDefaultInvalid) {
    atlas::api::ModelHandle handle;
    EXPECT_FALSE(handle.IsValid());
}
