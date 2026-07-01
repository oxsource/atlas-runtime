// Copyright 2025 The Atlas Authors
// Atlas SNPE CPU Runtime Demonstration
// Shows how to use the SNPE backend with CPU as the inference runtime.
//
// Platform constraint: SNPE backend is only functional on linux_aarch64 /
// android_arm64. On other platforms the stub returns kBackendNotFound.
//
// Build:  bazel build //examples/snpe_cpu:snpe_cpu
// Run:    SAMPLE_MODEL_DIR=<path> ./bazel-bin/examples/snpe_cpu/snpe_cpu \
//             examples/snpe_cpu/manifest.json

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "atlas/atlas.h"

namespace {

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
    img.data        = malloc(img.byte_size);
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
// Returns the index of the element with the largest absolute value.
// ---------------------------------------------------------------------------
int ArgMaxAbs(const float* data, size_t count) {
    int   best   = 0;
    float best_v = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const float v = data[i] >= 0.0f ? data[i] : -data[i];
        if (v > best_v) { best_v = v; best = static_cast<int>(i); }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Prints a formatted section banner.
// ---------------------------------------------------------------------------
void Banner(const char* title) {
    std::cout << "\n-- " << title << " --\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::cout << "Atlas SNPE CPU Runtime Sample  (v"
              << atlas::utils::VersionString() << ")\n";

    if (argc < 2) {
        std::cerr << "Usage: snpe_cpu <manifest_path>\n"
                  << "  Set SAMPLE_MODEL_DIR to the directory containing\n"
                  << "  identity_snpe.dlc before running.\n";
        return 1;
    }
    const std::string manifest_path = argv[1];

    // -- Step 1: Initialize AtlasRuntime ---------------------------------
    // Parses the manifest, creates a shared SnpeBackendContext,
    // and eagerly loads the SNPE model (load_strategy: eager).
    Banner("Step 1: Initialize");
    atlas::api::AtlasRuntime runtime;
    auto ret = runtime.Init(manifest_path);
    if (ret != atlas::utils::ErrorCode::kOk) {
        std::cerr << "[ERROR] Init failed: "
                  << atlas::utils::ErrorCodeToString(ret) << "\n";
        return 1;
    }
    std::cout << "Runtime initialized from: " << manifest_path << "\n";

    // -- Step 2: Obtain model handle -------------------------------------
    Banner("Step 2: Get Model Handle");
    auto model = runtime.GetModel("snpe_identity");
    if (!model.IsValid()) {
        std::cerr << "[ERROR] Failed to obtain model handle.\n";
        return 1;
    }

    // Print model metadata.
    const auto infos = model.GetInputInfo();
    std::cout << "[snpe_identity] input '" << infos[0].name
              << "'  shape: [";
    for (int d : infos[0].shape) std::cout << d << ",";
    std::cout << "]\n";

    std::cout << "[snpe_identity] backend:      "
              << model.GetBackend() << "\n"
              << "[snpe_identity] model_path:   "
              << model.GetModelPath() << "\n"
              << "[snpe_identity] load_strategy: "
              << model.GetLoadStrategy();
    std::cout << " ("
              << (model.GetLoadStrategy() == 0 ? "eager" : "lazy")
              << ")\n";

    const auto cfg = model.GetConfig();
    if (!cfg.empty()) {
        std::cout << "[snpe_identity] config:       {";
        bool first = true;
        for (const auto& [k, v] : cfg) {
            if (!first) std::cout << ", ";
            std::cout << k << ": " << v;
            first = false;
        }
        std::cout << "}\n";
    }

    // -- Step 3: Prepare input -------------------------------------------
    // The Pipeline auto-converts: uint8 HWC BGR -> float32 NCHW RGB.
    Banner("Step 3: Create Input Image");
    constexpr int kH = 4, kW = 4;
    auto raw_image = CreateSampleBGRImage(kH, kW);
    std::cout << "Created " << kH << "x" << kW
              << " BGR image (HWC uint8).\n";

    // -- Step 4: Run inference --------------------------------------------
    Banner("Step 4: Run SNPE Inference (CPU Runtime)");
    std::vector<atlas::utils::Tensor> outputs;
    ret = model.Run(raw_image, &outputs);

    if (ret == atlas::utils::ErrorCode::kBackendNotFound) {
        std::cout << "SNPE backend not available on this platform "
                  << "(stub returned kBackendNotFound).\n"
                  << "This is expected on non-aarch64 hosts. "
                  << "Run on linux_aarch64 or android_arm64 to execute.\n";
        runtime.Release();
        std::cout << "\nRuntime released. Done.\n";
        return 0;
    }

    if (ret != atlas::utils::ErrorCode::kOk) {
        std::cerr << "[ERROR] Inference failed: "
                  << atlas::utils::ErrorCodeToString(ret) << "\n";
        return 1;
    }

    // -- Step 5: Print results -------------------------------------------
    Banner("Step 5: Results");
    const float* out_data  = static_cast<const float*>(outputs[0].data);
    const size_t out_count = outputs[0].byte_size / sizeof(float);
    int   top1  = ArgMaxAbs(out_data, out_count);
    float value = out_data[top1];
    std::cout << "SNPE identity output: " << out_count
              << " elements, top-1 index=" << top1
              << "  value=" << value << "\n";

    // -- Step 6: Release --------------------------------------------------
    runtime.Release();
    std::cout << "\nRuntime released. Done.\n";
    return 0;
}
