// Atlas SNPE CPU Shared Library Comparison Test
// Demonstrates controlled-variable comparison between CPU (ONNX) and
// SNPE (DLC) backends via the Atlas shared library.
//
// Build:  export ATLAS_SDK=<dir> && make
// Run:    SAMPLE_MODEL_DIR=<dir> ATLAS_SDK=<dir> make test

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
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

// ---------------------------------------------------------------------------
// Creates a synthetic H×W BGR raw image (HWC uint8).
// Fills a gradient: B increases with row, G with column, R = 128.
// ---------------------------------------------------------------------------
atlas::utils::Tensor CreateSampleBGRImage(int h, int w) {
    atlas::utils::Tensor img;
    img.info.dtype  = atlas::utils::DataType::kUInt8;
    img.info.shape  = {h, w, 3};
    img.info.layout = "HWC";
    img.byte_size   = static_cast<size_t>(h * w * 3);
    img.data        = std::malloc(img.byte_size);
    img.owns_data   = true;

    uint8_t* p = static_cast<uint8_t*>(img.data);
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            p[(r * w + c) * 3 + 0] = static_cast<uint8_t>(r * 255 / h);  // B
            p[(r * w + c) * 3 + 1] = static_cast<uint8_t>(c * 255 / w);  // G
            p[(r * w + c) * 3 + 2] = 128;                                  // R
        }
    }
    return img;
}

// ---------------------------------------------------------------------------
// Compares two float32 tensors element-wise within a relative tolerance.
// Returns the maximum absolute difference.
// ---------------------------------------------------------------------------
float CompareTensors(const atlas::utils::Tensor& a,
                     const atlas::utils::Tensor& b) {
    if (a.byte_size != b.byte_size || a.data == nullptr || b.data == nullptr) {
        return -1.0f;  // incomparable
    }
    size_t count = a.byte_size / sizeof(float);
    const float* pa = static_cast<const float*>(a.data);
    const float* pb = static_cast<const float*>(b.data);
    float max_diff = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        float diff = std::fabs(pa[i] - pb[i]);
        if (diff > max_diff) max_diff = diff;
    }
    return max_diff;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <manifest.json>\n"
                  << "  Set SAMPLE_MODEL_DIR to the directory containing\n"
                  << "  relu_snpe.dlc before running.\n";
        return 1;
    }

    const std::string manifest = argv[1];

    std::cout << "Atlas SNPE/CPU Comparison Test (Shared Library)\n\n";

    // -----------------------------------------------------------------------
    // Test 1: Runtime Init
    // -----------------------------------------------------------------------
    atlas::api::AtlasRuntime rt;
    {
        auto rc = rt.Init(manifest);
        CHECK(rc == atlas::utils::ErrorCode::kOk, "Init returns kOk");
        CHECK(rt.IsInitialized(), "IsInitialized == true");
    }

    // -----------------------------------------------------------------------
    // Test 2: Get CPU model handle
    // -----------------------------------------------------------------------
    auto cpu_model = rt.GetModel("relu_cpu");
    CHECK(cpu_model.IsValid(), "GetModel('relu_cpu') is valid");
    CHECK(cpu_model.GetBackend() == "cpu",
          "CPU model backend == 'cpu'");

    // -----------------------------------------------------------------------
    // Test 3: Get SNPE model handle
    // -----------------------------------------------------------------------
    auto snpe_model = rt.GetModel("relu_snpe");
    CHECK(snpe_model.IsValid(), "GetModel('relu_snpe') is valid");
    CHECK(snpe_model.GetBackend() == "snpe",
          "SNPE model backend == 'snpe'");

    // -----------------------------------------------------------------------
    // Test 4: CPU inference
    // -----------------------------------------------------------------------
    {
        auto input = CreateSampleBGRImage(4, 4);
        std::vector<atlas::utils::Tensor> outputs;
        auto rc = cpu_model.Run(input, &outputs);
        CHECK(rc == atlas::utils::ErrorCode::kOk, "CPU Run returns kOk");
        CHECK(outputs.size() == 1u, "CPU Run produces 1 output tensor");
    }

    // -----------------------------------------------------------------------
    // Test 5: SNPE inference (may return kBackendNotFound on non-aarch64)
    // -----------------------------------------------------------------------
    bool snpe_available = false;
    {
        auto input = CreateSampleBGRImage(4, 4);
        std::vector<atlas::utils::Tensor> outputs;
        auto rc = snpe_model.Run(input, &outputs);

        if (rc == atlas::utils::ErrorCode::kBackendNotFound) {
            std::cout << "[OK]   SNPE backend not available on this "
                         "platform (kBackendNotFound). "
                         "Skipping comparison tests.\n";
            snpe_available = false;
        } else {
            CHECK(rc == atlas::utils::ErrorCode::kOk,
                  "SNPE Run returns kOk");
            CHECK(outputs.size() == 1u,
                  "SNPE Run produces 1 output tensor");
            snpe_available = true;
        }
    }

    // -----------------------------------------------------------------------
    // Test 6: Controlled-variable comparison (CPU vs SNPE)
    // -----------------------------------------------------------------------
    if (snpe_available) {
        // Re-run both with identical input for comparison
        auto shared_input = CreateSampleBGRImage(4, 4);

        // CPU inference
        std::vector<atlas::utils::Tensor> cpu_outputs;
        auto rc_cpu = cpu_model.Run(shared_input, &cpu_outputs);
        CHECK(rc_cpu == atlas::utils::ErrorCode::kOk,
              "[Compare] CPU Run returns kOk");

        // SNPE inference
        std::vector<atlas::utils::Tensor> snpe_outputs;
        auto rc_snpe = snpe_model.Run(shared_input, &snpe_outputs);
        CHECK(rc_snpe == atlas::utils::ErrorCode::kOk,
              "[Compare] SNPE Run returns kOk");

        if (rc_cpu == atlas::utils::ErrorCode::kOk &&
            rc_snpe == atlas::utils::ErrorCode::kOk) {
            const auto& cpu_out = cpu_outputs[0];
            const auto& snpe_out = snpe_outputs[0];

            // Compare shapes
            bool shape_match = (cpu_out.info.shape == snpe_out.info.shape);
            CHECK(shape_match, "[Compare] Output shapes match");

            // Compare data element-wise
            float max_diff = CompareTensors(cpu_out, snpe_out);
            constexpr float kTolerance = 1e-5f;
            bool values_match = (max_diff >= 0.0f && max_diff <= kTolerance);
            CHECK(values_match,
                  "[Compare] Output values match within tolerance");
            if (values_match) {
                std::cout << "        max_diff = " << max_diff
                          << " (tolerance = " << kTolerance << ")\n";
            } else if (max_diff >= 0.0f) {
                std::cout << "        max_diff = " << max_diff
                          << " EXCEEDS tolerance = " << kTolerance << "\n";
            }
        }
    }

    // -----------------------------------------------------------------------
    // Test 7: Invalid model
    // -----------------------------------------------------------------------
    CHECK(!rt.GetModel("nonexistent").IsValid(),
          "GetModel('nonexistent') is invalid");

    // -----------------------------------------------------------------------
    // Test 8: Release and re-init
    // -----------------------------------------------------------------------
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