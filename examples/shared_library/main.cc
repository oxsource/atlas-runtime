// Atlas External Consumer Integration Test
// Demonstrates how non-Bazel projects link against the Atlas SDK.
//
// Build:  export ATLAS_SDK=<dir> && make
// Run:    ATLAS_SDK=<dir> make test

#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "atlas/atlas_runtime.h"
#include "atlas/model_handle.h"
#include "atlas/types.h"

static int Failures = 0;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::cerr << "[FAIL] " << (msg) << " (" << __LINE__ << ")\n";     \
            ++Failures;                                                       \
        } else {                                                              \
            std::cout << "[OK]   " << (msg) << "\n";                          \
        }                                                                     \
    } while (0)

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <manifest.json> <identity_1x3x4x4.onnx>\n";
        return 1;
    }

    const std::string manifest = argv[1];

    std::cout << "Atlas External Consumer Integration Test\n\n";

    // Test 1: construct & init
    atlas::api::AtlasRuntime rt;
    {
        auto rc = rt.Init(manifest);
        CHECK(rc == atlas::utils::ErrorCode::kOk, "Init returns kOk");
        CHECK(rt.IsInitialized(), "IsInitialized == true");
    }

    // Test 3: get model handle
    auto handle = rt.GetModel("identity_model");
    CHECK(handle.IsValid(), "GetModel('identity_model') is valid");

    // Test 4: input info
    auto infos = handle.GetInputInfo();
    CHECK(!infos.empty(), "GetInputInfo returns non-empty");

    // Test 5: end-to-end inference (identity model)
    {
        atlas::utils::Tensor input;
        input.info.dtype  = atlas::utils::DataType::kUInt8;
        input.info.shape  = {4, 4, 3};
        input.info.layout = "HWC";
        input.byte_size   = 4 * 4 * 3;
        input.data        = std::malloc(input.byte_size);
        input.owns_data   = true;
        std::memset(input.data, 100, input.byte_size);

        std::vector<atlas::utils::Tensor> outputs;
        auto rc = handle.Run(input, &outputs);
        CHECK(rc == atlas::utils::ErrorCode::kOk, "Run returns kOk");
        CHECK(outputs.size() == 1u, "Run produces 1 output tensor");
    }

    // Test 6: invalid model
    CHECK(!rt.GetModel("nonexistent").IsValid(), "GetModel('nonexistent') is invalid");

    // Test 7: release and re-init
    rt.Release();
    CHECK(!rt.IsInitialized(), "After Release, IsInitialized == false");
    {
        auto rc = rt.Init(manifest);
        CHECK(rc == atlas::utils::ErrorCode::kOk, "Re-init succeeds");
        CHECK(rt.IsInitialized(), "Post-reinit IsInitialized == true");
    }
    rt.Release();

    std::cout << "\n" << Failures << " failure(s)\n";
    return Failures > 0 ? 1 : 0;
}